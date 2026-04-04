#include "nef_validator.h"

#include <libraw/libraw.h>

#include <cstdint>
#include <cstring>
#include <memory>

namespace {

struct LibRawDeleter {
  void operator()(libraw_data_t* p) const noexcept {
    libraw_close(p);
  }
};

using LibRawPtr = std::unique_ptr<libraw_data_t, LibRawDeleter>;

} // namespace

ValidationResult validateNef(const void* data, size_t dataSize) {
  // Step 1: Quick rejection — check TIFF header magic
  if (!data || dataSize < 8) {
    return WRONG;
  }
  const auto* bytes = static_cast<const uint8_t*>(data);

  // TIFF little-endian: 49 49 2A 00
  // TIFF big-endian:    4D 4D 00 2A
  bool littleEndian = (bytes[0] == 0x49 && bytes[1] == 0x49 && bytes[2] == 0x2A && bytes[3] == 0x00);
  bool bigEndian = (bytes[0] == 0x4D && bytes[1] == 0x4D && bytes[2] == 0x00 && bytes[3] == 0x2A);
  if (!littleEndian && !bigEndian) {
    return WRONG;
  }

  // Step 2: Create LibRaw processor and open from memory
  LibRawPtr raw{libraw_init(0)};
  if (!raw) {
    return INVALID;
  }

  int err = libraw_open_buffer(raw.get(), data, dataSize);
  if (err != LIBRAW_SUCCESS) {
    return (err == LIBRAW_FILE_UNSUPPORTED) ? WRONG : INVALID;
  }

  // Step 3: Unpack RAW sensor data
  err = libraw_unpack(raw.get());
  if (err != LIBRAW_SUCCESS) {
    return INVALID;
  }

  return VALID;
}
