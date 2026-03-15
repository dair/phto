// Separate TU so that jpeg_validator.h's ::ValidationResult enum does not
// conflict with validate_png.h's identically-named enum.
#include "Validators.h"
#include "jpeg_validator.h"

namespace {

class JpegValidator final : public validation::IValidator {
public:
    bool supportsExtension(const std::string& ext) const override {
        return ext == ".jpg" || ext == ".jpeg";
    }

    validation::ValidationResult validate(const uint8_t* data,
                                          size_t          size) const override {
        switch (validateJpeg(data, size)) {
            case VALID:   return {true,  ""};
            case INVALID: return {false, "JPEG data is corrupted or incomplete"};
            case WRONG:   return {false, "Data is not a JPEG file"};
        }
        return {false, "Unknown validation result"};
    }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IValidator> createJpegValidator() {
    return std::make_unique<JpegValidator>();
}

} // namespace imager
