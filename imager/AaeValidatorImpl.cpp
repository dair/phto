// Separate TU so that aae_validator.h's ::ValidationResult enum does not
// conflict with identically-named enums in other validator headers.
#include <validations/aae/aae_validator.h>

#include "Validators.h"

namespace {

class AaeValidator final: public validation::IValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".aae";
  }

  validation::ValidationResult validate(const uint8_t* data, size_t size) const override {
    switch (validateAae(data, size)) {
      case VALID:
        return {true, ""};
      case INVALID:
        return {false, "AAE data is corrupted or malformed"};
      case WRONG:
        return {false, "Data is not an AAE file"};
    }
    return {false, "Unknown validation result"};
  }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IValidator> createAaeValidator() {
  return std::make_unique<AaeValidator>();
}

} // namespace imager
