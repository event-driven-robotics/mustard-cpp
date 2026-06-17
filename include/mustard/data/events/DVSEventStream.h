#pragma once
#include "mustard/data/DataStream.h"
#include "mustard/data/DVSEvent.h"

#include <cstdint>
#include <string>

namespace mustard {

/// Common interface for DVS event streams backed by a DataLoader/DataStream pair.
///
/// Viewer code needs access to the sensor geometry and preferred chunk size, so
/// concrete DVS streams expose those details here instead of forcing the UI to
/// know about a particular file format.
class DVSEventStream : public DataStream<DVSEvent> {
public:
    ~DVSEventStream() override = default;

    virtual int sensorWidth()  const noexcept = 0;
    virtual int sensorHeight() const noexcept = 0;
    virtual int64_t chunkDurationUs() const noexcept = 0;
    virtual const std::string& path() const noexcept = 0;
};

} // namespace mustard
