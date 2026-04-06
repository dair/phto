// Separate TU so that heic_validator.h's ::ValidationResult enum does not
// conflict with identically-named enums in other validator headers.
#include <validations/heic/heic_validator.h>

#include "Validators.h"

namespace {

class HeicValidator final: public validation::IValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".heic" || ext == ".heif";
  }

  validation::ValidationResult validate(const uint8_t* data, size_t size) const override {
    switch (validateHeic(data, size)) {
      case VALID:
        return {true, ""};
      case INVALID:
        return {false, "HEIC/HEIF data is corrupted or incomplete"};
      case WRONG:
        return {false, "Data is not a HEIC/HEIF file"};
    }
    return {false, "Unknown validation result"};
  }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IValidator> createHeicValidator() {
  return std::make_unique<HeicValidator>();
}

} // namespace imager
