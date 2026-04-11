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

} // namespace imager
