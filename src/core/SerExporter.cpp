#include "core/SerExporter.h"

#include "core/FrameSource.h"
#include "core/SerFile.h"
#include "core/ObjectDetector.h"

#include <QFile>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace seeingforge {
namespace {

quint16 readSample(const char *bytes, int bytesPerSample, bool bigEndian)
{
    if (bytesPerSample == 1) return static_cast<uchar>(bytes[0]);
    const auto *raw = reinterpret_cast<const uchar *>(bytes);
    return bigEndian ? qFromBigEndian<quint16>(raw) : qFromLittleEndian<quint16>(raw);
}

void writeSample(char *bytes, quint16 value, int bytesPerSample, bool bigEndian)
{
    if (bytesPerSample == 1) {
        bytes[0] = static_cast<char>(value);
        return;
    }
    auto *raw = reinterpret_cast<uchar *>(bytes);
    if (bigEndian) qToBigEndian(value, raw);
    else qToLittleEndian(value, raw);
}

void writeText(QByteArray &header, int offset, const QString &value)
{
    const QByteArray text = value.toLatin1().left(39);
    std::memcpy(header.data() + offset, text.constData(), static_cast<std::size_t>(text.size()));
}

QByteArray orientFrame(const QByteArray &source, int width, int height, int bytesPerPixel,
                       int quarterTurns, bool flipHorizontal, bool flipVertical)
{
    const int turns = quarterTurns & 3;
    const int outputWidth = (turns & 1) != 0 ? height : width;
    const int outputHeight = (turns & 1) != 0 ? width : height;
    QByteArray output(static_cast<qsizetype>(outputWidth) * outputHeight * bytesPerPixel, '\0');
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int destinationX = x;
            int destinationY = y;
            if (turns == 1) { destinationX = height - 1 - y; destinationY = x; }
            else if (turns == 2) { destinationX = width - 1 - x; destinationY = height - 1 - y; }
            else if (turns == 3) { destinationX = y; destinationY = width - 1 - x; }
            if (flipHorizontal) destinationX = outputWidth - 1 - destinationX;
            if (flipVertical) destinationY = outputHeight - 1 - destinationY;
            std::memcpy(output.data()
                            + (static_cast<qsizetype>(destinationY) * outputWidth + destinationX)
                                  * bytesPerPixel,
                        source.constData()
                            + (static_cast<qsizetype>(y) * width + x) * bytesPerPixel,
                        static_cast<std::size_t>(bytesPerPixel));
        }
    }
    return output;
}

quint32 orientedBayerColorId(quint32 colorId, int quarterTurns,
                             bool flipHorizontal, bool flipVertical)
{
    if (!((colorId >= 8 && colorId <= 11) || (colorId >= 16 && colorId <= 19)))
        return colorId;
    const quint32 base = colorId >= 16 ? 16U : 8U;
    const quint32 pattern = colorId - base;
    int x = (pattern & 1U) != 0 ? 1 : 0;
    int y = (pattern & 2U) != 0 ? 1 : 0;
    const int turns = quarterTurns & 3;
    if (turns == 1) { const int oldX = x; x = 1 - y; y = oldX; }
    else if (turns == 2) { x = 1 - x; y = 1 - y; }
    else if (turns == 3) { const int oldX = x; x = y; y = 1 - oldX; }
    if (flipHorizontal) x = 1 - x;
    if (flipVertical) y = 1 - y;
    return base + static_cast<quint32>(x + 2 * y);
}

int bayerChannel(int x, int y, quint32 colorId)
{
    const int pattern = static_cast<int>(colorId);
    const int redX = pattern == 9 || pattern == 11 ? 1 : 0;
    const int redY = pattern == 10 || pattern == 11 ? 1 : 0;
    if ((x & 1) == redX && (y & 1) == redY) return 0;
    if ((x & 1) != redX && (y & 1) != redY) return 2;
    return 1;
}

} // namespace

