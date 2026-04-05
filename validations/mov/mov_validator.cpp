#include "mov_validator.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <cstdint>
#include <cstring>

namespace {

// ---- Memory-backed I/O for libavformat ----

struct BufferState {
  const uint8_t* data;
  size_t size;
  size_t pos;
};

int readPacket(void* opaque, uint8_t* buf, int buf_size) {
  auto* s = static_cast<BufferState*>(opaque);
  size_t remaining = s->size - s->pos;
  if (remaining == 0) {
    return AVERROR_EOF;
  }
  size_t n = std::min(remaining, static_cast<size_t>(buf_size));
  std::memcpy(buf, s->data + s->pos, n);
  s->pos += n;
  return static_cast<int>(n);
}

int64_t seekBuffer(void* opaque, int64_t offset, int whence) {
  auto* s = static_cast<BufferState*>(opaque);
  int64_t newPos = 0;
  switch (whence) {
    case SEEK_SET:
      newPos = offset;
      break;
    case SEEK_CUR:
      newPos = static_cast<int64_t>(s->pos) + offset;
      break;
    case SEEK_END:
      newPos = static_cast<int64_t>(s->size) + offset;
      break;
    case AVSEEK_SIZE:
      return static_cast<int64_t>(s->size);
    default:
      return AVERROR(EINVAL);
  }
  if (newPos < 0) {
    newPos = 0;
  }
  if (newPos > static_cast<int64_t>(s->size)) {
    newPos = static_cast<int64_t>(s->size);
  }
  s->pos = static_cast<size_t>(newPos);
  return newPos;
}

// ---- RAII wrapper for all FFmpeg resources ----

struct FFmpegState {
  AVFormatContext* fmtCtx = nullptr;
  AVIOContext* avioCtx = nullptr;
  AVCodecContext* codecCtx = nullptr;
  AVFrame* frame = nullptr;
  AVPacket* pkt = nullptr;

  ~FFmpegState() {
    if (pkt) {
      av_packet_free(&pkt);
    }
    if (frame) {
      av_frame_free(&frame);
    }
    if (codecCtx) {
      avcodec_free_context(&codecCtx);
    }
    if (fmtCtx) {
      avformat_close_input(&fmtCtx);
    }
    if (avioCtx) {
      av_freep(&avioCtx->buffer);
      avio_context_free(&avioCtx);
    }
  }
};

constexpr size_t kIoBufferSize = 32768; // 32 KB I/O buffer
constexpr int kMaxPacketsToTry = 8;     // Try up to 8 video packets for trial decode

// Recognized ISOBMFF box types that can appear at the start of a MOV/MP4 file
bool isIsobmffBoxType(const uint8_t* p) {
  static constexpr const char* types[] = {"ftyp", "moov", "mdat", "free", "wide", "skip", "pnot"};
  for (const auto* t : types) {
    if (std::memcmp(p, t, 4) == 0) {
      return true;
    }
  }
  return false;
}

} // namespace

ValidationResult validateMov(const void* data, size_t dataSize) {
  // Step 1: Quick rejection — check for ISOBMFF box at start
  if (!data || dataSize < 8) {
    return WRONG;
  }
  const auto* bytes = static_cast<const uint8_t*>(data);
  if (!isIsobmffBoxType(bytes + 4)) {
    return WRONG;
  }

  FFmpegState st;
  BufferState buf{bytes, dataSize, 0};

  // Step 2: Set up custom AVIO for memory-backed reading
  auto* ioBuffer = static_cast<uint8_t*>(av_malloc(kIoBufferSize));
  if (!ioBuffer) {
    return INVALID;
  }

  st.avioCtx = avio_alloc_context(
    ioBuffer,
    static_cast<int>(kIoBufferSize),
    0,    // write_flag = 0 (read-only)
    &buf, // opaque
    readPacket,
    nullptr,
    seekBuffer
  );
  if (!st.avioCtx) {
    av_freep(&ioBuffer);
    return INVALID;
  }

  st.fmtCtx = avformat_alloc_context();
  if (!st.fmtCtx) {
    return INVALID;
  }
  // AVFMT_FLAG_CUSTOM_IO tells avformat_close_input NOT to call avio_close(pb),
  // so our FFmpegState destructor can safely free avioCtx itself.
  st.fmtCtx->pb = st.avioCtx;
  st.fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

  int ret = avformat_open_input(&st.fmtCtx, nullptr, nullptr, nullptr);
  if (ret < 0) {
    return INVALID;
  }

  // Step 3: Probe streams
  ret = avformat_find_stream_info(st.fmtCtx, nullptr);
  if (ret < 0) {
    return INVALID;
  }

  // Step 4: Find best video stream
  int videoIdx = av_find_best_stream(st.fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (videoIdx < 0) {
    return INVALID;
  }

  AVStream* videoStream = st.fmtCtx->streams[videoIdx];

  // Step 5: Trial decode — open codec and decode one frame
  const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
  if (!codec) {
    return INVALID;
  }

  st.codecCtx = avcodec_alloc_context3(codec);
  if (!st.codecCtx) {
    return INVALID;
  }

  ret = avcodec_parameters_to_context(st.codecCtx, videoStream->codecpar);
  if (ret < 0) {
    return INVALID;
  }

  ret = avcodec_open2(st.codecCtx, codec, nullptr);
  if (ret < 0) {
    return INVALID;
  }

  st.frame = av_frame_alloc();
  st.pkt = av_packet_alloc();
  if (!st.frame || !st.pkt) {
    return INVALID;
  }

  bool gotFrame = false;
  int packetsRead = 0;
  while (packetsRead < kMaxPacketsToTry) {
    ret = av_read_frame(st.fmtCtx, st.pkt);
    if (ret < 0) {
      break; // EOF or error
    }

    if (st.pkt->stream_index != videoIdx) {
      av_packet_unref(st.pkt);
      continue;
    }
    ++packetsRead;

    ret = avcodec_send_packet(st.codecCtx, st.pkt);
    av_packet_unref(st.pkt);
    if (ret < 0) {
      continue;
    }

    ret = avcodec_receive_frame(st.codecCtx, st.frame);
    if (ret == 0) {
      gotFrame = true;
      break;
    }
  }

  return gotFrame ? VALID : INVALID;
}
