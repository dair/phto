#include "Validators.h"

namespace imager {

std::vector<std::unique_ptr<validation::IValidator>> createDefaultValidators() {
  std::vector<std::unique_ptr<validation::IValidator>> v;
  v.push_back(createJpegValidator());
  v.push_back(createPngValidator());
  v.push_back(createHeicValidator());
  v.push_back(createNefValidator());
  v.push_back(createMovValidator());
  v.push_back(createAaeValidator());
  return v;
}

std::vector<std::unique_ptr<validation::IStreamValidator>> createDefaultStreamValidators() {
  std::vector<std::unique_ptr<validation::IStreamValidator>> v;
  v.push_back(createJpegStreamValidator());
  v.push_back(createPngStreamValidator());
  v.push_back(createHeicStreamValidator());
  v.push_back(createMovStreamValidator());
  v.push_back(createNefStreamValidator());
  v.push_back(createAaeStreamValidator());
  return v;
}

} // namespace imager
