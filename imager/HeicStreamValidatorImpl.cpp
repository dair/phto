// Separate TU to avoid enum name collisions with other validator headers.
#include <imager/ImageValidator.h>
#include <libheif/heif.h>

#include <cstdio>
#include <cstring>
#include <memory>

#include "HeicStreamValidator.h"

namespace {

struct ContextDeleter {
  void operator()(heif_context* c) const noexcept {
    heif_context_free(c);
  }
};

struct HandleDeleter {
  void operator()(heif_image_handle* h) const noexcept {
    heif_image_handle_release(h);
  }
};

struct ImageDeleter {
  void operator()(heif_image* i) const noexcept {
    heif_image_release(i);
  }
};

using ContextPtr = std::unique_ptr<heif_context, ContextDeleter>;
using HandlePtr = std::unique_ptr<heif_image_handle, HandleDeleter>;
using ImagePtr = std::unique_ptr<heif_image, ImageDeleter>;

struct FileCloser {
  void operator()(FILE* f) const noexcept {
    if (f) {
      fclose(f);
    }
  }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

// heif_reader callbacks backed by a FILE*.
static int64_t heifGetPosition(void* userdata) {
  auto* f = static_cast<FILE*>(userdata);
  return static_cast<int64_t>(ftello(f));
}

static int heifRead(void* data, size_t size, void* userdata) {
  auto* f = static_cast<FILE*>(userdata);
  return (fread(data, 1, size, f) == size) ? 0 : -1;
}

static int heifSeek(int64_t position, void* userdata) {
  auto* f = static_cast<FILE*>(userdata);
  return (fseeko(f, static_cast<off_t>(position), SEEK_SET) == 0) ? 0 : -1;
}

static heif_reader_grow_status heifWaitForFileSize(int64_t /*target_size*/, void* /*userdata*/) {
  // File is fully available on disk; always report size available.
  return heif_reader_grow_status_size_reached;
}

class HeicStreamValidator final: public validation::IStreamValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".heic" || ext == ".heif";
  }

  validation::ValidationResult validateFile(const std::filesystem::path& path) const override {
    FilePtr f{fopen(path.c_str(), "rb")};
    if (!f) {
      return {false, "Cannot open file: " + path.string()};
    }

    // Quick magic check: ISOBMFF ftyp box must be at offset 4.
    uint8_t magic[12];
    if (fread(magic, 1, 12, f.get()) != 12 || std::memcmp(magic + 4, "ftyp", 4) != 0) {
      return {false, "Data is not a HEIC/HEIF file"};
    }
    rewind(f.get());

    ContextPtr ctx{heif_context_alloc()};
    if (!ctx) {
      return {false, "heif_context_alloc failed"};
    }

    // Zero-initialize to null all optional v2 function pointers, then set required fields.
    heif_reader kReader{};
    kReader.reader_api_version = 1;
    kReader.get_position = heifGetPosition;
    kReader.read = heifRead;
    kReader.seek = heifSeek;
    kReader.wait_for_file_size = heifWaitForFileSize;

    heif_error err = heif_context_read_from_reader(ctx.get(), &kReader, f.get(), nullptr);
    if (err.code != heif_error_Ok) {
      return (err.code == heif_error_Unsupported_filetype)
               ? validation::ValidationResult{false, "Data is not a HEIC/HEIF file"}
               : validation::ValidationResult{false, "HEIC/HEIF container is corrupted"};
    }

    heif_image_handle* rawHandle = nullptr;
    err = heif_context_get_primary_image_handle(ctx.get(), &rawHandle);
    if (err.code != heif_error_Ok) {
      return {false, "HEIC/HEIF: cannot get primary image handle"};
    }
    HandlePtr handle{rawHandle};

    heif_image* rawImg = nullptr;
    err = heif_decode_image(handle.get(), &rawImg, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
    if (err.code != heif_error_Ok) {
      return {false, "HEIC/HEIF image decode failed"};
    }
    ImagePtr img{rawImg};

    return {true, ""};
  }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IStreamValidator> createHeicStreamValidator() {
  return std::make_unique<HeicStreamValidator>();
}

} // namespace imager
