#include "core/SerExporter.h"

#include "core/SerFile.h"
#include "core/ObjectDetector.h"

#include <QFile>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
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

} // namespace

ExportResult SerExporter::exportFrames(
    const SerFile &source, const QString &outputPath,
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

    QFile input(source.path());
    if (!input.open(QIODevice::ReadOnly)) {
        result.error = input.errorString();
        return result;
    }
    QByteArray header = input.read(static_cast<qint64>(SerFile::HeaderSize));
    if (header.size() != static_cast<qsizetype>(SerFile::HeaderSize)) {
        result.error = QStringLiteral("The SER header could not be read.");
        return result;
    }
    int outputWidth = settings.outputSize.isValid()
        ? settings.outputSize.width() : static_cast<int>(source.header().width);
    int outputHeight = settings.outputSize.isValid()
        ? settings.outputSize.height() : static_cast<int>(source.header().height);
    outputWidth = std::max(2, outputWidth - (outputWidth & 1));
    outputHeight = std::max(2, outputHeight - (outputHeight & 1));
    const bool bayerSource = source.header().colorId >= 8
                             && source.header().colorId <= 19;
    const bool rgbSource = source.header().colorId == 100
                           || source.header().colorId == 101;
    const bool convertToMonochrome = settings.monochrome
                                     && (bayerSource || rgbSource);
    const bool binOutput = settings.monochrome && settings.binMonochrome;
    if (convertToMonochrome && bayerSource && !binOutput && source.header().colorId > 11) {
        result.error = QStringLiteral("Full-resolution monochrome conversion currently supports RGB Bayer patterns only.");
        return result;
    }
    const int finalWidth = binOutput
        ? outputWidth / 2 : outputWidth;
    const int finalHeight = binOutput
        ? outputHeight / 2 : outputHeight;
    if (settings.monochrome) {
        qToLittleEndian<quint32>(0, reinterpret_cast<uchar *>(header.data() + 18));
    }
    qToLittleEndian(static_cast<quint32>(finalWidth),
                    reinterpret_cast<uchar *>(header.data() + 26));
    qToLittleEndian(static_cast<quint32>(finalHeight),
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
        const std::uint64_t offset = SerFile::HeaderSize + frameBytes * index;
        if (!input.seek(static_cast<qint64>(offset))) {
            result.error = QStringLiteral("Could not seek to source frame %1.").arg(index + 1U);
            output.cancelWriting();
            return result;
        }
        const QByteArray frame = input.read(static_cast<qint64>(frameBytes));
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
            const bool bgr = source.header().colorId == 101;
            auto channelAt = [&](int x, int y) {
                const int pattern = static_cast<int>(source.header().colorId);
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
        if (output.write(exportFrame) != exportFrame.size()) {
            result.error = QStringLiteral("Could not write output frame %1.").arg(index + 1U);
            output.cancelWriting();
            return result;
        }
        ++completedFrames;
        if (progress) progress(completedFrames,
                               static_cast<int>(settings.frameIndices.size()));
    }

    const std::uint64_t sourceDataEnd = SerFile::HeaderSize
        + frameBytes * source.header().frameCount;
    const std::uint64_t timestampBytes = 8U * source.header().frameCount;
    const bool hasTimestampTrailer = static_cast<std::uint64_t>(input.size())
                                     >= sourceDataEnd + timestampBytes;
    if (hasTimestampTrailer) {
        for (const std::uint32_t index : settings.frameIndices) {
            if (!input.seek(static_cast<qint64>(sourceDataEnd + 8U * index))) {
                result.error = QStringLiteral("Could not seek to a frame timestamp.");
                output.cancelWriting();
                return result;
            }
            const QByteArray timestamp = input.read(8);
            if (timestamp.size() != 8 || output.write(timestamp) != 8) {
                result.error = QStringLiteral("Could not preserve the frame timestamps.");
                output.cancelWriting();
                return result;
            }
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
