# Supported Formats

Imager validates every file before storing it. Validation is format-specific — it goes beyond checking the file extension and actually decodes or parses the file content to verify integrity. This document describes what each validator does, what extensions it recognizes, and known limitations.

## Format Summary

| Format | Extension(s) | Validation Library | Notes |
|--------|--------------|--------------------|-------|
| JPEG | `.jpg`, `.jpeg` | system libjpeg | Full decode |
| PNG | `.png` | system libpng | Full decode |
| HEIC/HEIF | `.heic`, `.heif` | system libheif | Container decode |
| Nikon NEF | `.nef` | system LibRaw | Raw decode |
| MOV/MP4 | `.mov`, `.mp4` | system libavformat + libavcodec | Container demux + trial decode |
| Apple AAE | `.aae` | Pure C++ (no external dep) | XML/plist structure scan |

All extension matching is case-insensitive. `.JPG`, `.Jpg`, and `.jpg` are all recognized.

---

## JPEG

**Extensions:** `.jpg`, `.jpeg`

**Validation library:** system libjpeg (libjpeg-turbo or standard libjpeg)

**What validation checks:**

- The file begins with the JPEG SOI marker (`0xFF 0xD8`)
- The file can be fully decoded by libjpeg without errors
- libjpeg's error reporting is used to catch corrupt, truncated, or malformed files

**Accepted files:**

- Standard JPEG (JFIF)
- EXIF JPEG (the common format from digital cameras)
- Progressive JPEG
- JPEG files with embedded ICC color profiles
- JPEG files with IPTC metadata

**Rejected files:**

- Files whose content is not JPEG even if the extension is `.jpg`
- Truncated JPEGs (incomplete scan data)
- JPEGs with corrupt Huffman tables or invalid quantization tables
- Files under the minimum size to contain any valid JPEG structure

**Known limitation:** JPEG validation decodes the full image into memory. For very large JPEGs (>100 MB), this can be slow and memory-intensive.

---

## PNG

**Extensions:** `.png`

**Validation library:** system libpng

**What validation checks:**

- The file begins with the PNG signature bytes (`\x89PNG\r\n\x1a\n`)
- The IHDR chunk is present and valid
- The file can be fully decoded by libpng
- CRC checksums on all chunks are verified by libpng

**Accepted files:**

- All bit depths (1, 2, 4, 8, 16)
- All color types (grayscale, RGB, RGBA, indexed, grayscale-alpha)
- Interlaced (Adam7) and non-interlaced PNGs
- PNGs with optional metadata chunks (tEXt, iTXt, pHYs, etc.)

**Rejected files:**

- Files whose content is not PNG even if the extension is `.png`
- PNGs with corrupt chunk data or failing CRC checks
- Truncated PNGs (incomplete IDAT data)

---

## HEIC/HEIF

**Extensions:** `.heic`, `.heif`

**Validation library:** system libheif

**What validation checks:**

- The file is a valid HEIF container (ISO Base Media File Format)
- The container can be decoded by libheif without errors
- At least one image item is present in the container

**Accepted files:**

- HEIC files from iOS/macOS devices
- HEIF files with HEVC, AV1 (AVIF), or other supported codecs
- Multi-image HEIF containers (burst photos, image sequences)
- HEIC files with embedded depth maps or auxiliary images

**Rejected files:**

- Files that are not valid HEIF containers
- Files where libheif reports a decode error
- Containers with no image items

**Notes:**

- HEIF is a container format. Imager validates that the container is well-formed and decodable, not that a specific codec is present.
- `.avif` files (AVIF images) technically use the same HEIF container. They are not listed as a recognized extension, but if passed with a `.heic` or `.heif` extension they will be validated successfully.

---

## Nikon NEF

**Extensions:** `.nef`

**Validation library:** system LibRaw

**What validation checks:**

- The file is a valid RAW container (TIFF-based) readable by LibRaw
- LibRaw can successfully unpack the RAW data
- The file contains at least one valid image component

**Accepted files:**

- Nikon NEF files from any Nikon camera model supported by LibRaw
- Compressed and uncompressed NEF variants

