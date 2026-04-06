#include <validations/nef/nef_validator.h>

#include "Validators.h"

namespace {

class NefValidator final: public validation::IValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".nef";
  }

  validation::ValidationResult validate(const uint8_t* data, size_t size) const override {
    switch (validateNef(data, size)) {
      case VALID:
        return {true, ""};
      case INVALID:
        return {false, "NEF data is corrupted or incomplete"};
      case WRONG:
        return {false, "Data is not a NEF file"};
    }
    return {false, "Unknown validation result"};
  }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IValidator> createNefValidator() {
  return std::make_unique<NefValidator>();
}

} // namespace imager
