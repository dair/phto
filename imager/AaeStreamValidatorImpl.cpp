// Separate TU to avoid enum name collisions with other validator headers.
#include <imager/ImageValidator.h>
#include <validations/aae/aae_validator.h>

#include <cstdio>
#include <memory>

#include "AaeStreamValidator.h"

namespace {

struct FileCloser {
  void operator()(FILE* f) const noexcept {
    if (f) {
      fclose(f);
    }
  }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

// AAE files are tiny XML sidecar files (typically < 10 KB).
// Read up to 64 KB and delegate to the existing memory-based validator.
// If the file is larger than 64 KB it cannot be a valid AAE.
static constexpr size_t kMaxAaeReadBytes = 65536;

class AaeStreamValidator final: public validation::IStreamValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".aae";
  }

  validation::ValidationResult validateFile(const std::filesystem::path& path) const override {
    FilePtr f{fopen(path.c_str(), "rb")};
    if (!f) {
      return {false, "Cannot open file: " + path.string()};
    }

    char buf[kMaxAaeReadBytes];
    size_t n = fread(buf, 1, sizeof(buf), f.get());

    switch (validateAae(buf, n)) {
      case VALID:
        return {true, ""};
      case INVALID:
        return {false, "AAE data is corrupted or incomplete"};
      case WRONG:
        return {false, "Data is not an AAE file"};
    }
    return {false, "Unknown validation result"};
  }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IStreamValidator> createAaeStreamValidator() {
  return std::make_unique<AaeStreamValidator>();
}

} // namespace imager
