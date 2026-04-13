// Separate TU to avoid enum name collisions with other validator headers.
#include <imager/ImageValidator.h>
#include <png.h>
#include <setjmp.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "PngStreamValidator.h"

namespace {

struct FileCloser {
  void operator()(FILE* f) const noexcept {
    if (f) {
      fclose(f);
    }
  }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

struct PngFileState {
  FILE* f;
};

void pngFileReadCallback(png_structp png_ptr, png_bytep out, png_size_t count) {
  auto* s = static_cast<PngFileState*>(png_get_io_ptr(png_ptr));
  if (fread(out, 1, count, s->f) != count) {
    png_error(png_ptr, "read error");
  }
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

static const uint8_t PNG_SIG[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

class PngStreamValidator final: public validation::IStreamValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".png";
  }

  validation::ValidationResult validateFile(const std::filesystem::path& path) const override {
    FilePtr f{fopen(path.c_str(), "rb")};
    if (!f) {
      return {false, "Cannot open file: " + path.string()};
    }

    uint8_t sig[8];
    if (fread(sig, 1, 8, f.get()) != 8 || std::memcmp(sig, PNG_SIG, 8) != 0) {
      return {false, "Data is not a PNG file"};
    }
    // Rewind so libpng reads from the beginning (it re-reads the signature).
    rewind(f.get());

    PngReadGuard guard;
    guard.png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!guard.png_ptr) {
      return {false, "png_create_read_struct failed"};
    }

    guard.info_ptr = png_create_info_struct(guard.png_ptr);
    if (!guard.info_ptr) {
      return {false, "png_create_info_struct failed"};
    }

    if (setjmp(png_jmpbuf(guard.png_ptr))) {
      return {false, "PNG data is corrupted or incomplete"};
    }

    PngFileState state{f.get()};
    png_set_read_fn(guard.png_ptr, &state, pngFileReadCallback);
    png_read_info(guard.png_ptr, guard.info_ptr);

    uint32_t height = png_get_image_height(guard.png_ptr, guard.info_ptr);
    size_t rowbytes = png_get_rowbytes(guard.png_ptr, guard.info_ptr);

    std::vector<uint8_t> row(rowbytes);
    for (uint32_t y = 0; y < height; ++y) {
      png_read_row(guard.png_ptr, row.data(), nullptr);
    }

    png_read_end(guard.png_ptr, guard.info_ptr);
    return {true, ""};
  }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IStreamValidator> createPngStreamValidator() {
  return std::make_unique<PngStreamValidator>();
}

} // namespace imager
