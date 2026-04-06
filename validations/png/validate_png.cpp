#include "validate_png.h"

#include <png.h>
#include <setjmp.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct PngReadState {
  const uint8_t* data;
  size_t size;
  size_t pos;
};

void pngReadCallback(png_structp png_ptr, png_bytep out, png_size_t count) {
  auto* s = static_cast<PngReadState*>(png_get_io_ptr(png_ptr));
  if (s->pos + count > s->size) {
    png_error(png_ptr, "read past end of data");
  }
  std::memcpy(out, s->data + s->pos, count);
  s->pos += count;
}

struct PngReadGuard {
  png_structp png_ptr{nullptr};
  png_infop info_ptr{nullptr};
  PngReadGuard() = default;
  ~PngReadGuard() {
    if (png_ptr) {
      png_destroy_read_struct(&png_ptr, info_ptr ? &info_ptr : nullptr, nullptr);
    }
  }
  PngReadGuard(const PngReadGuard&) = delete;
  PngReadGuard& operator=(const PngReadGuard&) = delete;
};

} // namespace

ValidationResult validatePng(const void* data, size_t dataSize) {
  static const uint8_t PNG_SIG[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

  if (dataSize < 8 || std::memcmp(data, PNG_SIG, 8) != 0) {
    return WRONG;
  }

  PngReadGuard guard;
  guard.png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!guard.png_ptr) {
    return INVALID;
  }

  guard.info_ptr = png_create_info_struct(guard.png_ptr);
  if (!guard.info_ptr) {
    return INVALID;
  }

  PngReadState state{static_cast<const uint8_t*>(data), dataSize, 0};

  if (setjmp(png_jmpbuf(guard.png_ptr))) {
    return INVALID;
  }

  png_set_read_fn(guard.png_ptr, &state, pngReadCallback);
  png_read_info(guard.png_ptr, guard.info_ptr);

  uint32_t height = png_get_image_height(guard.png_ptr, guard.info_ptr);
  size_t rowbytes = png_get_rowbytes(guard.png_ptr, guard.info_ptr);

  std::vector<uint8_t> row(rowbytes);
  for (uint32_t y = 0; y < height; ++y) {
    png_read_row(guard.png_ptr, row.data(), nullptr);
  }

  png_read_end(guard.png_ptr, guard.info_ptr);
  return VALID;
}
