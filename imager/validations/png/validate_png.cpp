#include "validate_png.h"

#include <cstdint>
#include <cstring>
#include <vector>
#include <setjmp.h>

#include <png.h>

namespace {

struct PngReadState {
    const uint8_t* data;
    size_t         size;
    size_t         pos;
};

void pngReadCallback(png_structp png_ptr, png_bytep out, png_size_t count)
{
    auto* s = static_cast<PngReadState*>(png_get_io_ptr(png_ptr));
    if (s->pos + count > s->size) {
        png_error(png_ptr, "read past end of data");
    }
    std::memcpy(out, s->data + s->pos, count);
    s->pos += count;
}

} // namespace

ValidationResult validatePng(const void* data, size_t dataSize)
{
    static const uint8_t PNG_SIG[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    if (dataSize < 8 || std::memcmp(data, PNG_SIG, 8) != 0) {
        return WRONG;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        return INVALID;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        return INVALID;
    }

    PngReadState state{static_cast<const uint8_t*>(data), dataSize, 0};

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        return INVALID;
    }

    png_set_read_fn(png_ptr, &state, pngReadCallback);
    png_read_info(png_ptr, info_ptr);

    uint32_t height   = png_get_image_height(png_ptr, info_ptr);
    size_t   rowbytes = png_get_rowbytes(png_ptr, info_ptr);

    std::vector<uint8_t> row(rowbytes);
    for (uint32_t y = 0; y < height; ++y) {
        png_read_row(png_ptr, row.data(), nullptr);
    }

    png_read_end(png_ptr, info_ptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return VALID;
}
