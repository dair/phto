// Separate TU to avoid enum name collisions with other validator headers.
#include <imager/IStreamValidator.h>
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

struct OptionsDeleter {
  void operator()(heif_decoding_options* o) const noexcept {
    heif_decoding_options_free(o);
  }
};

using OptionsPtr = std::unique_ptr<heif_decoding_options, OptionsDeleter>;
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

// heif_reader callbacks backed by a FILE* plus the (fixed) file size.
struct HeifFileState {
  FILE* f;
  int64_t size;
};

static int64_t heifGetPosition(void* userdata) {
  return static_cast<int64_t>(ftello(static_cast<HeifFileState*>(userdata)->f));
}

static int heifRead(void* data, size_t size, void* userdata) {
  return (fread(data, 1, size, static_cast<HeifFileState*>(userdata)->f) == size) ? 0 : -1;
}

static int heifSeek(int64_t position, void* userdata) {
  return (fseeko(static_cast<HeifFileState*>(userdata)->f, static_cast<off_t>(position), SEEK_SET) == 0) ? 0 : -1;
}

static heif_reader_grow_status heifWaitForFileSize(int64_t targetSize, void* userdata) {
  // File is fully available on disk; libheif probes past EOF and needs to be told so.
  return targetSize > static_cast<HeifFileState*>(userdata)->size ? heif_reader_grow_status_size_beyond_eof
                                                                  : heif_reader_grow_status_size_reached;
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
    if (fseeko(f.get(), 0, SEEK_END) != 0) {
      return {false, "Cannot determine file size: " + path.string()};
    }
    HeifFileState state{f.get(), static_cast<int64_t>(ftello(f.get()))};
    rewind(f.get());

    ContextPtr ctx{heif_context_alloc()};
    if (!ctx) {
      return {false, "heif_context_alloc failed"};
    }

    // Zero-initialize to null all optional v2 function pointers, then set required fields.
    heif_reader reader{};
    reader.reader_api_version = 1;
    reader.get_position = heifGetPosition;
    reader.read = heifRead;
    reader.seek = heifSeek;
    reader.wait_for_file_size = heifWaitForFileSize;

    heif_error err = heif_context_read_from_reader(ctx.get(), &reader, &state, nullptr);
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
    // strict_decoding is what makes truncation fail: with default (lenient) options libheif 1.21 returns Ok with the
    // full declared dimensions for files cut at 5-99% and only records decoding warnings. Strict mode returns an
    // error (iloc extent outside file) at every cut point tried; intact plain/alpha/10-bit/grid files still pass.
    // Keep identical to heic_validator.cpp (StreamValidatorEquivalenceTest).
    OptionsPtr opts{heif_decoding_options_alloc()};
    if (!opts) {
      return {false, "HEIC/HEIF: cannot allocate decoding options"};
    }
    opts->strict_decoding = 1;
    err = heif_decode_image(handle.get(), &rawImg, heif_colorspace_RGB, heif_chroma_interleaved_RGB, opts.get());
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
