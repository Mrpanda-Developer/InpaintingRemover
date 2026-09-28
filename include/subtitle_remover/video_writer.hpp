#pragma once

#include "subtitle_remover/config.hpp"
#include "subtitle_remover/video_types.hpp"

#include <filesystem>
#include <unordered_map>

namespace subtitle_remover {

class VideoWriter {
public:
    VideoWriter(const std::filesystem::path& outputPath,
                const VideoInfo& sourceInfo,
                const AVFormatContext* sourceFormat,
                const ProcessingConfig& config);
    ~VideoWriter();
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;

    void writeAudioPacket(const AVPacket& sourcePacket, int sourceStreamIndex);
    void writeVideoFrame(const VideoFrame& frame);
    void finish();

private:
    void drainEncoder(bool flushing);

    OutputFormatPtr format_;
    CodecContextPtr encoder_;
    SwsContextPtr converter_;
    FramePtr encodeFrame_;
    PacketPtr encodedPacket_;
    int videoStreamIndex_ = -1;
    std::unordered_map<int, int> audioStreamMap_;
    std::unordered_map<int, AVRational> audioSourceTimeBases_;
    bool finished_ = false;
};

} // namespace subtitle_remover