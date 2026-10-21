#include "core/QualityAnalyzer.h"

#include "core/SerFile.h"

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

    constexpr int samplingStep = 4;
    double sum = 0.0;
    double sumSquares = 0.0;
    double gradientEnergy = 0.0;
    std::uint64_t count = 0;
    std::uint64_t clipped = 0;
    for (int y = 2; y < frame.height() - 2; y += samplingStep) {
        for (int x = 2; x < frame.width() - 2; x += samplingStep) {
            const int value = luminanceAt(frame, x, y);
            if (value <= backgroundThreshold) continue;
            const int gradientX = luminanceAt(frame, x + 2, y)
                                  - luminanceAt(frame, x - 2, y);
            const int gradientY = luminanceAt(frame, x, y + 2)
                                  - luminanceAt(frame, x, y - 2);
            gradientEnergy += static_cast<double>(gradientX * gradientX
                                                   + gradientY * gradientY);
            sum += value;
            sumSquares += static_cast<double>(value * value);
            if (value >= 250) ++clipped;
            ++count;
        }
    }
    if (count == 0) return result;
    result.meanBrightness = sum / static_cast<double>(count);
    result.contrast = std::sqrt(std::max(
        0.0, sumSquares / static_cast<double>(count)
                 - result.meanBrightness * result.meanBrightness));
    result.sharpness = gradientEnergy / static_cast<double>(count);
    result.clippedPercent = 100.0 * static_cast<double>(clipped)
                            / static_cast<double>(count);
    return result;
}

QualityResult QualityAnalyzer::analyze(
    const SerFile &file, int backgroundThreshold,
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

    QVector<double> sharpness;
    QVector<double> contrast;
    QVector<double> brightness;
    sharpness.reserve(result.frames.size());
    contrast.reserve(result.frames.size());
    brightness.reserve(result.frames.size());
    for (const auto &frame : result.frames) {
        sharpness.append(frame.sharpness);
        contrast.append(frame.contrast);
        brightness.append(frame.meanBrightness);
    }
    const double sharpLow = percentile(sharpness, 0.10);
    const double sharpHigh = percentile(sharpness, 0.90);
    const double contrastLow = percentile(contrast, 0.10);
    const double contrastHigh = percentile(contrast, 0.90);
    const double medianBrightness = percentile(brightness, 0.50);

    QVector<double> scoreByIndex(static_cast<qsizetype>(file.header().frameCount), 0.0);
    for (auto &frame : result.frames) {
        const double detail = normalize(frame.sharpness, sharpLow, sharpHigh);
        const double localContrast = normalize(frame.contrast, contrastLow, contrastHigh);
        const double exposureError = medianBrightness > 0.0
            ? std::abs(frame.meanBrightness - medianBrightness) / medianBrightness : 0.0;
        const double exposureConsistency = std::clamp(1.0 - exposureError * 4.0, 0.0, 1.0);
        const double clippingPenalty = std::clamp(frame.clippedPercent / 10.0, 0.0, 1.0);
        frame.score = 100.0 * std::clamp(0.75 * detail + 0.15 * localContrast
                                         + 0.10 * exposureConsistency
                                         - 0.15 * clippingPenalty, 0.0, 1.0);
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
