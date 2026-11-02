# CLAUDE.md — validations/heic/

HEIC/HEIF image validator using the system-installed libheif library.

## Public API

```cpp
namespace heic_detail {

enum ValidationResult { INVALID = -1, VALID = 0, WRONG = 1 };
ValidationResult validateHeic(const void* data, size_t dataSize);

} // namespace heic_detail
```

Returns `WRONG` if the data lacks an ISOBMFF ftyp box, `INVALID` if the container is corrupt or undecodable, `VALID` if fully decoded successfully.

## Library integration

- Uses system `libheif` via `find_package(libheif REQUIRED)` — imported target `heif`
- Do NOT bundle libheif; it has a codec plugin system that must come from the system
- Required system packages: `libheif-dev`, `libheif1`, and a codec plugin (`libde265-0` for HEVC, `libdav1d` for AV1)

## Validation strategy

Five steps: magic check (ftyp at offset 4) → container parse → primary image handle → full decode → cleanup. Full decode uses `heif_decoding_options` with `strict_decoding = 1`: libheif's default lenient decoding accepts truncated files and only records decoding warnings, so strict mode is required to reject them (the stream validator `imager/HeicStreamValidatorImpl.cpp` does the same). Full decode forces the codec plugin to decompress the image, catching codec-level corruption. RAII wrappers (`ContextPtr`, `HandlePtr`, `ImagePtr`) ensure cleanup on all paths.

## Test data

Tests use a 273-byte AVIF file (HEIF container with AV1 compression) embedded as a `constexpr uint8_t[]` array in `HeicValidatorTest.cpp`. It was generated with `libheif + libaom` encoder. Tests that require actual decode are guarded by `heif_have_decoder_for_format(heif_compression_AV1)` and silently skip if no AV1 decoder is present.

## Supported extensions

`.heic` and `.heif` — handled by `HeicValidator` in `imager/HeicValidatorImpl.cpp`.

## Known residual risk: strict decoding

Strict decoding was verified on the 12 MP fixture and on generated plain 8-bit, RGBA (alpha), 10-bit and 2x2 grid HEICs (all intact files accepted, every truncation rejected). It was NOT verified on multi-image files (several top-level images), HDR files with NCLX/PQ profiles, or real camera/iPhone files beyond the fixture. If a valid real-world HEIC is ever rejected as `BrokenFile`, suspect `strict_decoding` first.
