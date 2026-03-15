// Separate TU so that validate_png.h's ::ValidationResult enum does not
// conflict with jpeg_validator.h's identically-named enum.
#include "Validators.h"
#include "validate_png.h"

namespace {

class PngValidator final : public validation::IValidator {
public:
    bool supportsExtension(const std::string& ext) const override {
        return ext == ".png";
    }

    validation::ValidationResult validate(const uint8_t* data,
                                          size_t          size) const override {
        switch (validatePng(data, size)) {
            case VALID:   return {true,  ""};
            case INVALID: return {false, "PNG data is corrupted or incomplete"};
            case WRONG:   return {false, "Data is not a PNG file"};
        }
        return {false, "Unknown validation result"};
    }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IValidator> createPngValidator() {
    return std::make_unique<PngValidator>();
}

} // namespace imager
