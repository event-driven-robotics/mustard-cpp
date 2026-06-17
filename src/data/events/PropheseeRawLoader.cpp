#include "mustard/data/events/PropheseeRawLoader.h"

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/stream/camera_exception.h>
#include <metavision/sdk/stream/file_config_hints.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mustard {

namespace {

std::string upperCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

} // namespace

class PropheseeRawLoader::Impl {
public:
    using ProgressCallback = DataLoader<DVSEvent>::ProgressCallback;

    bool open(const std::string& path, ProgressCallback progress_cb) {
        close();
        path_ = path;
        progress_cb_ = std::move(progress_cb);
        reportProgress(0.0f, "Opening Prophesee RAW");

        try {
            auto hints = Metavision::FileConfigHints()
                             .real_time_playback(false)
                             .time_shift(false);
            camera_ = std::make_unique<Metavision::Camera>(
                Metavision::Camera::from_file(path, hints));

            sensor_w_ = camera_->geometry().get_width();
            sensor_h_ = camera_->geometry().get_height();
            format_ = parseFormatToken(camera_->get_camera_configuration().data_encoding_format);

            camera_->cd().add_callback(
                [this](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
                    onCdEvents(begin, end);
                });

            reportProgress(0.25f, "Preparing OpenEB seek index");
            auto& osc = camera_->offline_streaming_control();
            float wait_progress = 0.25f;
            while (!osc.is_ready()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                wait_progress = std::min(0.85f, wait_progress + 0.01f);
                reportProgress(wait_progress, "Preparing OpenEB seek index");
            }

            seek_start_time_ = static_cast<int64_t>(osc.get_seek_start_time());
            seek_end_time_ = static_cast<int64_t>(osc.get_seek_end_time());
            const int64_t duration = static_cast<int64_t>(osc.get_duration());
            const int64_t tentative_end = seek_start_time_ + std::max<int64_t>(duration, 1);

            reportProgress(0.9f, "Reading stream bounds");
            const auto first = readEventsWindow(seek_start_time_, tentative_end + 1, 1);
            if (first.empty()) {
                close();
                return false;
            }

            start_time_ = first.front().t;
            const int64_t tail_start = std::max(seek_start_time_, seek_end_time_);
            const auto tail = readEventsWindow(tail_start, tentative_end + 1, 0);
            end_time_ = tail.empty() ? first.back().t : tail.back().t;
            has_events_ = end_time_ >= start_time_;

            reportProgress(1.0f, "Ready");
            return has_events_;
        } catch (const Metavision::CameraException&) {
            close();
            return false;
        } catch (const std::exception&) {
            close();
            return false;
        }
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            read_active_ = false;
            read_done_ = true;
            read_buffer_.clear();
        }
        callback_cv_.notify_all();

        if (camera_) {
            try {
                if (camera_->is_running()) {
                    camera_->stop();
                }
            } catch (...) {
            }
            camera_.reset();
        }

        path_.clear();
        progress_cb_ = {};
        format_ = Format::kUnknown;
        sensor_w_ = 0;
        sensor_h_ = 0;
        start_time_ = 0;
        end_time_ = 0;
        seek_start_time_ = 0;
        seek_end_time_ = 0;
        has_events_ = false;
    }

    bool isOpen() const {
        return camera_ != nullptr && has_events_;
    }

    int64_t startTime() const {
        return start_time_;
    }

    int64_t endTime() const {
        return end_time_;
    }

    int sensorWidth() const {
        return sensor_w_;
    }

    int sensorHeight() const {
        return sensor_h_;
    }

    Format format() const {
        return format_;
    }

    DataChunk<DVSEvent> readChunk(int64_t t0, int64_t t1) {
        DataChunk<DVSEvent> chunk;
        chunk.t_start = t0;
        chunk.t_end = t1;

        if (!isOpen() || t1 <= t0) {
            return chunk;
        }

        chunk.data = readEventsWindow(t0, t1, 0);
        return chunk;
    }

