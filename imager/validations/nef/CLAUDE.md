# CLAUDE.md — validations/nef/

NEF (Nikon Electronic Format) RAW image validator using the system-installed LibRaw library.

## Public API

```cpp
enum ValidationResult { INVALID = -1, VALID = 0, WRONG = 1 };
ValidationResult validateNef(const void* data, size_t dataSize);
```

Returns `WRONG` if the data lacks a TIFF header magic (`II\x2A\x00` or `MM\x00\x2A`) or is not a recognized RAW format, `INVALID` if LibRaw can identify the format but the sensor data cannot be unpacked (corrupted or truncated), `VALID` if the RAW data is successfully parsed and unpacked.

## Library integration

- Uses system `libraw` via `pkg_check_modules(LibRaw REQUIRED IMPORTED_TARGET libraw)` — imported target `PkgConfig::LibRaw`
- Do NOT bundle LibRaw; it must come from the system
- Required system package: `libraw-dev`

## Validation strategy

Three steps: TIFF magic check (bytes 0–3) → RAW container parse via `libraw_open_buffer()` → sensor data unpack via `libraw_unpack()`. Unlike JPEG/PNG/HEIC, full demosaicing (`libraw_dcraw_process()`) is skipped — it is computationally expensive and unnecessary for integrity validation. `libraw_unpack()` already fully decompresses the sensor data and catches any structural or data corruption. RAII wrapper (`LibRawPtr`) ensures cleanup on all paths.

## Error classification

| LibRaw return code | Result |
|---|---|
| `LIBRAW_SUCCESS` (0) | `VALID` |
| `LIBRAW_FILE_UNSUPPORTED` (-2) | `WRONG` |
| All other negative codes | `INVALID` |

## Test data

Tests use a real NEF file (`test/fixtures/valid.nef`) loaded from disk. Truncated and corrupted variants are derived programmatically in the test code. Tests that require the fixture skip gracefully if the file is absent.

## Supported extensions

`.nef` — handled by `NefValidator` in `imager/NefValidatorImpl.cpp`.

Future candidates: `.nrw` (Nikon compact camera RAW).
