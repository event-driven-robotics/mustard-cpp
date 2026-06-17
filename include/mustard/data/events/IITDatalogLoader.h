#pragma once
#include "mustard/data/DataLoader.h"
#include "mustard/data/DVSEvent.h"

#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace mustard {

/// A packet index entry: the timestamp and the file offset of the first
/// data byte inside the quoted payload of one packet line.
struct IITPacketIndex {
    int64_t      ts_us{0};
    std::streampos data_offset{0};
};

/// DataLoader for the iitdatalog binary DVS event format.
///
/// File format per line:
///   <seq>  <sec.usec>  AE  <duration_usec>  "<escaped_binary>"\n
///
/// Escape rules (reader side):
///   \\  → 0x5C    \n → 0x0A    \r → 0x0D    \0 → 0x00
///   \"  → 0x22    \x → x (any other \x is unescaped to x)
///   A bare 0x22 '"' terminates the payload.
///
/// 32-bit event decode (little-endian uint32):
///   polarity = raw & 0x1
///   x        = (raw >> 1)  & 0x7FF  (11 bits)
///   y        = (raw >> 12) & 0x3FF  (10 bits)
class IITDatalogLoader : public DataLoader<DVSEvent> {
public:
    IITDatalogLoader() = default;
    ~IITDatalogLoader() override { close(); }

    bool open(const std::string& path) override;
    void close() override;
    bool isOpen() const override;

    int64_t startTime() const override;
    int64_t endTime()   const override;

    /// Estimated sensor width in pixels (detected from data on open).
    int sensorWidth()  const noexcept { return sensor_w_; }

    /// Estimated sensor height in pixels (detected from data on open).
    int sensorHeight() const noexcept { return sensor_h_; }

    /// Total number of indexed packets.
    std::size_t packetCount() const noexcept { return index_.size(); }

protected:
    DataChunk<DVSEvent> readChunkImpl(int64_t t0, int64_t t1) override;

private:
    /// Build the packet index by scanning the file line-by-line.
    bool buildIndex();

    /// Sample @p n_packets packets spread across the file to detect sensor
    /// resolution (sets sensor_w_ and sensor_h_).
    void detectResolution(int n_packets = 50);

    /// Parse a "sec.usec" timestamp string into microseconds.
    static int64_t parseTimestamp(const std::string& token);

    /// Unescape an iitdatalog payload read from @p file starting at the
    /// current position.  Reads until a bare '"' is encountered.
    static std::vector<uint8_t> unescapePayload(std::ifstream& file);

    /// Decode four raw bytes (little-endian) into a DVSEvent at time @p ts.
    static DVSEvent decodeEvent(const uint8_t* bytes, int64_t ts);

    std::ifstream                file_;
    std::string                  path_;
    std::vector<IITPacketIndex>  index_;
    int64_t                      start_time_{0};
    int64_t                      end_time_{0};
    int                          sensor_w_{0};
    int                          sensor_h_{0};
};

} // namespace mustard
