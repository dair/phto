// Separate TU so that mov_validator.h's ::ValidationResult enum does not
// conflict with identically-named enums in other validator headers.
#include "Validators.h"
#include "mov_validator.h"

namespace {

class MovValidator final: public validation::IValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".mov" || ext == ".mp4";
  }

  validation::ValidationResult validate(const uint8_t* data, size_t size) const override {
    switch (validateMov(data, size)) {
      case VALID:
        return {true, ""};
      case INVALID:
        return {false, "MOV/MP4 data is corrupted or incomplete"};
      case WRONG:
        return {false, "Data is not a MOV/MP4 file"};
    }
    return {false, "Unknown validation result"};
  }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IValidator> createMovValidator() {
  return std::make_unique<MovValidator>();
}

} // namespace imager
