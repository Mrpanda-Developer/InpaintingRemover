#include "subtitle_remover/processing_interfaces.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include <algorithm>
#include <stdexcept>

namespace subtitle_remover {

std::vector<SubtitleRegion> OpenCvSubtitleDetector::detect(const cv::Mat& frame) {
    if (frame.empty()) return {};
    int regionHeight = frame.rows;
    switch (region_) {
    case SubtitleRegion::Full: break;
    case SubtitleRegion::Bottom20: regionHeight = frame.rows / 5; break;
    case SubtitleRegion::Bottom30: regionHeight = frame.rows * 30 / 100; break;
    case SubtitleRegion::Bottom35: regionHeight = frame.rows * 35 / 100; break;
    case SubtitleRegion::Bottom40: regionHeight = frame.rows * 40 / 100; break;
    }
    const cv::Rect roi(0, frame.rows - regionHeight, frame.cols, regionHeight);
    cv::Mat gray;
    cv::cvtColor(frame(roi), gray, cv::COLOR_BGR2GRAY);
    cv::Mat edges;
    cv::Canny(gray, edges, 90.0, 180.0);
    cv::Mat horizontal;
    const int kernelWidth = std::max(9, frame.cols / 24);
    cv::morphologyEx(edges, horizontal, cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_RECT, cv::Size(kernelWidth, 3)));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(horizontal, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    std::vector<SubtitleRegion> regions;
    for (const auto& contour : contours) {
        const cv::Rect bounds = cv::boundingRect(contour);
        if (bounds.width < frame.cols / 20 || bounds.height < 3 ||
            bounds.height > std::max(30, frame.rows / 8)) continue;
        regions.push_back({cv::Rect(bounds.x, bounds.y + roi.y, bounds.width, bounds.height), 0.5F});
    }
    return regions;
}

void OpenCvOpticalFlow::estimate(const cv::Mat& previous, const cv::Mat& current, cv::Mat& flow) {
    if (previous.size() != current.size() || previous.empty() || current.empty()) {
        throw std::invalid_argument("optical-flow frames must be non-empty and have equal dimensions");
    }
    cv::Mat previousGray;
    cv::Mat currentGray;
    if (previous.channels() == 1) previousGray = previous;
    else cv::cvtColor(previous, previousGray, cv::COLOR_BGR2GRAY);
    if (current.channels() == 1) currentGray = current;
    else cv::cvtColor(current, currentGray, cv::COLOR_BGR2GRAY);
    cv::calcOpticalFlowFarneback(previousGray, currentGray, flow, 0.5, 3, 15, 3, 5, 1.2, 0);
}

void OpenCvInpainter::inpaint(cv::Mat& frame, const cv::Mat& mask) {
    if (frame.empty() || mask.empty() || frame.size() != mask.size() || mask.channels() != 1) {
        throw std::invalid_argument("inpainting frame and mask must be non-empty and have equal dimensions");
    }
    cv::Mat binaryMask;
    if (mask.type() == CV_8UC1) binaryMask = mask;
    else mask.convertTo(binaryMask, CV_8UC1);
    cv::Mat repaired;
    cv::inpaint(frame, binaryMask, 3.0, cv::INPAINT_TELEA, repaired);
    repaired.copyTo(frame, binaryMask);
}

OnnxInpainter::OnnxInpainter(const std::filesystem::path& modelPath, Device device) {
    (void)modelPath;
    (void)device;
    throw std::runtime_error("ONNX inpainter is an interface placeholder; no commercially licensed model is bundled");
}

void OnnxInpainter::inpaint(cv::Mat& frame, const cv::Mat& mask) {
    (void)frame;
    (void)mask;
    throw std::runtime_error("ONNX inpainter is not configured");
}

} // namespace subtitle_remover