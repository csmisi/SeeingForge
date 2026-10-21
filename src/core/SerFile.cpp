#include "core/SerFile.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <limits>

namespace seeingforge {
namespace {

constexpr int FileIdLength = 14;
constexpr int TextFieldLength = 40;

bool isSupportedColor(std::uint32_t colorId)
{
    switch (colorId) {
    case 0:
    case 8:
    case 9:
    case 10:
    case 11:
    case 16:
    case 17:
    case 18:
    case 19:
    case 100:
    case 101:
        return true;
    default:
        return false;
    }
}

QString readText(const QByteArray &data, int offset, int length)
{
    QByteArray text = data.mid(offset, length);
    const qsizetype terminator = text.indexOf('\0');
    if (terminator >= 0) {
        text.truncate(terminator);
    }
    return QString::fromLatin1(text).trimmed();
}

std::uint32_t readU32(const QByteArray &data, int offset)
{
    return qFromLittleEndian<std::uint32_t>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

std::uint64_t readU64(const QByteArray &data, int offset)
{
    return qFromLittleEndian<std::uint64_t>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

int toDisplayValue(std::uint16_t value, std::uint32_t depth)
{
    if (depth <= 8) {
        return static_cast<int>(value);
    }
    const std::uint32_t effectiveDepth = std::min(depth, 16U);
    const std::uint32_t maximum = (1U << effectiveDepth) - 1U;
    return static_cast<int>((static_cast<std::uint32_t>(value) * 255U + maximum / 2U) / maximum);
}

std::uint16_t sampleAt(const QByteArray &frame, std::uint64_t sampleIndex,
                       std::uint32_t depth, bool bigEndian)
{
    if (depth <= 8) {
        return static_cast<uchar>(frame.at(static_cast<qsizetype>(sampleIndex)));
    }

    const auto byteIndex = static_cast<qsizetype>(sampleIndex * 2U);
    const auto *bytes = reinterpret_cast<const uchar *>(frame.constData() + byteIndex);
    return bigEndian ? qFromBigEndian<std::uint16_t>(bytes)
                     : qFromLittleEndian<std::uint16_t>(bytes);
}

} // namespace

QString SerHeader::colorName() const
{
    switch (colorId) {
    case 0: return QStringLiteral("Monochrome");
    case 8: return QStringLiteral("Bayer RGGB");
    case 9: return QStringLiteral("Bayer GRBG");
    case 10: return QStringLiteral("Bayer GBRG");
    case 11: return QStringLiteral("Bayer BGGR");
    case 16: return QStringLiteral("Bayer CYYM");
    case 17: return QStringLiteral("Bayer YCMY");
    case 18: return QStringLiteral("Bayer YMCY");
    case 19: return QStringLiteral("Bayer MYYC");
    case 100: return QStringLiteral("RGB");
    case 101: return QStringLiteral("BGR");
    default: return QStringLiteral("Unknown (%1)").arg(colorId);
    }
}

int SerHeader::planeCount() const
{
    return (colorId == 100 || colorId == 101) ? 3 : 1;
}

std::uint64_t SerHeader::bytesPerFrame() const
{
    const std::uint64_t bytesPerSample = pixelDepth <= 8 ? 1U : 2U;
    return static_cast<std::uint64_t>(width) * height
           * static_cast<std::uint64_t>(planeCount()) * bytesPerSample;
}

bool SerFile::open(const QString &path)
{
    close();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }

    const QByteArray raw = file.read(static_cast<qint64>(HeaderSize));
    if (raw.size() != static_cast<qsizetype>(HeaderSize)) {
        m_error = QStringLiteral("The file is too small to contain a SER header.");
        return false;
    }

    SerHeader candidate;
    candidate.fileId = readText(raw, 0, FileIdLength);
    candidate.cameraId = readU32(raw, 14);
    candidate.colorId = readU32(raw, 18);
    candidate.endianFlag = readU32(raw, 22);
    candidate.width = readU32(raw, 26);
    candidate.height = readU32(raw, 30);
    candidate.pixelDepth = readU32(raw, 34);
    candidate.frameCount = readU32(raw, 38);
    candidate.observer = readText(raw, 42, TextFieldLength);
    candidate.instrument = readText(raw, 82, TextFieldLength);
    candidate.telescope = readText(raw, 122, TextFieldLength);
    candidate.dateTimeLocal = readU64(raw, 162);
    candidate.dateTimeUtc = readU64(raw, 170);

    if (!candidate.fileId.endsWith(QStringLiteral("RECORDER"), Qt::CaseInsensitive)) {
        m_error = QStringLiteral(
            "The file identifier '%1' is not recognized as a SER recorder identifier.")
                      .arg(candidate.fileId.isEmpty() ? QStringLiteral("(empty)")
                                                     : candidate.fileId);
        return false;
    }
    if (candidate.width == 0 || candidate.height == 0 || candidate.frameCount == 0) {
        m_error = QStringLiteral("The SER header contains invalid image dimensions or frame count.");
        return false;
    }
    if (candidate.pixelDepth == 0 || candidate.pixelDepth > 16) {
        m_error = QStringLiteral("Unsupported SER pixel depth: %1 bits.").arg(candidate.pixelDepth);
        return false;
    }
    if (!isSupportedColor(candidate.colorId)) {
        m_error = QStringLiteral("Unsupported SER colour format: %1.").arg(candidate.colorId);
        return false;
    }

    const std::uint64_t frameBytes = candidate.bytesPerFrame();
    if (frameBytes == 0
        || frameBytes > static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())) {
        m_error = QStringLiteral("The SER frame size is not supported.");
        return false;
    }

