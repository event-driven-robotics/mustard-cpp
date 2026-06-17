#pragma once
#include "mustard/data/DataLoader.h"
#include "mustard/data/DVSEvent.h"

#include <cstdint>
#include <memory>
#include <string>

namespace mustard {

/// Loader for Prophesee RAW event files.
///
/// Uses Prophesee OpenEB / Metavision SDK Stream to open the recording,
/// decode CD events and seek to requested time windows.
class PropheseeRawLoader : public DataLoader<DVSEvent> {
public:
    enum class Format {
        kUnknown,
        kEvt2,
        kEvt21,
        kEvt3,
    };

    PropheseeRawLoader();
    ~PropheseeRawLoader() override;

    PropheseeRawLoader(const PropheseeRawLoader&) = delete;
    PropheseeRawLoader& operator=(const PropheseeRawLoader&) = delete;

    bool open(const std::string& path) override;
    void close() override;
    bool isOpen() const override;

    int64_t startTime() const override;
    int64_t endTime() const override;

    int sensorWidth()  const noexcept;
    int sensorHeight() const noexcept;
    Format format()    const noexcept;

protected:
    DataChunk<DVSEvent> readChunkImpl(int64_t t0, int64_t t1) override;

private:
    static Format parseFormatToken(const std::string& token);

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mustard
