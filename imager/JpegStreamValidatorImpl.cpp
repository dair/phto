// Separate TU to avoid enum name collisions with other validator headers.
#include <imager/ImageValidator.h>

#include <csetjmp>
#include <cstdio>
#include <memory>

#include "JpegStreamValidator.h"

extern "C" {
#include <jpeglib.h>
}

namespace {

struct JpegErrorManager {
  jpeg_error_mgr pub;
  jmp_buf escape;
};

[[noreturn]] void jpegStreamOnError(j_common_ptr cinfo) {
  auto* em = reinterpret_cast<JpegErrorManager*>(cinfo->err);
  longjmp(em->escape, 1);
}

void jpegStreamOnMessage(j_common_ptr cinfo, int msg_level) {
  if (msg_level < 0) {
    jpegStreamOnError(cinfo);
  }
}

struct FileCloser {
  void operator()(FILE* f) const noexcept {
    if (f) {
      fclose(f);
    }
  }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

class JpegStreamValidator final: public validation::IStreamValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".jpg" || ext == ".jpeg";
  }

  validation::ValidationResult validateFile(const std::filesystem::path& path) const override {
    FilePtr f{fopen(path.c_str(), "rb")};
    if (!f) {
      return {false, "Cannot open file: " + path.string()};
    }

    // Peek at first 2 bytes to detect non-JPEG early.
    unsigned char magic[2];
    if (fread(magic, 1, 2, f.get()) != 2 || magic[0] != 0xFF || magic[1] != 0xD8) {
      return {false, "Data is not a JPEG file"};
    }
    rewind(f.get());

    jpeg_decompress_struct cinfo{};
    JpegErrorManager em{};

    cinfo.err = jpeg_std_error(&em.pub);
    em.pub.error_exit = jpegStreamOnError;
    em.pub.emit_message = jpegStreamOnMessage;

    if (setjmp(em.escape)) {
      jpeg_destroy_decompress(&cinfo);
      return {false, "JPEG data is corrupted or incomplete"};
    }

    jpeg_create_decompress(&cinfo);
    // libjpeg reads from the FILE* in its own ~4 KB internal buffers.
    jpeg_stdio_src(&cinfo, f.get());

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
      jpeg_destroy_decompress(&cinfo);
      return {false, "JPEG header invalid"};
    }

    jpeg_start_decompress(&cinfo);

    const int rowStride = static_cast<int>(cinfo.output_width) * cinfo.output_components;
    JSAMPARRAY buf = (*cinfo.mem->alloc_sarray)(reinterpret_cast<j_common_ptr>(&cinfo), JPOOL_IMAGE, rowStride, 1);

    while (cinfo.output_scanline < cinfo.output_height) {
      jpeg_read_scanlines(&cinfo, buf, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return {true, ""};
  }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IStreamValidator> createJpegStreamValidator() {
  return std::make_unique<JpegStreamValidator>();
}

} // namespace imager