    if (candidate.frameCount > (std::numeric_limits<std::uint64_t>::max() - HeaderSize) / frameBytes) {
        m_error = QStringLiteral("The SER file declares an impossible amount of image data.");
        return false;
    }
    const std::uint64_t requiredSize = HeaderSize + frameBytes * candidate.frameCount;
    if (requiredSize > static_cast<std::uint64_t>(file.size())) {
        m_error = QStringLiteral("The SER file is truncated: its header describes more image data than the file contains.");
        return false;
    }

    m_path = path;
    m_header = candidate;
    m_error.clear();
    m_isOpen = true;
    return true;
}

void SerFile::close()
{
    m_path.clear();
    m_header = {};
    m_error.clear();
    m_isOpen = false;
}

bool SerFile::isOpen() const { return m_isOpen; }
const QString &SerFile::path() const { return m_path; }
const SerHeader &SerFile::header() const { return m_header; }
const QString &SerFile::errorString() const { return m_error; }

QImage SerFile::readFrame(std::uint32_t index) const
{
    return decodeFrame(readRawFrame(index));
}

QByteArray SerFile::readRawFrame(std::uint32_t index) const
{
    if (!m_isOpen || index >= m_header.frameCount) {
        return {};
    }

    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const std::uint64_t frameBytes = m_header.bytesPerFrame();
    const std::uint64_t offset = HeaderSize + frameBytes * index;
    if (!file.seek(static_cast<qint64>(offset))) {
        return {};
    }
    const QByteArray frame = file.read(static_cast<qint64>(frameBytes));
    return frame.size() == static_cast<qsizetype>(frameBytes) ? frame : QByteArray{};
}

QImage SerFile::decodeFrame(const QByteArray &frame) const
{
    if (!m_isOpen || frame.size() != static_cast<qsizetype>(m_header.bytesPerFrame()))
        return {};

    const int width = static_cast<int>(m_header.width);
    const int height = static_cast<int>(m_header.height);
    const bool bigEndian = m_header.endianFlag != 0; // Widely used de-facto SER interpretation.

    if (m_header.planeCount() == 1) {
        QImage image(width, height, QImage::Format_Grayscale8);
        for (int y = 0; y < height; ++y) {
            uchar *line = image.scanLine(y);
            for (int x = 0; x < width; ++x) {
                const auto sampleIndex = static_cast<std::uint64_t>(y) * m_header.width
                                         + static_cast<std::uint64_t>(x);
                line[x] = static_cast<uchar>(toDisplayValue(
                    sampleAt(frame, sampleIndex, m_header.pixelDepth, bigEndian),
                    m_header.pixelDepth));
            }
        }
        return image;
    }

    QImage image(width, height, QImage::Format_RGB888);
    const bool bgr = m_header.colorId == 101;
    for (int y = 0; y < height; ++y) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const auto pixelIndex = static_cast<std::uint64_t>(y) * m_header.width
                                    + static_cast<std::uint64_t>(x);
            const auto base = pixelIndex * 3U;
            const int first = toDisplayValue(sampleAt(frame, base, m_header.pixelDepth, bigEndian),
                                             m_header.pixelDepth);
            const int green = toDisplayValue(sampleAt(frame, base + 1U, m_header.pixelDepth, bigEndian),
                                             m_header.pixelDepth);
            const int third = toDisplayValue(sampleAt(frame, base + 2U, m_header.pixelDepth, bigEndian),
                                             m_header.pixelDepth);
            const int byte = x * 3;
            line[byte] = static_cast<uchar>(bgr ? third : first);
            line[byte + 1] = static_cast<uchar>(green);
            line[byte + 2] = static_cast<uchar>(bgr ? first : third);
        }
    }
    return image;
}

} // namespace seeingforge
