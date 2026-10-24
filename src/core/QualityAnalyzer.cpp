#include "core/QualityAnalyzer.h"

#include "core/FrameSource.h"
#include "core/SerFile.h"
#include "core/ObjectDetector.h"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace seeingforge {
namespace {

int luminanceAt(const QImage &image, int x, int y)
{
    if (image.format() == QImage::Format_Grayscale8)
        return image.constScanLine(y)[x];
    return qGray(image.pixel(x, y));
}

double percentile(QVector<double> values, double fraction)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const qsizetype index = static_cast<qsizetype>(std::clamp(
        fraction * static_cast<double>(values.size() - 1), 0.0,
        static_cast<double>(values.size() - 1)));
    return values[index];
}

double normalize(double value, double low, double high)
{
    if (high <= low) return 0.5;
    return std::clamp((value - low) / (high - low), 0.0, 1.0);
}

} // namespace

FrameQuality QualityAnalyzer::measureFrame(const QImage &frame,
                                           std::uint32_t frameIndex,
                                           int backgroundThreshold)
{
    FrameQuality result;
    result.frameIndex = frameIndex;
    if (frame.isNull() || frame.width() < 5 || frame.height() < 5) return result;

    const DetectionResult detection = ObjectDetector::detect(
        frame, backgroundThreshold, 10);
    const QRect region = detection.found() ? detection.cropRect : frame.rect();
    const int samplingStep = std::max(1, std::max(region.width(), region.height()) / 400);
    double sum = 0.0;
    double sumSquares = 0.0;
    double gradientEnergy = 0.0;
    std::uint64_t count = 0;
    std::uint64_t clipped = 0;
    const int left = std::max(2, region.left());
    const int right = std::min(frame.width() - 3, region.right());
    const int top = std::max(2, region.top());
    const int bottom = std::min(frame.height() - 3, region.bottom());
    for (int y = top; y <= bottom; y += samplingStep) {
        for (int x = left; x <= right; x += samplingStep) {
            const int value = luminanceAt(frame, x, y);
            if (value <= backgroundThreshold) continue;
            const int fineX = luminanceAt(frame, x + 1, y)
                              - luminanceAt(frame, x - 1, y);
            const int fineY = luminanceAt(frame, x, y + 1)
                              - luminanceAt(frame, x, y - 1);
            const int broadX = luminanceAt(frame, x + 2, y)
                               - luminanceAt(frame, x - 2, y);
            const int broadY = luminanceAt(frame, x, y + 2)
                               - luminanceAt(frame, x, y - 2);
            gradientEnergy += static_cast<double>(2 * fineX * fineX + 2 * fineY * fineY
                                                   + broadX * broadX + broadY * broadY);
            sum += value;
            sumSquares += static_cast<double>(value * value);
            if (value >= 250) ++clipped;
            ++count;
        }
    }
    if (count == 0) return result;
    result.meanBrightness = sum / static_cast<double>(count);
    const double deviation = std::sqrt(std::max(
        0.0, sumSquares / static_cast<double>(count)
                 - result.meanBrightness * result.meanBrightness));
    result.contrast = deviation / std::max(result.meanBrightness, 1.0);
    result.sharpness = gradientEnergy / static_cast<double>(count)
                       / std::max(result.meanBrightness * result.meanBrightness, 1.0);
    result.clippedPercent = 100.0 * static_cast<double>(clipped)
                            / static_cast<double>(count);
    return result;
}

QualityResult QualityAnalyzer::analyze(
    const FrameSource &file, int backgroundThreshold,
    const std::function<void(int, int)> &progress,
    const std::function<bool()> &shouldCancel)
{
    QualityResult result;
    if (!file.isOpen()) return result;
    result.frames.reserve(static_cast<qsizetype>(file.header().frameCount));
    for (std::uint32_t index = 0; index < file.header().frameCount; ++index) {
        if (shouldCancel && shouldCancel()) {
            result.canceled = true;
            return result;
        }
        const QImage frame = file.readFrame(index);
        if (!frame.isNull())
            result.frames.append(measureFrame(frame, index, backgroundThreshold));
        if (progress) progress(static_cast<int>(index + 1U),
                               static_cast<int>(file.header().frameCount));
    }
    if (result.frames.isEmpty()) return result;

    QVector<double> brightness;
    brightness.reserve(result.frames.size());
    for (const auto &frame : result.frames) {
        brightness.append(frame.meanBrightness);
    }
    const double medianBrightness = percentile(brightness, 0.50);

    QVector<double> scoreByIndex(static_cast<qsizetype>(file.header().frameCount), 0.0);
    QVector<double> rawScores;
    rawScores.reserve(result.frames.size());
    for (auto &frame : result.frames) {
        const double exposureError = medianBrightness > 0.0
            ? std::abs(frame.meanBrightness - medianBrightness) / medianBrightness : 0.0;
        const double exposureConsistency = std::clamp(1.0 - exposureError * 4.0, 0.0, 1.0);
        const double clippingPenalty = std::clamp(frame.clippedPercent / 10.0, 0.0, 1.0);
        frame.score = std::log1p(frame.sharpness * 1000.0)
                      + 0.20 * std::log1p(frame.contrast * 10.0)
                      + 0.05 * exposureConsistency - 0.15 * clippingPenalty;
        rawScores.append(frame.score);
    }
    const double scoreLow = percentile(rawScores, 0.02);
    const double scoreHigh = percentile(rawScores, 0.98);
    for (auto &frame : result.frames) {
        frame.score = 100.0 * normalize(frame.score, scoreLow, scoreHigh);
        result.rankedFrameIndices.append(frame.frameIndex);
        scoreByIndex[static_cast<qsizetype>(frame.frameIndex)] = frame.score;
    }
    std::sort(result.rankedFrameIndices.begin(), result.rankedFrameIndices.end(),
              [&scoreByIndex](std::uint32_t left, std::uint32_t right) {
                  return scoreByIndex[static_cast<qsizetype>(left)]
                         > scoreByIndex[static_cast<qsizetype>(right)];
              });
    return result;
}

} // namespace seeingforge
