#include "core/PreviewProcessor.h"

#include <QColor>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>

namespace seeingforge {
namespace {

enum class Channel { Red, Green, Blue };

void applyWhiteBalance(QImage &image, double redAdjustment, double blueAdjustment)
{
    int brightest = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const int byte = x * 3;
            brightest = std::max(brightest, std::max({static_cast<int>(line[byte]),
                                                       static_cast<int>(line[byte + 1]),
                                                       static_cast<int>(line[byte + 2])}));
        }
    }
    // Planetary targets often occupy much less than one percent of the frame.
    // Measuring the near-black sky makes its colour noise drive the white balance.
    const int foregroundFloor = std::max(8, brightest / 8);
    std::uint64_t redSum = 0, greenSum = 0, blueSum = 0, count = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const int byte = x * 3;
            const int red = line[byte];
            const int green = line[byte + 1];
            const int blue = line[byte + 2];
            if (std::max({red, green, blue}) < foregroundFloor) continue;
            redSum += static_cast<std::uint64_t>(red);
            greenSum += static_cast<std::uint64_t>(green);
            blueSum += static_cast<std::uint64_t>(blue);
            ++count;
        }
    }
    if (count == 0 || redSum == 0 || blueSum == 0) return;
    const double redGain = std::clamp(static_cast<double>(greenSum)
                                          / static_cast<double>(redSum), 0.25, 4.0)
                           * redAdjustment;
    const double blueGain = std::clamp(static_cast<double>(greenSum)
                                           / static_cast<double>(blueSum), 0.25, 4.0)
                            * blueAdjustment;
    for (int y = 0; y < image.height(); ++y) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const int byte = x * 3;
            line[byte] = static_cast<uchar>(std::clamp(
                static_cast<int>(std::lround(line[byte] * redGain)), 0, 255));
            line[byte + 2] = static_cast<uchar>(std::clamp(
                static_cast<int>(std::lround(line[byte + 2] * blueGain)), 0, 255));
        }
    }
}

void applyColorAdjustment(QImage &image, double saturationScale, int hueShiftDegrees)
{
    if (image.format() != QImage::Format_RGB888
        || (qFuzzyCompare(saturationScale, 1.0) && hueShiftDegrees == 0)) return;
    for (int y = 0; y < image.height(); ++y) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const int byte = x * 3;
            QColor color(line[byte], line[byte + 1], line[byte + 2]);
            int hue = 0, saturation = 0, value = 0;
            color.getHsv(&hue, &saturation, &value);
            saturation = std::clamp(
                static_cast<int>(std::lround(saturation * saturationScale)), 0, 255);
            if (hue >= 0) hue = (hue + hueShiftDegrees % 360 + 360) % 360;
            color.setHsv(hue, saturation, value);
            line[byte] = static_cast<uchar>(color.red());
            line[byte + 1] = static_cast<uchar>(color.green());
            line[byte + 2] = static_cast<uchar>(color.blue());
        }
    }
}

void applyToneAdjustment(QImage &image, double gain, double gamma)
{
    if (qFuzzyCompare(gain, 1.0) && qFuzzyCompare(gamma, 1.0)) return;
    std::array<uchar, 256> lookup{};
    const double inverseGamma = 1.0 / std::max(gamma, 0.01);
    for (int value = 0; value < 256; ++value) {
        const double normalized = std::clamp(value * gain / 255.0, 0.0, 1.0);
        lookup[static_cast<std::size_t>(value)] = static_cast<uchar>(
            std::lround(255.0 * std::pow(normalized, inverseGamma)));
    }
    const bool rgb = image.format() == QImage::Format_RGB888;
    for (int y = 0; y < image.height(); ++y) {
        uchar *line = image.scanLine(y);
        const int count = image.width() * (rgb ? 3 : 1);
        for (int index = 0; index < count; ++index)
            line[index] = lookup[static_cast<std::size_t>(line[index])];
    }
}

QImage orient(QImage image, const PreviewSettings &settings)
{
    QTransform transform;
    transform.rotate(90.0 * (settings.rotationQuarterTurns & 3));
    image = image.transformed(transform, Qt::FastTransformation);
    if (settings.flipHorizontal || settings.flipVertical) {
        QTransform flip;
        flip.scale(settings.flipHorizontal ? -1.0 : 1.0,
                   settings.flipVertical ? -1.0 : 1.0);
        image = image.transformed(flip, Qt::FastTransformation);
    }
    return image;
}

