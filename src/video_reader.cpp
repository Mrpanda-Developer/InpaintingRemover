#include "subtitle_remover/video_reader.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace subtitle_remover {

VideoReader::VideoReader(const std::filesystem::path& inputPath) {
    AVFormatContext* rawFormat = nullptr;
    const auto input = inputPath.string();
    const int openResult = avformat_open_input(&rawFormat, input.c_str(), nullptr, nullptr);
    if (openResult < 0) {
        if (rawFormat) avformat_close_input(&rawFormat);
        checkFfmpeg(openResult, "open input video");
    }
    format_.reset(rawFormat);
    checkFfmpeg(avformat_find_stream_info(format_.get(), nullptr), "read input stream information");

    const AVCodec* codec = nullptr;
    const int streamIndex = av_find_best_stream(format_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    checkFfmpeg(streamIndex, "find video stream");
    AVStream* stream = format_->streams[streamIndex];
    decoder_.reset(avcodec_alloc_context3(codec));
    if (!decoder_) throw std::runtime_error("allocate video decoder context");
    checkFfmpeg(avcodec_parameters_to_context(decoder_.get(), stream->codecpar), "copy decoder parameters");
    checkFfmpeg(avcodec_open2(decoder_.get(), codec, nullptr), "open video decoder");

    decodedFrame_.reset(av_frame_alloc());
    packet_.reset(av_packet_alloc());
    if (!decodedFrame_ || !packet_) throw std::runtime_error("allocate FFmpeg frame or packet");
    info_.streamIndex = streamIndex;
    info_.timeBase = stream->time_base;
    info_.frameRate = av_guess_frame_rate(format_.get(), stream, nullptr);
    info_.width = decoder_->width;
    info_.height = decoder_->height;
    info_.pixelFormat = decoder_->pix_fmt;
    info_.codecId = stream->codecpar->codec_id;
    for (unsigned int index = 0; index < format_->nb_streams; ++index) {
        if (format_->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            info_.audioStreamIndices.push_back(static_cast<int>(index));
        }
    }
}

AVStream* VideoReader::videoStream() const noexcept {
    return format_->streams[info_.streamIndex];
}

bool VideoReader::read(VideoFrame& output) {
    while (true) {
        const int receiveResult = avcodec_receive_frame(decoder_.get(), decodedFrame_.get());
        if (receiveResult == 0) {
            const auto framePts = decodedFrame_->best_effort_timestamp != AV_NOPTS_VALUE
                ? decodedFrame_->best_effort_timestamp : decodedFrame_->pts;
            std::int64_t pts = framePts;
            if (pts == AV_NOPTS_VALUE) {
                const AVRational frameStep = info_.frameRate.num > 0 && info_.frameRate.den > 0
                    ? av_inv_q(info_.frameRate) : AVRational{1, 25};
                const auto step = std::max<std::int64_t>(1, av_rescale_q(1, frameStep, info_.timeBase));
                pts = lastPts_ == AV_NOPTS_VALUE ? 0 : lastPts_ + step;
            }
            lastPts_ = pts;
            output.pts = pts;
            output.timestampSeconds = pts * av_q2d(info_.timeBase);
            output.bgr.create(decodedFrame_->height, decodedFrame_->width, CV_8UC3);
            converter_.reset(sws_getCachedContext(converter_.release(), decodedFrame_->width,
                decodedFrame_->height, static_cast<AVPixelFormat>(decodedFrame_->format),
                decodedFrame_->width, decodedFrame_->height, AV_PIX_FMT_BGR24,
                SWS_BILINEAR, nullptr, nullptr, nullptr));
            if (!converter_) throw std::runtime_error("create pixel-format converter");
            std::uint8_t* destination[] = { output.bgr.data, nullptr, nullptr, nullptr };
            int destinationLines[] = { static_cast<int>(output.bgr.step), 0, 0, 0 };
            const int scaled = sws_scale(converter_.get(), decodedFrame_->data,
                decodedFrame_->linesize, 0, decodedFrame_->height, destination, destinationLines);
            av_frame_unref(decodedFrame_.get());
            if (scaled != output.bgr.rows) throw std::runtime_error("convert decoded frame to BGR");
            return true;
        }
        if (receiveResult == AVERROR_EOF) return false;
        if (receiveResult != AVERROR(EAGAIN)) checkFfmpeg(receiveResult, "receive decoded video frame");

        if (demuxEnded_) {
            if (!decoderFlushed_) {
                checkFfmpeg(avcodec_send_packet(decoder_.get(), nullptr), "flush video decoder");
                decoderFlushed_ = true;
                continue;
            }
            return false;
        }

        bool packetSent = false;
        while (!packetSent && !demuxEnded_) {
            const int readResult = av_read_frame(format_.get(), packet_.get());
            if (readResult == AVERROR_EOF) {
                demuxEnded_ = true;
                break;
            }
            checkFfmpeg(readResult, "read input packet");
            if (packet_->stream_index == info_.streamIndex) {
                const int sendResult = avcodec_send_packet(decoder_.get(), packet_.get());
                av_packet_unref(packet_.get());
                checkFfmpeg(sendResult, "send packet to video decoder");
                packetSent = true;
            } else {
                if (audioPacketSink_ && packet_->stream_index >= 0 &&
                    format_->streams[packet_->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                    audioPacketSink_(*packet_, packet_->stream_index);
                }
                av_packet_unref(packet_.get());
            }
        }
    }
}

} // namespace subtitle_remover