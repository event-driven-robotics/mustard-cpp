#include "mustard/data/events/IITDatalogLoader.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mustard {

// ---------------------------------------------------------------------------
// open / close / isOpen
// ---------------------------------------------------------------------------

bool IITDatalogLoader::open(const std::string& path) {
    close();
    path_ = path;
    file_.open(path, std::ios::in | std::ios::binary);
    if (!file_.is_open()) {
        return false;
    }
    if (!buildIndex()) {
        close();
        return false;
    }
    if (index_.empty()) {
        close();
        return false;
    }
    start_time_ = index_.front().ts_us;
    end_time_   = index_.back().ts_us;
    detectResolution(50);
    return true;
}

void IITDatalogLoader::close() {
    if (file_.is_open()) {
        file_.close();
    }
    index_.clear();
    start_time_ = 0;
    end_time_   = 0;
    sensor_w_   = 0;
    sensor_h_   = 0;
    path_.clear();
}

bool IITDatalogLoader::isOpen() const {
    return file_.is_open();
}

int64_t IITDatalogLoader::startTime() const { return start_time_; }
int64_t IITDatalogLoader::endTime()   const { return end_time_;   }

// ---------------------------------------------------------------------------
// buildIndex
// ---------------------------------------------------------------------------

bool IITDatalogLoader::buildIndex() {
    file_.clear();
    file_.seekg(0, std::ios::beg);

    std::string line;
    while (true) {
        std::streampos line_start = file_.tellg();
        if (!std::getline(file_, line)) {
            break;
        }
        // Skip empty lines and comment-like lines
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Find the opening quote that marks the start of the binary payload.
        // The line format is:  <seq>  <ts>  AE  <dur>  "<payload>"
        // We want the position of the first '"' in the line.
        auto quote_pos = line.find('"');
        if (quote_pos == std::string::npos) {
            continue; // not a data line
        }

        // Parse the timestamp from the second whitespace-delimited token
        std::istringstream iss(line);
        std::string seq_token, ts_token;
        if (!(iss >> seq_token >> ts_token)) {
            continue;
        }
        int64_t ts = parseTimestamp(ts_token);

        // data_offset points to the first payload byte (character after '"')
        std::streampos data_offset = static_cast<std::streamoff>(line_start)
                                   + static_cast<std::streamoff>(quote_pos + 1);

        index_.push_back({ts, data_offset});
    }

    file_.clear(); // clear EOF flag
    return !index_.empty();
}

// ---------------------------------------------------------------------------
// detectResolution
// ---------------------------------------------------------------------------

void IITDatalogLoader::detectResolution(int n_packets) {
    if (index_.empty()) return;

    int max_x = 0;
    int max_y = 0;

    std::size_t step = std::max(std::size_t{1}, index_.size() / static_cast<std::size_t>(n_packets));

    for (std::size_t i = 0; i < index_.size(); i += step) {
        const auto& entry = index_[i];
        file_.clear();
        file_.seekg(entry.data_offset);
        if (!file_) continue;

        auto raw_bytes = unescapePayload(file_);
        int64_t ts = entry.ts_us;

        for (std::size_t b = 0; b + 3 < raw_bytes.size(); b += 4) {
            DVSEvent ev = decodeEvent(&raw_bytes[b], ts);
            if (ev.x > max_x) max_x = ev.x;
            if (ev.y > max_y) max_y = ev.y;
        }
    }

    sensor_w_ = max_x + 1;
    sensor_h_ = max_y + 1;
}

// ---------------------------------------------------------------------------
// readChunkImpl
// ---------------------------------------------------------------------------

DataChunk<DVSEvent> IITDatalogLoader::readChunkImpl(int64_t t0, int64_t t1) {
    DataChunk<DVSEvent> chunk;
    chunk.t_start = t0;
    chunk.t_end   = t1;

    if (!isOpen() || index_.empty()) {
        return chunk;
    }

    // Find the first packet whose timestamp >= t0
    auto it = std::lower_bound(
        index_.begin(), index_.end(), t0,
        [](const IITPacketIndex& e, int64_t ts) { return e.ts_us < ts; }
    );

    for (; it != index_.end() && it->ts_us < t1; ++it) {
        file_.clear();
        file_.seekg(it->data_offset);
        if (!file_) continue;

        auto raw_bytes = unescapePayload(file_);
        int64_t ts = it->ts_us;

        for (std::size_t b = 0; b + 3 < raw_bytes.size(); b += 4) {
            chunk.data.push_back(decodeEvent(&raw_bytes[b], ts));
        }
    }

    return chunk;
}

// ---------------------------------------------------------------------------
// parseTimestamp
// ---------------------------------------------------------------------------

int64_t IITDatalogLoader::parseTimestamp(const std::string& token) {
    // Format: "sec.usec" where usec is always 6 digits
    auto dot = token.find('.');
    if (dot == std::string::npos) {
        return std::stoll(token) * 1'000'000LL;
    }
    int64_t sec  = std::stoll(token.substr(0, dot));
    std::string frac = token.substr(dot + 1);
    // Pad or truncate to 6 digits
    while (frac.size() < 6) frac += '0';
    frac = frac.substr(0, 6);
    int64_t usec = std::stoll(frac);
    return sec * 1'000'000LL + usec;
}

// ---------------------------------------------------------------------------
// unescapePayload
// ---------------------------------------------------------------------------

std::vector<uint8_t> IITDatalogLoader::unescapePayload(std::ifstream& file) {
    std::vector<uint8_t> out;
    // Reserve a reasonable initial capacity
    out.reserve(256);

    int c = file.get();
    while (c != EOF) {
        if (c == '"') {
            // Bare '"' terminates the payload
            break;
        }
        if (c == '\\') {
            int next = file.get();
            if (next == EOF) break;
            switch (next) {
                case '\\': out.push_back(0x5C); break;
                case 'n':  out.push_back(0x0A); break;
                case 'r':  out.push_back(0x0D); break;
                case '0':  out.push_back(0x00); break;
                default:   out.push_back(static_cast<uint8_t>(next)); break;
            }
        } else {
            out.push_back(static_cast<uint8_t>(c));
        }
        c = file.get();
    }
    return out;
}

// ---------------------------------------------------------------------------
// decodeEvent
// ---------------------------------------------------------------------------

DVSEvent IITDatalogLoader::decodeEvent(const uint8_t* bytes, int64_t ts) {
    // Little-endian 32-bit word
    uint32_t raw = static_cast<uint32_t>(bytes[0])
                 | (static_cast<uint32_t>(bytes[1]) << 8)
                 | (static_cast<uint32_t>(bytes[2]) << 16)
                 | (static_cast<uint32_t>(bytes[3]) << 24);

    DVSEvent ev;
    ev.t        = ts;
    ev.polarity = static_cast<bool>(raw & 0x1u);
    ev.x        = static_cast<uint16_t>((raw >> 1)  & 0x7FFu);
    ev.y        = static_cast<uint16_t>((raw >> 12) & 0x3FFu);
    return ev;
}

} // namespace mustard
