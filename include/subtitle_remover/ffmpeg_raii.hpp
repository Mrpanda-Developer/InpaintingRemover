#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

#include <memory>
#include <stdexcept>
#include <string>

namespace subtitle_remover {

struct FormatInputDeleter {
    void operator()(AVFormatContext* context) const noexcept {
        if (context) avformat_close_input(&context);
    }
};
struct FormatOutputDeleter {
    void operator()(AVFormatContext* context) const noexcept {
        if (!context) return;
        if (!(context->oformat->flags & AVFMT_NOFILE) && context->pb) avio_closep(&context->pb);
        avformat_free_context(context);
    }
};
struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const noexcept { avcodec_free_context(&context); }
};
struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};
struct PacketDeleter {
    void operator()(AVPacket* packet) const noexcept { av_packet_free(&packet); }
};
struct SwsContextDeleter {
    void operator()(SwsContext* context) const noexcept { sws_freeContext(context); }
};

using InputFormatPtr = std::unique_ptr<AVFormatContext, FormatInputDeleter>;
using OutputFormatPtr = std::unique_ptr<AVFormatContext, FormatOutputDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

inline std::string ffmpegError(int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error, buffer, sizeof(buffer));
    return buffer;
}

inline void checkFfmpeg(int result, const std::string& operation) {
    if (result < 0) throw std::runtime_error(operation + ": " + ffmpegError(result));
}

} // namespace subtitle_remover