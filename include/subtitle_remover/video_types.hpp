#pragma once

#include "subtitle_remover/ffmpeg_raii.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace subtitle_remover {

struct VideoFrame {
    cv::Mat bgr;
    std::int64_t pts = AV_NOPTS_VALUE;
    double timestampSeconds = 0.0;
};

struct VideoInfo {
    int streamIndex = -1;
    AVRational timeBase{0, 1};
    AVRational frameRate{0, 1};
    int width = 0;
    int height = 0;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    AVCodecID codecId = AV_CODEC_ID_NONE;
    std::vector<int> audioStreamIndices;
};

struct ChunkWindow {
    std::size_t sequence = 0;
    double centralStartSeconds = 0.0;
    double centralEndSeconds = 0.0;
    double contextStartSeconds = 0.0;
    double contextEndSeconds = 0.0;
    std::size_t centralBeginIndex = 0;
    std::size_t centralEndIndex = 0;
    std::vector<VideoFrame> frames;
};

} // namespace subtitle_remover