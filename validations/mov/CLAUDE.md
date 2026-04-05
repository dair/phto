# CLAUDE.md — validations/mov/

MOV/MP4 video validator using the system-installed FFmpeg libraries (libavformat, libavcodec, libavutil).

## Public API

```cpp
enum ValidationResult { INVALID = -1, VALID = 0, WRONG = 1 };
ValidationResult validateMov(const void* data, size_t dataSize);
```

Returns `WRONG` if the data lacks a recognized ISOBMFF box type at offset 4, `INVALID` if the container is
corrupt, has no video stream, or the codec fails to decode a frame, `VALID` if at least one video frame was
successfully decoded.

## Library integration

- Uses system FFmpeg libraries via pkg-config:
  - `libavformat` — container demuxing (`avformat_open_input`, `avformat_find_stream_info`)
  - `libavcodec` — codec decoding (`avcodec_open2`, `avcodec_send_packet`, `avcodec_receive_frame`)
  - `libavutil` — memory management and error codes (`av_malloc`, `AVERROR_EOF`, etc.)
- Do NOT bundle FFmpeg; it has a codec plugin system that must come from the system
- Required system packages: `libavformat-dev`, `libavcodec-dev`, `libavutil-dev`

## Validation strategy

Six steps: ISOBMFF box check (offset 4) → custom AVIOContext from memory buffer → avformat_open_input →
avformat_find_stream_info → find video stream → trial decode (up to 8 packets, stop at first frame).
RAII struct `FFmpegState` ensures all resources (AVFormatContext, AVIOContext, AVCodecContext, AVFrame,
AVPacket) are freed on all paths including early returns.

## Memory I/O

libavformat normally reads from files. This validator constructs a custom `AVIOContext` with `readPacket`
and `seekBuffer` callbacks backed by the in-memory data buffer. The `AVSEEK_SIZE` case in `seekBuffer` is
required so that libavformat's MOV demuxer can determine the file size for parsing the moov atom.

## Test data

Tests use a 1416-byte MOV file (H.264 baseline, 16x16, 1 frame) embedded as a `static const uint8_t[]`
array in `MovValidatorTest.cpp`. The same file is stored as `test/fixtures/valid.mov`.
Generated with: `ffmpeg -f lavfi -i color=c=red:s=16x16:d=0.04 -c:v libx264 -profile:v baseline -pix_fmt yuv420p -frames:v 1 valid.mov`

## Supported extensions

`.mov` and `.mp4` — both handled by `MovValidator` in `imager/MovValidatorImpl.cpp`.
Both formats use the same ISOBMFF container and the same libavformat demuxer.