Channel channelAt(int x, int y, std::uint32_t colorId)
{
    const bool evenX = (x & 1) == 0;
    const bool evenY = (y & 1) == 0;
    switch (colorId) {
    case 8:
        if (evenY && evenX) return Channel::Red;
        if (!evenY && !evenX) return Channel::Blue;
        return Channel::Green;
    case 9:
        if (evenY && !evenX) return Channel::Red;
        if (!evenY && evenX) return Channel::Blue;
        return Channel::Green;
    case 10:
        if (!evenY && evenX) return Channel::Red;
        if (evenY && !evenX) return Channel::Blue;
        return Channel::Green;
    case 11:
        if (!evenY && !evenX) return Channel::Red;
        if (evenY && evenX) return Channel::Blue;
        return Channel::Green;
    default:
        return Channel::Green;
    }
}

int interpolate(const QImage &raw, int x, int y, Channel wanted, std::uint32_t colorId)
{
    if (channelAt(x, y, colorId) == wanted) return raw.constScanLine(y)[x];
    int sum = 0;
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        const int sampleY = std::clamp(y + dy, 0, raw.height() - 1);
        const uchar *line = raw.constScanLine(sampleY);
        for (int dx = -1; dx <= 1; ++dx) {
            const int sampleX = std::clamp(x + dx, 0, raw.width() - 1);
            if (channelAt(sampleX, sampleY, colorId) == wanted) {
                sum += line[sampleX];
                ++count;
            }
        }
    }
    return count > 0 ? sum / count : raw.constScanLine(y)[x];
}

QImage debayer(const QImage &raw, std::uint32_t colorId,
               bool whiteBalance, double redBalance, double blueBalance)
{
    QImage output(raw.size(), QImage::Format_RGB888);
    for (int y = 0; y < raw.height(); ++y) {
        uchar *destination = output.scanLine(y);
        for (int x = 0; x < raw.width(); ++x) {
            int red = interpolate(raw, x, y, Channel::Red, colorId);
            int green = interpolate(raw, x, y, Channel::Green, colorId);
            int blue = interpolate(raw, x, y, Channel::Blue, colorId);
            const int byte = x * 3;
            destination[byte] = static_cast<uchar>(std::clamp(red, 0, 255));
            destination[byte + 1] = static_cast<uchar>(std::clamp(green, 0, 255));
            destination[byte + 2] = static_cast<uchar>(std::clamp(blue, 0, 255));
        }
    }
    if (whiteBalance) applyWhiteBalance(output, redBalance, blueBalance);
    return output;
}

QImage debayerBinned(const QImage &raw, std::uint32_t colorId, int factor,
                     bool whiteBalance, double redBalance, double blueBalance)
{
    factor = std::max(2, factor);
    if ((factor & 1) != 0) --factor;
    const int outputWidth = raw.width() / factor;
    const int outputHeight = raw.height() / factor;
    QImage output(outputWidth, outputHeight, QImage::Format_RGB888);
    for (int outputY = 0; outputY < outputHeight; ++outputY) {
        uchar *destination = output.scanLine(outputY);
        const int sourceY = outputY * factor;
        for (int outputX = 0; outputX < outputWidth; ++outputX) {
            const int sourceX = outputX * factor;
            int red = 0, green = 0, blue = 0;
            int redCount = 0, greenCount = 0, blueCount = 0;
            for (int y = 0; y < factor; ++y) {
                const uchar *line = raw.constScanLine(sourceY + y);
                for (int x = 0; x < factor; ++x) {
                    const int value = line[sourceX + x];
                    switch (channelAt(sourceX + x, sourceY + y, colorId)) {
                    case Channel::Red: red += value; ++redCount; break;
                    case Channel::Green: green += value; ++greenCount; break;
                    case Channel::Blue: blue += value; ++blueCount; break;
                    }
                }
            }
            red /= std::max(redCount, 1);
            green /= std::max(greenCount, 1);
            blue /= std::max(blueCount, 1);
            const int byte = outputX * 3;
            destination[byte] = static_cast<uchar>(std::clamp(red, 0, 255));
            destination[byte + 1] = static_cast<uchar>(std::clamp(green, 0, 255));
            destination[byte + 2] = static_cast<uchar>(std::clamp(blue, 0, 255));
        }
    }
    if (whiteBalance) applyWhiteBalance(output, redBalance, blueBalance);
    return output;
}

