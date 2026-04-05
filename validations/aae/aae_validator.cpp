#include "aae_validator.h"

#include <cstdint>
#include <cstring>

namespace {

// Search for a substring within the first `limit` bytes of `haystack`.
static bool containsWithinLimit(const char* haystack, size_t haystackLen,
                                const char* needle, size_t needleLen,
                                size_t limit) {
  if (haystackLen < needleLen) {
    return false;
  }
  size_t searchLen = haystackLen < limit ? haystackLen : limit;
  if (searchLen < needleLen) {
    return false;
  }
  for (size_t i = 0; i <= searchLen - needleLen; ++i) {
    if (std::memcmp(haystack + i, needle, needleLen) == 0) {
      return true;
    }
  }
  return false;
}

// Minimal XML well-formedness check: verify matching open/close tags exist
// for <dict> and that it contains at least one <key> element.
// Returns true if the structure looks like a valid plist dict.
static bool hasValidPlistStructure(const char* data, size_t size) {
  // Check for opening <dict> tag
  bool hasDict = containsWithinLimit(data, size, "<dict>", 6, size);
  if (!hasDict) {
    return false;
  }
  // Check for closing </dict>
  bool hasDictClose = containsWithinLimit(data, size, "</dict>", 7, size);
  if (!hasDictClose) {
    return false;
  }
  // Check for at least one <key> element
  bool hasKey = containsWithinLimit(data, size, "<key>", 5, size);
  return hasKey;
}

} // namespace

ValidationResult validateAae(const void* data, size_t dataSize) {
  // Step 1: Quick rejection — minimum size and XML/plist markers
  if (!data || dataSize < 64) {
    return WRONG;
  }

  const char* bytes = static_cast<const char*>(data);

  // Must start with XML declaration near the beginning
  if (!containsWithinLimit(bytes, dataSize, "<?xml", 5, 16)) {
    return WRONG;
  }

  // Must have <plist within the first 512 bytes
  if (!containsWithinLimit(bytes, dataSize, "<plist", 6, 512)) {
    return WRONG;
  }

  // Step 2: Verify it's a plist (not just any XML)
  // Must have </plist> somewhere in the file
  if (!containsWithinLimit(bytes, dataSize, "</plist>", 8, dataSize)) {
    return INVALID;
  }

  // Step 3: Structure check — must have a <dict> with at least one <key>
  if (!hasValidPlistStructure(bytes, dataSize)) {
    return INVALID;
  }

  return VALID;
}
