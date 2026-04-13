// Separate TU to avoid enum name collisions with other validator headers.
#include <imager/ImageValidator.h>

#include "MovStreamValidator.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <cstdio>
#include <cstring>
#include <memory>

namespace {

struct FileCloser {
  void operator()(FILE* f) const noexcept {
    if (f) {
      fclose(f);
    }
  }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

struct FileState {
  FILE* f;
  int64_t size;
};

int readPacketFile(void* opaque, uint8_t* buf, int buf_size) {
  auto* s = static_cast<FileState*>(opaque);
  size_t n = fread(buf, 1, static_cast<size_t>(buf_size), s->f);
  return (n == 0) ? AVERROR_EOF : static_cast<int>(n);
}

int64_t seekFile(void* opaque, int64_t offset, int whence) {
  auto* s = static_cast<FileState*>(opaque);
  if (whence == AVSEEK_SIZE) {
    return s->size;
  }
  if (fseeko(s->f, static_cast<off_t>(offset), whence) != 0) {
    return -1;
  }
  return static_cast<int64_t>(ftello(s->f));
}

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

static constexpr size_t kIoBufferSize = 32768;
static constexpr int kMaxPacketsToTry = 8;

static bool isIsobmffBoxType(const uint8_t* p) {
  static constexpr const char* types[] = {"ftyp", "moov", "mdat", "free", "wide", "skip", "pnot"};
  for (const auto* t : types) {
    if (std::memcmp(p, t, 4) == 0) {
      return true;
    }
  }
  return false;
}

class MovStreamValidator final: public validation::IStreamValidator {
public:
  bool supportsExtension(const std::string& ext) const override {
    return ext == ".mov" || ext == ".mp4";
  }

  validation::ValidationResult validateFile(const std::filesystem::path& path) const override {
    FilePtr f{fopen(path.c_str(), "rb")};
    if (!f) {
      return {false, "Cannot open file: " + path.string()};
    }

    // Quick rejection: ISOBMFF box type at offset 4.
    uint8_t magic[8];
    if (fread(magic, 1, 8, f.get()) != 8 || !isIsobmffBoxType(magic + 4)) {
      return {false, "Data is not a MOV/MP4 file"};
    }

    // Determine file size for AVSEEK_SIZE.
    fseeko(f.get(), 0, SEEK_END);
    int64_t fileSize = static_cast<int64_t>(ftello(f.get()));
    rewind(f.get());

    FileState state{f.get(), fileSize};

    FFmpegState ff;

    uint8_t* ioBuf = static_cast<uint8_t*>(av_malloc(kIoBufferSize));
    if (!ioBuf) {
      return {false, "av_malloc failed"};
    }

    ff.avioCtx =
      avio_alloc_context(ioBuf, static_cast<int>(kIoBufferSize), 0, &state, readPacketFile, nullptr, seekFile);
    if (!ff.avioCtx) {
      av_free(ioBuf);
      return {false, "avio_alloc_context failed"};
    }

    ff.fmtCtx = avformat_alloc_context();
    if (!ff.fmtCtx) {
      return {false, "avformat_alloc_context failed"};
    }
    ff.fmtCtx->pb = ff.avioCtx;
    ff.fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&ff.fmtCtx, nullptr, nullptr, nullptr) < 0) {
      return {false, "MOV/MP4 data is corrupted or not a valid container"};
    }

    if (avformat_find_stream_info(ff.fmtCtx, nullptr) < 0) {
      return {false, "MOV/MP4: cannot find stream info"};
    }

    // Find a video stream.
    int videoStream = -1;
    const AVCodec* codec = nullptr;
    for (unsigned i = 0; i < ff.fmtCtx->nb_streams; ++i) {
      if (ff.fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        codec = avcodec_find_decoder(ff.fmtCtx->streams[i]->codecpar->codec_id);
        if (codec) {
          videoStream = static_cast<int>(i);
          break;
        }
      }
    }
    if (videoStream < 0) {
      return {false, "MOV/MP4: no decodable video stream found"};
    }

    ff.codecCtx = avcodec_alloc_context3(codec);
    if (!ff.codecCtx) {
      return {false, "avcodec_alloc_context3 failed"};
    }
    avcodec_parameters_to_context(ff.codecCtx, ff.fmtCtx->streams[videoStream]->codecpar);
    if (avcodec_open2(ff.codecCtx, codec, nullptr) < 0) {
      return {false, "MOV/MP4: cannot open codec"};
    }

    ff.frame = av_frame_alloc();
    ff.pkt = av_packet_alloc();
    if (!ff.frame || !ff.pkt) {
      return {false, "av_frame_alloc/av_packet_alloc failed"};
    }

    int attempts = 0;
    bool decoded = false;
    while (attempts < kMaxPacketsToTry && av_read_frame(ff.fmtCtx, ff.pkt) >= 0) {
      if (ff.pkt->stream_index != videoStream) {
        av_packet_unref(ff.pkt);
        continue;
      }
      ++attempts;
      if (avcodec_send_packet(ff.codecCtx, ff.pkt) >= 0 && avcodec_receive_frame(ff.codecCtx, ff.frame) >= 0) {
        decoded = true;
        av_packet_unref(ff.pkt);
        break;
      }
      av_packet_unref(ff.pkt);
    }

    if (!decoded) {
      return {false, "MOV/MP4: could not decode any video frame"};
    }
    return {true, ""};
  }
};

} // namespace

namespace imager {

std::unique_ptr<validation::IStreamValidator> createMovStreamValidator() {
  return std::make_unique<MovStreamValidator>();
}

} // namespace imager
