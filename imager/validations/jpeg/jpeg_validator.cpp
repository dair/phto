#include "jpeg_validator.h"

#include <csetjmp>

extern "C" {
#include <stdio.h>
#include "jpeglib.h"
}

namespace {

struct ErrorManager {
    jpeg_error_mgr pub;
    jmp_buf        escape;
};

[[noreturn]] void onError(j_common_ptr cinfo) {
    auto* em = reinterpret_cast<ErrorManager*>(cinfo->err);
    longjmp(em->escape, 1);
}

// msg_level < 0 means a data-integrity warning — treat it as fatal.
void onMessage(j_common_ptr cinfo, int msg_level) {
    if (msg_level < 0)
        onError(cinfo);
    // Suppress informational trace messages.
}

} // namespace

ValidationResult validateJpeg(const void* data, size_t dataSize) {
    if (!data || dataSize < 2)
        return WRONG;

    const auto* bytes = static_cast<const unsigned char*>(data);
    if (bytes[0] != 0xFF || bytes[1] != 0xD8)
        return WRONG;

    jpeg_decompress_struct cinfo{};
    ErrorManager em{};

    cinfo.err              = jpeg_std_error(&em.pub);
    em.pub.error_exit      = onError;
    em.pub.emit_message    = onMessage;

    if (setjmp(em.escape)) {
        jpeg_destroy_decompress(&cinfo);
        return INVALID;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, bytes, dataSize);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return INVALID;
    }

    jpeg_start_decompress(&cinfo);

    const int rowStride = static_cast<int>(cinfo.output_width) * cinfo.output_components;
    JSAMPARRAY buf = (*cinfo.mem->alloc_sarray)(
        reinterpret_cast<j_common_ptr>(&cinfo), JPOOL_IMAGE, rowStride, 1);

    while (cinfo.output_scanline < cinfo.output_height)
        jpeg_read_scanlines(&cinfo, buf, 1);

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return VALID;
}
