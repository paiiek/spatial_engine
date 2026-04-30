// core/src/audio_io/AudioBackend.cpp — describe() for BackendError.

#include "audio_io/AudioBackend.h"

namespace spe::audio_io {

const char* describe(BackendError e) noexcept {
    switch (e) {
        case BackendError::Ok:                    return "ok";
        case BackendError::DeviceOpenFailed:      return "device_open_failed";
        case BackendError::BlockSizeExceedsMax:   return "block_size_exceeds_max";
        case BackendError::SampleRateUnsupported: return "sample_rate_unsupported";
        case BackendError::AlreadyStarted:        return "already_started";
        case BackendError::NotStarted:            return "not_started";
    }
    return "unknown";
}

}  // namespace spe::audio_io