QImage stretch(const QImage &source)
{
    std::array<std::uint64_t, 256> histogram{};
    const bool rgb = source.format() == QImage::Format_RGB888;
    std::uint64_t sampleCount = 0;
    for (int y = 0; y < source.height(); ++y) {
        const uchar *line = source.constScanLine(y);
        for (int x = 0; x < source.width(); ++x) {
            int value = line[x];
            if (rgb) {
                const int byte = x * 3;
                // Stretch colour from luminance so a biased background channel
                // cannot determine the limits or alter the colour balance.
                value = (static_cast<int>(line[byte])
                         + 2 * static_cast<int>(line[byte + 1])
                         + static_cast<int>(line[byte + 2]) + 2) / 4;
            }
            ++histogram[static_cast<std::size_t>(value)];
            ++sampleCount;
        }
    }
    const std::uint64_t lowTarget = sampleCount / 200U;
    // Ignore only the brightest 0.01%. A 99.5th-percentile upper limit clips
    // compact planets whenever they cover less than 0.5% of a large sensor.
    const std::uint64_t highTail = std::max<std::uint64_t>(1, sampleCount / 10000U);
    const std::uint64_t highTarget = sampleCount - highTail;
    std::uint64_t cumulative = 0;
    int low = 0;
    int high = 255;
    for (int value = 0; value < 256; ++value) {
        cumulative += histogram[static_cast<std::size_t>(value)];
        if (cumulative >= lowTarget) { low = value; break; }
    }
    cumulative = 0;
    for (int value = 0; value < 256; ++value) {
        cumulative += histogram[static_cast<std::size_t>(value)];
        if (cumulative >= highTarget) { high = value; break; }
    }
    if (high <= low + 1) return source;

    QImage output = source.copy();
    const double scale = 255.0 / static_cast<double>(high - low);
    for (int y = 0; y < output.height(); ++y) {
        uchar *line = output.scanLine(y);
        const int count = output.width() * (rgb ? 3 : 1);
        for (int index = 0; index < count; ++index) {
            const int value = static_cast<int>((static_cast<int>(line[index]) - low) * scale);
            line[index] = static_cast<uchar>(std::clamp(value, 0, 255));
        }
    }
    return output;
}

QImage rawLuminance(const QImage &raw)
{
    const int width = raw.width() / 2;
    const int height = raw.height() / 2;
    QImage output(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        const uchar *top = raw.constScanLine(y * 2);
        const uchar *bottom = raw.constScanLine(y * 2 + 1);
        uchar *destination = output.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int sourceX = x * 2;
            destination[x] = static_cast<uchar>((top[sourceX] + top[sourceX + 1]
                                                 + bottom[sourceX] + bottom[sourceX + 1] + 2) / 4);
        }
    }
    return output;
}

} // namespace

PreviewImages PreviewProcessor::render(const QImage &rawFrame,
                                       std::uint32_t colorId,
                                       const PreviewSettings &settings)
{
    QImage previewSource = rawFrame;
    if (settings.maxPreviewDimension > 0
        && std::max(rawFrame.width(), rawFrame.height()) > settings.maxPreviewDimension) {
        previewSource = rawFrame.scaled(settings.maxPreviewDimension,
                                        settings.maxPreviewDimension,
                                        Qt::KeepAspectRatio,
                                        Qt::FastTransformation);
    }
    PreviewImages result;
    result.rawMosaic = settings.autoStretch ? stretch(rawFrame) : rawFrame;
    result.rawLuminance = rawLuminance(rawFrame);
    if (settings.autoStretch) result.rawLuminance = stretch(result.rawLuminance);
    if (settings.debayer && supportsDebayer(colorId)) {
        const int largestDimension = std::max(rawFrame.width(), rawFrame.height());
        const int requiredFactor = settings.maxPreviewDimension > 0
                && largestDimension > settings.maxPreviewDimension
            ? std::max(2, largestDimension / settings.maxPreviewDimension)
            : 0;
        if (requiredFactor >= 2) {
            result.processed = debayerBinned(rawFrame, colorId, requiredFactor,
                                              settings.whiteBalance,
                                              settings.redBalance,
                                              settings.blueBalance);
        } else {
            result.processed = debayer(rawFrame, colorId, settings.whiteBalance,
                                       settings.redBalance, settings.blueBalance);
        }
    } else {
        result.processed = supportsDebayer(colorId) ? result.rawLuminance : previewSource;
    }
    if (settings.autoStretch) result.processed = stretch(result.processed);
    applyColorAdjustment(result.processed, settings.saturation,
                         settings.hueShiftDegrees);
    applyToneAdjustment(result.processed, settings.gain, settings.gamma);
    result.processed = orient(std::move(result.processed), settings);
    result.rawLuminance = orient(std::move(result.rawLuminance), settings);
    result.rawMosaic = orient(std::move(result.rawMosaic), settings);
    return result;
}

bool PreviewProcessor::supportsDebayer(std::uint32_t colorId)
{
    return colorId >= 8 && colorId <= 11;
}

} // namespace seeingforge
