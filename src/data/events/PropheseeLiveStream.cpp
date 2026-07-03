#include "mustard/data/events/PropheseeLiveStream.h"

#include <metavision/sdk/base/events/event_cd.h>
#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/stream/camera_exception.h>

#include <memory>
#include <string>

namespace mustard {

class PropheseeLiveStream::Impl {
public:
    bool openFirstAvailable() {
        close();
        try {
            camera_ = std::make_unique<Metavision::Camera>(
                Metavision::Camera::from_first_available());
            sensor_w_ = camera_->geometry().get_width();
            sensor_h_ = camera_->geometry().get_height();
            path_ = "prophesee:first_available";
            camera_->cd().add_callback(
                [this](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
                    for (auto it = begin; it != end; ++it) {
                        buffer_.appendEvent(static_cast<int64_t>(it->t),
                                            static_cast<uint16_t>(it->x),
                                            static_cast<uint16_t>(it->y),
                                            it->p != 0);
                    }
                });
            running_ = camera_->start();
            if (!running_) {
                close();
            }
            return running_;
        } catch (const Metavision::CameraException&) {
            close();
            return false;
        } catch (...) {
            close();
            return false;
        }
    }

    void close() {
        if (camera_) {
            try {
                if (camera_->is_running()) {
                    camera_->stop();
                }
            } catch (...) {
            }
            camera_.reset();
        }
        buffer_.clear();
        sensor_w_ = 0;
        sensor_h_ = 0;
        running_ = false;
        path_.clear();
    }

    bool isOpen() const {
        return running_ && camera_ != nullptr;
    }

    PropheseeLiveBuffer buffer_{PropheseeLiveStream::kChunkDurationUs};
    std::unique_ptr<Metavision::Camera> camera_;
    std::string path_;
    int sensor_w_{0};
    int sensor_h_{0};
    bool running_{false};
};

PropheseeLiveStream::PropheseeLiveStream()
    : impl_(std::make_unique<Impl>())
{}

PropheseeLiveStream::~PropheseeLiveStream() = default;

bool PropheseeLiveStream::open(const std::string&) {
    return openFirstAvailable();
}

bool PropheseeLiveStream::openFirstAvailable() {
    return impl_->openFirstAvailable();
}

void PropheseeLiveStream::close() {
    impl_->close();
}

bool PropheseeLiveStream::isOpen() const {
    return impl_->isOpen();
}

std::optional<DataChunk<DVSEvent>> PropheseeLiveStream::getDataAtTime(int64_t t) {
    if (!isOpen()) return std::nullopt;
    return impl_->buffer_.chunkAt(t);
}

int64_t PropheseeLiveStream::startTime() const {
    return 0;
}

int64_t PropheseeLiveStream::endTime() const {
    return impl_->buffer_.latestTimeUs();
}

int PropheseeLiveStream::sensorWidth() const noexcept {
    return impl_->sensor_w_;
}

int PropheseeLiveStream::sensorHeight() const noexcept {
    return impl_->sensor_h_;
}

int64_t PropheseeLiveStream::chunkDurationUs() const noexcept {
    return kChunkDurationUs;
}

const std::string& PropheseeLiveStream::path() const noexcept {
    return impl_->path_;
}

} // namespace mustard
