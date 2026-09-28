#pragma once

#include "subtitle_remover/video_types.hpp"

#include <functional>
#include <filesystem>

namespace subtitle_remover {

class VideoReader {
public:
    using AudioPacketSink = std::function<void(const AVPacket&, int)>;

    explicit VideoReader(const std::filesystem::path& inputPath);
    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;

    const VideoInfo& info() const noexcept { return info_; }
    AVFormatContext* formatContext() const noexcept { return format_.get(); }
    AVStream* videoStream() const noexcept;
    void setAudioPacketSink(AudioPacketSink sink) { audioPacketSink_ = std::move(sink); }
    bool read(VideoFrame& output);

private:
    InputFormatPtr format_;
    CodecContextPtr decoder_;
    FramePtr decodedFrame_;
    PacketPtr packet_;
    SwsContextPtr converter_;
    VideoInfo info_;
    AudioPacketSink audioPacketSink_;
    bool demuxEnded_ = false;
    bool decoderFlushed_ = false;
    std::int64_t lastPts_ = AV_NOPTS_VALUE;
};

} // namespace subtitle_remover