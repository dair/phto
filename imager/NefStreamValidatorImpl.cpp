// Separate TU to avoid enum name collisions with other validator headers.
#include <imager/ImageValidator.h>
#include <libraw/libraw.h>

#include <cstdint>
#include <cstring>
#include <memory>

#include "NefStreamValidator.h"

namespace {

struct LibRawDeleter {
  void operator()(libraw_data_t* p) const noexcept {
    libraw_close(p);
  }
};

using LibRawPtr = std::unique_ptr<libraw_data_t, LibRawDeleter>;

class NefStreamValidator final: public validation::IStreamValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".nef";
  }

  validation::ValidationResult validateFile(const std::filesystem::path& path) const override {
    LibRawPtr raw{libraw_init(0)};
    if (!raw) {
      return {false, "libraw_init failed"};
    }

    // libraw_open_file() opens and reads from disk directly.
    int err = libraw_open_file(raw.get(), path.c_str());
    if (err != LIBRAW_SUCCESS) {
      return (err == LIBRAW_FILE_UNSUPPORTED)
               ? validation::ValidationResult{false, "Data is not a NEF file"}
               : validation::ValidationResult{false, "NEF data is corrupted or incomplete"};
    }

    err = libraw_unpack(raw.get());
    if (err != LIBRAW_SUCCESS) {
      return {false, "NEF sensor data unpack failed"};
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
