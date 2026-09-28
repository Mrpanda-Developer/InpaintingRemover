#include "subtitle_remover/video_writer.hpp"

#include "subtitle_remover/logging.hpp"

#include <algorithm>
#include <libavutil/opt.h>
#include <stdexcept>
#include <string>

namespace subtitle_remover {
namespace {

AVCodecID outputCodec(VideoCodec codec) {
    switch (codec) {
    case VideoCodec::H264: return AV_CODEC_ID_H264;
    case VideoCodec::Hevc: return AV_CODEC_ID_HEVC;
    case VideoCodec::Prores: return AV_CODEC_ID_PRORES;
    }
    return AV_CODEC_ID_H264;
}

AVPixelFormat selectPixelFormat(const AVCodec* codec) {
    if (!codec->pix_fmts) return AV_PIX_FMT_YUV420P;
    for (const AVPixelFormat* candidate = codec->pix_fmts;
         *candidate != AV_PIX_FMT_NONE; ++candidate) {
        if (*candidate == AV_PIX_FMT_YUV420P) return *candidate;
    }
    return codec->pix_fmts[0];
}

} // namespace

VideoWriter::VideoWriter(const std::filesystem::path& outputPath,
                         const VideoInfo& sourceInfo,
                         const AVFormatContext* sourceFormat,
                         const ProcessingConfig& config) {
    if (config.audioMode == AudioMode::Reencode) {
        throw std::runtime_error("audio re-encoding is not implemented in this MVP; use --audio-mode copy");
    }
    if (sourceInfo.timeBase.num <= 0 || sourceInfo.timeBase.den <= 0) {
        throw std::runtime_error("input video has an invalid time base");
    }
    AVFormatContext* rawFormat = nullptr;
    const auto output = outputPath.string();
    const int allocResult = avformat_alloc_output_context2(&rawFormat, nullptr, nullptr, output.c_str());
    if (allocResult < 0) {
        if (rawFormat) avformat_free_context(rawFormat);
        checkFfmpeg(allocResult, "create output container");
    }
    if (!rawFormat) throw std::runtime_error("could not infer output container from output path");
    format_.reset(rawFormat);
    av_dict_copy(&format_->metadata, sourceFormat->metadata, 0);

    const AVCodecID codecId = outputCodec(config.videoCodec);
    const AVCodec* codec = avcodec_find_encoder(codecId);
    if (!codec) throw std::runtime_error("requested video encoder is unavailable in this FFmpeg build");
    AVStream* videoStream = avformat_new_stream(format_.get(), nullptr);
    if (!videoStream) throw std::runtime_error("create output video stream");
    videoStreamIndex_ = videoStream->index;

    encoder_.reset(avcodec_alloc_context3(codec));
    if (!encoder_) throw std::runtime_error("allocate video encoder context");
    encoder_->codec_id = codecId;
    encoder_->codec_type = AVMEDIA_TYPE_VIDEO;
    encoder_->width = sourceInfo.width;
    encoder_->height = sourceInfo.height;
    encoder_->time_base = sourceInfo.timeBase;
    encoder_->framerate = sourceInfo.frameRate.num > 0 && sourceInfo.frameRate.den > 0
        ? sourceInfo.frameRate : AVRational{25, 1};
    encoder_->pix_fmt = selectPixelFormat(codec);
    encoder_->sample_aspect_ratio = sourceFormat->streams[sourceInfo.streamIndex]->sample_aspect_ratio;
    encoder_->color_range = sourceFormat->streams[sourceInfo.streamIndex]->codecpar->color_range;
    encoder_->color_primaries = sourceFormat->streams[sourceInfo.streamIndex]->codecpar->color_primaries;
    encoder_->color_trc = sourceFormat->streams[sourceInfo.streamIndex]->codecpar->color_trc;
    encoder_->colorspace = sourceFormat->streams[sourceInfo.streamIndex]->codecpar->color_space;
    encoder_->thread_count = config.threads;
    encoder_->gop_size = 48;
    encoder_->max_b_frames = 0;
    if (format_->oformat->flags & AVFMT_GLOBALHEADER) encoder_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (codecId != AV_CODEC_ID_PRORES && encoder_->priv_data) {
        const char* encoderPreset = config.preset == Preset::Fast ? "ultrafast" :
                                   config.preset == Preset::Broadcast ? "veryslow" : "medium";
        const char* crf = config.quality == Quality::Fast ? "24" :
                          config.quality == Quality::High ? "17" : "20";
        av_opt_set(encoder_->priv_data, "preset", encoderPreset, 0);
        av_opt_set(encoder_->priv_data, "crf", crf, 0);
    }
    checkFfmpeg(avcodec_open2(encoder_.get(), codec, nullptr), "open video encoder");
    checkFfmpeg(avcodec_parameters_from_context(videoStream->codecpar, encoder_.get()),
                "copy output video parameters");
    videoStream->time_base = encoder_->time_base;
    videoStream->avg_frame_rate = encoder_->framerate;
    av_dict_copy(&videoStream->metadata,
                 sourceFormat->streams[sourceInfo.streamIndex]->metadata, 0);

    for (const int sourceIndex : sourceInfo.audioStreamIndices) {
        const AVStream* sourceStream = sourceFormat->streams[sourceIndex];
        if (avformat_query_codec(format_->oformat, sourceStream->codecpar->codec_id,
                                 FF_COMPLIANCE_NORMAL) == 0) {
            log(LogLevel::Warning, "output container cannot carry an input audio codec; that audio stream will be omitted");
            continue;
        }
        AVStream* outputStream = avformat_new_stream(format_.get(), nullptr);
        if (!outputStream) throw std::runtime_error("create output audio stream");
        checkFfmpeg(avcodec_parameters_copy(outputStream->codecpar, sourceStream->codecpar),
                    "copy audio stream parameters");
        outputStream->codecpar->codec_tag = 0;
        outputStream->time_base = sourceStream->time_base;
        av_dict_copy(&outputStream->metadata, sourceStream->metadata, 0);
        audioStreamMap_.emplace(sourceIndex, outputStream->index);
        audioSourceTimeBases_.emplace(sourceIndex, sourceStream->time_base);
    }

    if (!(format_->oformat->flags & AVFMT_NOFILE)) {
        checkFfmpeg(avio_open(&format_->pb, output.c_str(), AVIO_FLAG_WRITE), "open output file");
    }
    checkFfmpeg(avformat_write_header(format_.get(), nullptr), "write output container header");

    encodeFrame_.reset(av_frame_alloc());
    encodedPacket_.reset(av_packet_alloc());
    if (!encodeFrame_ || !encodedPacket_) throw std::runtime_error("allocate encoder frame or packet");
    encodeFrame_->format = encoder_->pix_fmt;
    encodeFrame_->width = encoder_->width;
    encodeFrame_->height = encoder_->height;
    encodeFrame_->sample_aspect_ratio = encoder_->sample_aspect_ratio;
    checkFfmpeg(av_frame_get_buffer(encodeFrame_.get(), 32), "allocate encoder frame buffer");
}

VideoWriter::~VideoWriter() = default;

void VideoWriter::writeAudioPacket(const AVPacket& sourcePacket, int sourceStreamIndex) {
    const auto mapped = audioStreamMap_.find(sourceStreamIndex);
    if (mapped == audioStreamMap_.end()) return;
    PacketPtr packet(av_packet_clone(&sourcePacket));
    if (!packet) throw std::runtime_error("copy audio packet for muxing");
    const auto timeBase = audioSourceTimeBases_.at(sourceStreamIndex);
    av_packet_rescale_ts(packet.get(), timeBase, format_->streams[mapped->second]->time_base);
    packet->stream_index = mapped->second;
    packet->pos = -1;
    checkFfmpeg(av_interleaved_write_frame(format_.get(), packet.get()), "mux copied audio packet");
}

void VideoWriter::writeVideoFrame(const VideoFrame& frame) {
    if (finished_) throw std::runtime_error("cannot write a frame after output was finalized");
    if (frame.bgr.empty() || frame.bgr.cols != encoder_->width || frame.bgr.rows != encoder_->height ||
        frame.bgr.type() != CV_8UC3) {
        throw std::runtime_error("output frame dimensions or pixel format changed unexpectedly");
    }
    checkFfmpeg(av_frame_make_writable(encodeFrame_.get()), "make encoder frame writable");
    converter_.reset(sws_getCachedContext(converter_.release(), frame.bgr.cols, frame.bgr.rows,
        AV_PIX_FMT_BGR24, encoder_->width, encoder_->height, encoder_->pix_fmt,
        SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!converter_) throw std::runtime_error("create output pixel-format converter");
    const std::uint8_t* sourceData[] = { frame.bgr.data, nullptr, nullptr, nullptr };
    const int sourceLines[] = { static_cast<int>(frame.bgr.step), 0, 0, 0 };
    const int scaled = sws_scale(converter_.get(), sourceData, sourceLines, 0, frame.bgr.rows,
                                 encodeFrame_->data, encodeFrame_->linesize);
    if (scaled != encoder_->height) throw std::runtime_error("convert frame for video encoder");
    encodeFrame_->pts = frame.pts;
    encodeFrame_->pict_type = AV_PICTURE_TYPE_NONE;
    checkFfmpeg(avcodec_send_frame(encoder_.get(), encodeFrame_.get()), "send frame to video encoder");
    drainEncoder(false);
}

void VideoWriter::drainEncoder(bool flushing) {
    if (flushing) checkFfmpeg(avcodec_send_frame(encoder_.get(), nullptr), "flush video encoder");
    while (true) {
        const int result = avcodec_receive_packet(encoder_.get(), encodedPacket_.get());
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
        checkFfmpeg(result, "receive encoded video packet");
        av_packet_rescale_ts(encodedPacket_.get(), encoder_->time_base,
                             format_->streams[videoStreamIndex_]->time_base);
        encodedPacket_->stream_index = videoStreamIndex_;
        encodedPacket_->pos = -1;
        checkFfmpeg(av_interleaved_write_frame(format_.get(), encodedPacket_.get()), "write encoded video packet");
        av_packet_unref(encodedPacket_.get());
    }
}

void VideoWriter::finish() {
    if (finished_) return;
    drainEncoder(true);
    checkFfmpeg(av_write_trailer(format_.get()), "write output container trailer");
    finished_ = true;
}

} // namespace subtitle_remover