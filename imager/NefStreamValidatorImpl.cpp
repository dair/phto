// Separate TU to avoid enum name collisions with other validator headers.
#include <imager/IStreamValidator.h>
#include <libraw/libraw.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "NefStreamValidator.h"

namespace {

struct FileCloser {
  void operator()(FILE* f) const noexcept {
    if (f) {
      fclose(f);
    }
  }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

/// Disk-backed LibRaw datastream with the exact seek/eof semantics of LibRaw_buffer_datastream, which
/// nef_detail::validateNef (libraw_open_buffer) uses. LibRaw_file_datastream / libraw_open_file differ:
/// they let LibRaw seek past EOF, so e.g. a corrupt IFD0 offset can still be recovered and accepted. Clamping
/// every seek to [0, size] (always "successful") makes both validators reach the same verdict while only
/// FILE*'s small buffer is ever resident.
class ClampedFileStream final: public LibRaw_abstract_datastream {
public:
  explicit ClampedFileStream(const char* path)
    : m_f(fopen(path, "rb")) {
    if (m_f && fseeko(m_f.get(), 0, SEEK_END) == 0) {
      m_size = static_cast<INT64>(ftello(m_f.get()));
      rewind(m_f.get());
    } else {
      m_f.reset();
    }
  }

  int valid() override {
    return m_f != nullptr;
  }

  int read(void* p, size_t sz, size_t n) override {
    return static_cast<int>(fread(p, sz, n, m_f.get()));
  }

  int seek(INT64 o, int whence) override {
    INT64 t = o;
    if (whence == SEEK_CUR) {
      t = tell() + o;
    } else if (whence == SEEK_END) {
      t = m_size + o;
    }
    t = t < 0 ? 0 : (t > m_size ? m_size : t);
    fseeko(m_f.get(), static_cast<off_t>(t), SEEK_SET);
    return 0;
  }

  INT64 tell() override {
    return static_cast<INT64>(ftello(m_f.get()));
  }

  INT64 size() override {
    return m_size;
  }

  int get_char() override {
    return fgetc(m_f.get());
  }

  char* gets(char* s, int n) override {
    return fgets(s, n, m_f.get());
  }

  int scanf_one(const char* fmt, void* v) override {
    return fscanf(m_f.get(), fmt, v);
  }

  int eof() override {
    return tell() >= m_size; // buffer-stream semantics (feof() only fires after a failed read)
  }

private:
  FilePtr m_f;
  INT64 m_size{0};
};

class NefStreamValidator final: public validation::IStreamValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".nef";
  }

  // Mirrors nef_detail::validateNef (validations/nef/nef_validator.cpp) step for step and with the same messages:
  // TIFF magic -> open -> unpack. Only the input source differs (clamped disk stream instead of a buffer).
  validation::ValidationResult validateFile(const std::filesystem::path& path) const override {
    ClampedFileStream stream(path.c_str());
    if (!stream.valid()) {
      return {false, "Cannot open file: " + path.string()};
    }

    uint8_t head[8];
    if (stream.read(head, 1, sizeof(head)) != static_cast<int>(sizeof(head))) {
      return {false, "Data is not a NEF file"};
    }
    const bool littleEndian = head[0] == 0x49 && head[1] == 0x49 && head[2] == 0x2A && head[3] == 0x00;
    const bool bigEndian = head[0] == 0x4D && head[1] == 0x4D && head[2] == 0x00 && head[3] == 0x2A;
    if (!littleEndian && !bigEndian) {
      return {false, "Data is not a NEF file"};
    }
    stream.seek(0, SEEK_SET);

    LibRaw raw; // declared after `stream`: destroyed first; it never owns an external stream
    int err = raw.open_datastream(&stream);
    if (err != LIBRAW_SUCCESS) {
      return (err == LIBRAW_FILE_UNSUPPORTED)
               ? validation::ValidationResult{false, "Data is not a NEF file"}
               : validation::ValidationResult{false, "NEF data is corrupted or incomplete"};
    }

    if (raw.unpack() != LIBRAW_SUCCESS) {
      return {false, "NEF data is corrupted or incomplete"};
    }

    return {true, ""};
  }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IStreamValidator> createNefStreamValidator() {
  return std::make_unique<NefStreamValidator>();
}

} // namespace imager