ExportResult SerExporter::exportFrames(
    const FrameSource &source, const QString &outputPath,
    const SerExportSettings &settings,
    const std::function<void(int, int)> &progress,
    const std::function<bool()> &shouldCancel)
{
    ExportResult result;
    result.outputPath = outputPath;
    if (!source.isOpen() || settings.frameIndices.isEmpty()) {
        result.error = QStringLiteral("There are no source frames to export.");
        return result;
    }
    for (const std::uint32_t index : settings.frameIndices) {
        if (shouldCancel && shouldCancel()) {
            result.canceled = true;
            return result;
        }
        if (index >= source.header().frameCount) {
            result.error = QStringLiteral("The export selection contains an invalid frame index.");
            return result;
        }
    }

    QByteArray header(static_cast<qsizetype>(SerFile::HeaderSize), '\0');
    const QByteArray identifier("LUCAM-RECORDER");
    std::memcpy(header.data(), identifier.constData(), static_cast<std::size_t>(identifier.size()));
    qToLittleEndian(source.header().cameraId, reinterpret_cast<uchar *>(header.data() + 14));
    qToLittleEndian(source.header().colorId, reinterpret_cast<uchar *>(header.data() + 18));
    qToLittleEndian(source.header().endianFlag, reinterpret_cast<uchar *>(header.data() + 22));
    qToLittleEndian(source.header().width, reinterpret_cast<uchar *>(header.data() + 26));
    qToLittleEndian(source.header().height, reinterpret_cast<uchar *>(header.data() + 30));
    qToLittleEndian(source.header().pixelDepth, reinterpret_cast<uchar *>(header.data() + 34));
    writeText(header, 42, source.header().observer);
    writeText(header, 82, source.header().instrument);
    writeText(header, 122, source.header().telescope);
    qToLittleEndian(source.header().dateTimeLocal, reinterpret_cast<uchar *>(header.data() + 162));
    qToLittleEndian(source.header().dateTimeUtc, reinterpret_cast<uchar *>(header.data() + 170));
    int outputWidth = settings.outputSize.isValid()
        ? settings.outputSize.width() : static_cast<int>(source.header().width);
    int outputHeight = settings.outputSize.isValid()
        ? settings.outputSize.height() : static_cast<int>(source.header().height);
    outputWidth = std::max(2, outputWidth - (outputWidth & 1));
    outputHeight = std::max(2, outputHeight - (outputHeight & 1));
    const std::uint32_t sourceColorId = settings.sourceColorId != 0
        ? settings.sourceColorId : source.header().colorId;
    const bool bayerSource = sourceColorId >= 8 && sourceColorId <= 19;
    const bool rgbSource = sourceColorId == 100 || sourceColorId == 101;
    const bool convertToMonochrome = settings.monochrome
                                     && (bayerSource || rgbSource);
    const bool binOutput = settings.monochrome && settings.binMonochrome;
    if (convertToMonochrome && bayerSource && !binOutput && sourceColorId > 11) {
        result.error = QStringLiteral("Full-resolution monochrome conversion currently supports RGB Bayer patterns only.");
        return result;
    }
    const int finalWidth = binOutput
        ? outputWidth / 2 : outputWidth;
    const int finalHeight = binOutput
        ? outputHeight / 2 : outputHeight;
    const bool rotateSides = (settings.rotationQuarterTurns & 1) != 0;
    const int orientedWidth = rotateSides ? finalHeight : finalWidth;
    const int orientedHeight = rotateSides ? finalWidth : finalHeight;
    if (settings.monochrome) {
        qToLittleEndian<quint32>(0, reinterpret_cast<uchar *>(header.data() + 18));
    } else if (bayerSource) {
        qToLittleEndian(orientedBayerColorId(sourceColorId,
                                             settings.rotationQuarterTurns,
                                             settings.flipHorizontal,
                                             settings.flipVertical),
                        reinterpret_cast<uchar *>(header.data() + 18));
    }
    qToLittleEndian(static_cast<quint32>(orientedWidth),
                    reinterpret_cast<uchar *>(header.data() + 26));
    qToLittleEndian(static_cast<quint32>(orientedHeight),
                    reinterpret_cast<uchar *>(header.data() + 30));
    qToLittleEndian(static_cast<quint32>(settings.frameIndices.size()),
                    reinterpret_cast<uchar *>(header.data() + 38));

    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) {
        result.error = output.errorString();
        return result;
    }
    if (output.write(header) != header.size()) {
        result.error = output.errorString();
        output.cancelWriting();
        return result;
    }

    const std::uint64_t frameBytes = source.header().bytesPerFrame();
    const int sourceWidth = static_cast<int>(source.header().width);
    const int sourceHeight = static_cast<int>(source.header().height);
    const int bytesPerSample = source.header().pixelDepth <= 8 ? 1 : 2;
    const int bytesPerPixel = source.header().planeCount() * bytesPerSample;
    const bool bigEndian = source.header().endianFlag != 0;
    const qsizetype outputFrameBytes = static_cast<qsizetype>(outputWidth)
        * outputHeight * bytesPerPixel;
    int completedFrames = 0;
    for (const std::uint32_t index : settings.frameIndices) {
        if (shouldCancel && shouldCancel()) {
            result.canceled = true;
            output.cancelWriting();
            return result;
        }
        QByteArray frame = source.readRawFrame(index);
        if (frame.size() != static_cast<qsizetype>(frameBytes)) {
            result.error = QStringLiteral("Could not copy source frame %1.").arg(index + 1U);
            output.cancelWriting();
            return result;
        }
        int originX = (sourceWidth - outputWidth) / 2;
        int originY = (sourceHeight - outputHeight) / 2;
        if (settings.centerObject) {
            const QImage decoded = source.decodeFrame(frame);
            const DetectionResult detection = ObjectDetector::detect(
                decoded, settings.detectionThreshold, 0);
            if (detection.found()) {
                originX = detection.objectRect.center().x() - outputWidth / 2;
                originY = detection.objectRect.center().y() - outputHeight / 2;
            }
        }
        if (bayerSource) {
            originX -= originX & 1;
            originY -= originY & 1;
        }
        const double intensityGain = std::clamp(settings.intensityGain, 1.0, 16.0);
        const bool applyChannelGains = bayerSource
            && settings.bayerChannelGains != std::array<double, 3>{1.0, 1.0, 1.0};
        if (intensityGain > 1.0 || applyChannelGains) {
            const std::uint32_t maximumSample = source.header().pixelDepth >= 16
                ? 65535U : (1U << source.header().pixelDepth) - 1U;
            const int planes = source.header().planeCount();
            for (int y = 0; y < sourceHeight; ++y) {
                for (int x = 0; x < sourceWidth; ++x) {
                    const double colorGain = applyChannelGains
                        ? settings.bayerChannelGains[static_cast<std::size_t>(
                              bayerChannel(x, y, sourceColorId))]
                        : 1.0;
                    for (int plane = 0; plane < planes; ++plane) {
                        const qsizetype sampleOffset =
                            ((static_cast<qsizetype>(y) * sourceWidth + x) * planes + plane)
                            * bytesPerSample;
                        const quint16 value = readSample(frame.constData() + sampleOffset,
                                                         bytesPerSample, bigEndian);
                        const quint16 adjusted = static_cast<quint16>(std::clamp(
                            static_cast<std::uint32_t>(std::lround(
                                value * intensityGain * colorGain)), 0U, maximumSample));
                        writeSample(frame.data() + sampleOffset, adjusted,
                                    bytesPerSample, bigEndian);
                    }
                }
            }
        }
        QByteArray centered(outputFrameBytes, '\0');
        const int sourceX = std::max(originX, 0);
        const int sourceY = std::max(originY, 0);
        const int destinationX = std::max(-originX, 0);
        const int destinationY = std::max(-originY, 0);
        const int copyWidth = std::min(sourceWidth - sourceX, outputWidth - destinationX);
        const int copyHeight = std::min(sourceHeight - sourceY, outputHeight - destinationY);
        if (copyWidth > 0 && copyHeight > 0) {
            const qsizetype sourceStride = static_cast<qsizetype>(sourceWidth) * bytesPerPixel;
            const qsizetype destinationStride = static_cast<qsizetype>(outputWidth) * bytesPerPixel;
            const qsizetype copyBytes = static_cast<qsizetype>(copyWidth) * bytesPerPixel;
            for (int row = 0; row < copyHeight; ++row) {
                const char *sourceLine = frame.constData()
                    + (static_cast<qsizetype>(sourceY + row) * sourceStride
                       + static_cast<qsizetype>(sourceX) * bytesPerPixel);
                char *destinationLine = centered.data()
                    + (static_cast<qsizetype>(destinationY + row) * destinationStride
                       + static_cast<qsizetype>(destinationX) * bytesPerPixel);
                std::memcpy(destinationLine, sourceLine, static_cast<std::size_t>(copyBytes));
            }
        }
        QByteArray exportFrame;
        if (convertToMonochrome && bayerSource && binOutput) {
            exportFrame.resize(static_cast<qsizetype>(finalWidth)
                               * finalHeight * bytesPerSample);
            for (int y = 0; y < finalHeight; ++y) {
                for (int x = 0; x < finalWidth; ++x) {
                    std::uint32_t sum = 0;
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const qsizetype sourceOffset =
                                (static_cast<qsizetype>(y * 2 + dy) * outputWidth
                                 + x * 2 + dx) * bytesPerSample;
                            sum += readSample(centered.constData() + sourceOffset,
                                              bytesPerSample, bigEndian);
                        }
                    }
                    const quint16 luminance = static_cast<quint16>((sum + 2U) / 4U);
                    const qsizetype destinationOffset =
                        (static_cast<qsizetype>(y) * finalWidth + x) * bytesPerSample;
                    writeSample(exportFrame.data() + destinationOffset, luminance,
                                bytesPerSample, bigEndian);
                }
            }
        } else if (convertToMonochrome) {
            exportFrame.resize(static_cast<qsizetype>(outputWidth)
                               * outputHeight * bytesPerSample);
            const bool bgr = sourceColorId == 101;
            auto channelAt = [&](int x, int y) {
                const int pattern = static_cast<int>(sourceColorId);
                const int redX = pattern == 9 || pattern == 11 ? 1 : 0;
                const int redY = pattern == 10 || pattern == 11 ? 1 : 0;
                if ((x & 1) == redX && (y & 1) == redY) return 0;
                if ((x & 1) != redX && (y & 1) != redY) return 2;
                return 1;
            };
            auto interpolate = [&](int x, int y, int channel) -> std::uint32_t {
                std::uint32_t sum = 0, count = 0;
                const bool native = channelAt(x, y) == channel;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = x + dx, sy = y + dy;
                        if (sx < 0 || sy < 0 || sx >= outputWidth || sy >= outputHeight
                            || channelAt(sx, sy) != channel
                            || (native && (dx != 0 || dy != 0))) continue;
                        const qsizetype offset = (static_cast<qsizetype>(sy) * outputWidth + sx) * bytesPerSample;
                        sum += readSample(centered.constData() + offset, bytesPerSample, bigEndian);
                        ++count;
                    }
                }
                return count ? (sum + count / 2) / count : 0;
            };
            for (int y = 0; y < outputHeight; ++y) {
                for (int x = 0; x < outputWidth; ++x) {
                    const qsizetype sourceOffset =
                        (static_cast<qsizetype>(y) * outputWidth + x)
                        * 3 * bytesPerSample;
                    const quint16 first = bayerSource ? 0 : readSample(centered.constData() + sourceOffset,
                                                     bytesPerSample, bigEndian);
                    const std::uint32_t green = bayerSource ? interpolate(x, y, 1) : readSample(
                        centered.constData() + sourceOffset + bytesPerSample,
                        bytesPerSample, bigEndian);
                    const quint16 third = bayerSource ? 0 : readSample(
                        centered.constData() + sourceOffset + 2 * bytesPerSample,
                        bytesPerSample, bigEndian);
                    const std::uint32_t red = bayerSource ? interpolate(x, y, 0) : (bgr ? third : first);
                    const std::uint32_t blue = bayerSource ? interpolate(x, y, 2) : (bgr ? first : third);
                    const quint16 luminance = static_cast<quint16>(
                        (2126U * red + 7152U * green + 722U * blue + 5000U) / 10000U);
                    const qsizetype destinationOffset =
                        (static_cast<qsizetype>(y) * outputWidth + x) * bytesPerSample;
                    writeSample(exportFrame.data() + destinationOffset, luminance,
                                bytesPerSample, bigEndian);
                }
            }
        } else {
            exportFrame = std::move(centered);
        }
        if (binOutput && !(convertToMonochrome && bayerSource)) {
            QByteArray binned(static_cast<qsizetype>(finalWidth) * finalHeight * bytesPerSample, '\0');
            for (int y = 0; y < finalHeight; ++y) {
                for (int x = 0; x < finalWidth; ++x) {
                    std::uint32_t sum = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                            sum += readSample(exportFrame.constData()
                                + (static_cast<qsizetype>(y * 2 + dy) * outputWidth + x * 2 + dx) * bytesPerSample,
                                bytesPerSample, bigEndian);
                    writeSample(binned.data() + (static_cast<qsizetype>(y) * finalWidth + x) * bytesPerSample,
                                static_cast<quint16>((sum + 2U) / 4U), bytesPerSample, bigEndian);
                }
            }
            exportFrame = std::move(binned);
        }
        if ((settings.rotationQuarterTurns & 3) != 0
            || settings.flipHorizontal || settings.flipVertical) {
            exportFrame = orientFrame(exportFrame, finalWidth, finalHeight,
                                      settings.monochrome ? bytesPerSample : bytesPerPixel,
                                      settings.rotationQuarterTurns,
                                      settings.flipHorizontal, settings.flipVertical);
        }
        if (output.write(exportFrame) != exportFrame.size()) {
            result.error = QStringLiteral("Could not write output frame %1.").arg(index + 1U);
            output.cancelWriting();
            return result;
        }
        ++completedFrames;
        if (progress) progress(completedFrames,
                               static_cast<int>(settings.frameIndices.size()));
    }

    bool allTimestamps = true;
    QByteArray timestamps;
    timestamps.reserve(settings.frameIndices.size() * 8);
    for (const std::uint32_t index : settings.frameIndices) {
        const std::uint64_t timestamp = source.frameTimestamp(index);
        if (timestamp == 0) { allTimestamps = false; break; }
        const qsizetype oldSize = timestamps.size();
        timestamps.resize(oldSize + 8);
        qToLittleEndian(timestamp, reinterpret_cast<uchar *>(timestamps.data() + oldSize));
    }
    if (allTimestamps) {
        if (output.write(timestamps) != timestamps.size()) {
            result.error = QStringLiteral("Could not preserve the frame timestamps.");
            output.cancelWriting();
            return result;
        }
        result.timestampsPreserved = true;
    }

    if (!output.commit()) {
        result.error = output.errorString();
        return result;
    }
    result.success = true;
    result.exportedFrames = static_cast<int>(settings.frameIndices.size());
    return result;
}

} // namespace seeingforge