**Rejected files:**

- Files that are not valid RAW containers
- NEF files from camera models not yet supported by the installed LibRaw version
- Truncated or corrupt NEF data

**Notes:**

- LibRaw is updated separately from Imager. If a newer Nikon camera produces NEF files that the installed LibRaw version does not recognize, those files will be rejected as unsupported. Upgrading the system LibRaw package resolves this.
- Only `.nef` is recognized. Canon CR2/CR3, Sony ARW, and other RAW formats are not currently validated (they are rejected as `UnsupportedFormat`).

---

## MOV / MP4

**Extensions:** `.mov`, `.mp4`

**Validation libraries:** system libavformat + libavcodec

**What validation checks:**

- libavformat can open and demux the container (QuickTime/ISOBMFF)
- At least one video or audio stream is present in the container
- libavcodec can perform a trial decode of at least the first frame

The trial decode is a deeper check than container validation alone: it catches files that have a valid container wrapper but corrupt codec data inside.

**Accepted files:**

- MOV files from iOS/macOS devices, GoPro, DJI, and other QuickTime sources
- MP4 files from any source (H.264, H.265/HEVC, AV1, etc.)
- Files with audio-only tracks in a QuickTime container
- Files with multiple video streams

**Rejected files:**

- Files that are not valid QuickTime or ISOBMFF containers
- Containers with no decodable streams
- Files where the first frame of video cannot be decoded by libavcodec

**Notes:**

- Trial decode uses the first video packet in the file. Files with a valid container header but completely corrupt video data (e.g., the first keyframe is missing) will be rejected.
- Very large video files are not read in full during validation — only the container headers and first packet are examined.
- The validation can be slow if a large video file needs a long seek to find the first video packet.

---

## Apple AAE

**Extensions:** `.aae`

**Validation library:** Pure C++ (no external dependency)

**What validation checks:**

- Minimum size of 64 bytes
- Presence of `<?xml` near the start of the file
- Presence of `<plist` within the first 512 bytes
- XML/plist well-formedness (lightweight structural scan)
- Presence of at least one `<dict>` root element with at least one key

**Accepted files:**

- Standard AAE files produced by Apple Photos on iOS and macOS
- Any well-formed XML Apple plist file with a `<dict>` root

**Rejected files:**

- Non-XML data (e.g., binary plists, JPEG headers)
- Files that are too small to contain valid XML
- XML files that are not Apple plist format
- Malformed XML that cannot be parsed

**Notes on sidecar behavior:**

AAE files are not independent media. They are sidecar files that carry non-destructive edit metadata for a parent image. Their storage filename is derived from the parent image's SHA256 hash, not their own content hash. See [Storage and Data Model: AAE Sidecars](storage.md#aae-sidecars) for a full explanation of how pairing, orphan handling, and cascade deletion work.

---

## Adding a File with an Unknown Extension

If you pass a file with an extension that is not in the table above, Imager returns `UnsupportedFormat` without reading the file content.

**Common scenarios:**

- `.cr2`, `.cr3` (Canon RAW) — rejected; LibRaw support planned but not yet implemented
- `.arw` (Sony RAW) — rejected
- `.dng` (Adobe DNG) — rejected
- `.gif`, `.bmp`, `.tiff` — rejected
- `.raf`, `.rw2`, `.orf` — rejected
- `.m4v`, `.ts`, `.mkv` — rejected
- Files with no extension — rejected

To add support for a new format, a validator implementing `validation::IValidator` must be created and registered in `imager::Validators.h`.

---

## Validation Error Codes

| Scenario | `ErrorCode` returned |
|----------|----------------------|
| File is corrupt or malformed | `BrokenFile` |
| Extension is not recognized | `UnsupportedFormat` |
| File is already in the library | `DuplicateFile` |
| Format-specific library not installed | Build failure (at compile time, not runtime) |

`BrokenFile` and `UnsupportedFormat` are distinct codes because the appropriate response is different: a broken file should be flagged for inspection, while an unsupported extension is usually just filtered out of the import.