private:
    void reportProgress(float progress, const std::string& stage) const {
        if (progress_cb_) {
            progress_cb_(progress, stage);
        }
    }

    std::vector<DVSEvent> readEventsWindow(int64_t t0, int64_t t1, std::size_t max_events) {
        std::vector<DVSEvent> result;
        if (!camera_ || t1 <= t0) {
            return result;
        }

        std::lock_guard<std::mutex> stream_lock(stream_mutex_);

        const int64_t seek_upper = std::max(seek_start_time_, seek_end_time_);
        const int64_t seek_ts = std::clamp(t0, seek_start_time_, seek_upper);

        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            read_buffer_.clear();
            read_t0_ = t0;
            read_t1_ = t1;
            read_max_events_ = max_events;
            read_active_ = true;
            read_done_ = false;
        }

        try {
            auto& osc = camera_->offline_streaming_control();
            if (!osc.seek(static_cast<Metavision::timestamp>(seek_ts))) {
                finishRead(result);
                return result;
            }

            if (!camera_->start()) {
                finishRead(result);
                return result;
            }

            waitForReadCompletion();

            camera_->stop();
        } catch (...) {
            try {
                if (camera_) {
                    camera_->stop();
                }
            } catch (...) {
            }
        }

        finishRead(result);
        return result;
    }

    void waitForReadCompletion() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(callback_mutex_);
                if (callback_cv_.wait_for(lock, std::chrono::milliseconds(2),
                                          [this] { return read_done_; })) {
                    return;
                }
            }

            if (!camera_->is_running()) {
                return;
            }
        }
    }

    void finishRead(std::vector<DVSEvent>& result) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        read_active_ = false;
        read_done_ = true;
        result = std::move(read_buffer_);
        read_buffer_.clear();
    }

    void onCdEvents(const Metavision::EventCD* begin, const Metavision::EventCD* end) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (!read_active_) {
            return;
        }

        for (auto it = begin; it != end; ++it) {
            const int64_t t = static_cast<int64_t>(it->t);
            if (t >= read_t1_) {
                read_done_ = true;
                break;
            }
            if (t < read_t0_) {
                continue;
            }

            DVSEvent ev;
            ev.t = t;
            ev.x = static_cast<uint16_t>(it->x);
            ev.y = static_cast<uint16_t>(it->y);
            ev.polarity = it->p != 0;
            read_buffer_.push_back(ev);

            if (read_max_events_ > 0 && read_buffer_.size() >= read_max_events_) {
                read_done_ = true;
                break;
            }
        }

        callback_cv_.notify_all();
    }

    std::unique_ptr<Metavision::Camera> camera_;
    std::string path_;
    ProgressCallback progress_cb_;

    Format format_{Format::kUnknown};
    int sensor_w_{0};
    int sensor_h_{0};
    int64_t start_time_{0};
    int64_t end_time_{0};
    int64_t seek_start_time_{0};
    int64_t seek_end_time_{0};
    bool has_events_{false};

    std::mutex stream_mutex_;
    std::mutex callback_mutex_;
    std::condition_variable callback_cv_;
    std::vector<DVSEvent> read_buffer_;
    int64_t read_t0_{0};
    int64_t read_t1_{0};
    std::size_t read_max_events_{0};
    bool read_active_{false};
    bool read_done_{false};
};

PropheseeRawLoader::PropheseeRawLoader()
    : impl_(std::make_unique<Impl>())
{}

PropheseeRawLoader::~PropheseeRawLoader() = default;

bool PropheseeRawLoader::open(const std::string& path) {
    return impl_->open(path, [this](float progress, const std::string& stage) {
        reportProgress(progress, stage);
    });
}

void PropheseeRawLoader::close() {
    impl_->close();
}

bool PropheseeRawLoader::isOpen() const {
    return impl_->isOpen();
}

int64_t PropheseeRawLoader::startTime() const {
    return impl_->startTime();
}

int64_t PropheseeRawLoader::endTime() const {
    return impl_->endTime();
}

int PropheseeRawLoader::sensorWidth() const noexcept {
    return impl_->sensorWidth();
}

int PropheseeRawLoader::sensorHeight() const noexcept {
    return impl_->sensorHeight();
}

PropheseeRawLoader::Format PropheseeRawLoader::format() const noexcept {
    return impl_->format();
}

DataChunk<DVSEvent> PropheseeRawLoader::readChunkImpl(int64_t t0, int64_t t1) {
    return impl_->readChunk(t0, t1);
}

PropheseeRawLoader::Format PropheseeRawLoader::parseFormatToken(const std::string& token) {
    const std::string value = upperCopy(token);
    if (value.find("EVT2.1") != std::string::npos || value.find("EVT21") != std::string::npos) {
        return Format::kEvt21;
    }
    if (value.find("EVT2") != std::string::npos) {
        return Format::kEvt2;
    }
    if (value.find("EVT3") != std::string::npos) {
        return Format::kEvt3;
    }
    return Format::kUnknown;
}

} // namespace mustard
