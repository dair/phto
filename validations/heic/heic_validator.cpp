#include "heic_validator.h"

#include <libheif/heif.h>

#include <cstdint>
#include <cstring>
#include <memory>

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

} // namespace

ValidationResult validateHeic(const void* data, size_t dataSize) {
  // Step 1: quick rejection — must have ISOBMFF ftyp box at offset 4
  if (!data || dataSize < 12) {
    return WRONG;
  }
  const auto* bytes = static_cast<const uint8_t*>(data);
  if (std::memcmp(bytes + 4, "ftyp", 4) != 0) {
    return WRONG;
  }

  // Step 2: parse container from memory
  ContextPtr ctx{heif_context_alloc()};
  if (!ctx) {
    return INVALID;
  }

  heif_error err = heif_context_read_from_memory_without_copy(ctx.get(), data, dataSize, nullptr);
  if (err.code != heif_error_Ok) {
    return (err.code == heif_error_Unsupported_filetype) ? WRONG : INVALID;
  }

  // Step 3: obtain primary image handle
  heif_image_handle* rawHandle = nullptr;
  err = heif_context_get_primary_image_handle(ctx.get(), &rawHandle);
  if (err.code != heif_error_Ok) {
    return INVALID;
  }
  HandlePtr handle{rawHandle};

  // Step 4: full decode to exercise codec and catch image-level corruption
  heif_image* rawImg = nullptr;
  err = heif_decode_image(handle.get(), &rawImg, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
  if (err.code != heif_error_Ok) {
    return INVALID;
  }
  ImagePtr img{rawImg};

  return VALID;
}
