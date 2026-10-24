#include "core/RawSequence.h"

#include <QDir>
#include <QFileInfo>
#include <QtEndian>

#include <libraw/libraw.h>

#include <algorithm>
#include <limits>

namespace seeingforge {
namespace {

constexpr std::uint64_t UnixEpochSerTicks = 621355968000000000ULL;

std::uint32_t bayerColorId(LibRaw &raw)
{
    int redX = -1;
    int redY = -1;
    int blueX = -1;
    int blueY = -1;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const int channel = raw.COLOR(y + raw.imgdata.sizes.top_margin,
                                          x + raw.imgdata.sizes.left_margin);
            const char color = channel >= 0 && channel < 4
                ? raw.imgdata.idata.cdesc[channel] : '?';
            if (color == 'R') { redX = x; redY = y; }
            if (color == 'B') { blueX = x; blueY = y; }
        }
    }
    if (redX < 0 || blueX < 0 || redX == blueX || redY == blueY) return 0;
    return 8U + static_cast<std::uint32_t>(redX + 2 * redY);
}

bool inspect(const QString &path, SerHeader &header, std::uint64_t &timestamp,
             std::array<double, 3> &whiteBalance, QString &error)
{
    LibRaw raw;
    const QByteArray encoded = QFile::encodeName(path);
    int status = raw.open_file(encoded.constData());
    if (status != LIBRAW_SUCCESS) {
        error = QString::fromLatin1(libraw_strerror(status));
        return false;
    }
    const std::uint32_t colorId = bayerColorId(raw);
    if (colorId == 0 || raw.imgdata.idata.filters == 0) {
        error = QStringLiteral("Only standard 2×2 RGB Bayer RAW files are supported.");
        return false;
    }
    header.fileId = QStringLiteral("LUCAM-RECORDER");
    header.colorId = colorId;
    header.endianFlag = 0;
    header.width = raw.imgdata.sizes.width;
    header.height = raw.imgdata.sizes.height;
    header.pixelDepth = 16;
    header.instrument = QString::fromLatin1(raw.imgdata.idata.make).trimmed()
        + QLatin1Char(' ') + QString::fromLatin1(raw.imgdata.idata.model).trimmed();
    header.instrument = header.instrument.trimmed();
    const double red = raw.imgdata.color.cam_mul[0];
    const double green1 = raw.imgdata.color.cam_mul[1];
    const double blue = raw.imgdata.color.cam_mul[2];
    const double green2 = raw.imgdata.color.cam_mul[3];
    const double green = green1 > 0.0 && green2 > 0.0
        ? (green1 + green2) / 2.0 : std::max(green1, green2);
    if (red > 0.0 && green > 0.0 && blue > 0.0) {
        const double largest = std::max({red, green, blue});
        whiteBalance = {red / largest, green / largest, blue / largest};
    }
    const auto unixTime = static_cast<std::int64_t>(raw.imgdata.other.timestamp);
    timestamp = unixTime > 0
        ? UnixEpochSerTicks + static_cast<std::uint64_t>(unixTime) * 10000000ULL : 0;
    return header.width > 0 && header.height > 0;
}

} // namespace

bool RawSequence::open(const QStringList &paths,
                       const std::function<void(int, int)> &progress,
                       const std::function<bool()> &shouldCancel)
{
    close();
    m_error.clear();
    if (paths.isEmpty()) {
        m_error = QStringLiteral("No RAW files were selected.");
        return false;
    }
    SerHeader reference;
    int completed = 0;
    for (const QString &path : paths) {
        if (shouldCancel && shouldCancel()) {
            m_error = QStringLiteral("RAW sequence loading was canceled.");
            close();
            return false;
        }
        SerHeader candidate;
        std::uint64_t timestamp = 0;
        std::array<double, 3> whiteBalance{1.0, 1.0, 1.0};
        QString error;
        if (!inspect(path, candidate, timestamp, whiteBalance, error)) {
            m_error = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error);
            close();
            return false;
        }
        if (!m_paths.isEmpty()
            && (candidate.width != reference.width || candidate.height != reference.height
                || candidate.colorId != reference.colorId
                || candidate.instrument != reference.instrument)) {
            m_error = QStringLiteral("%1 is not compatible with the first frame (camera, dimensions, or Bayer pattern differ).")
                          .arg(QFileInfo(path).fileName());
            close();
            return false;
        }
        if (m_paths.isEmpty()) {
            reference = candidate;
            m_cameraWhiteBalance = whiteBalance;
        }
        m_paths.append(QFileInfo(path).absoluteFilePath());
        m_timestamps.append(timestamp);
        ++completed;
        if (progress) progress(completed, static_cast<int>(paths.size()));
    }
    m_header = reference;
    m_header.frameCount = static_cast<std::uint32_t>(m_paths.size());
    m_header.dateTimeUtc = m_timestamps.isEmpty() ? 0 : m_timestamps.constFirst();
    m_header.dateTimeLocal = m_header.dateTimeUtc;
    return true;
}

void RawSequence::close()
{
    m_paths.clear();
    m_timestamps.clear();
    m_header = {};
    m_cameraWhiteBalance = {1.0, 1.0, 1.0};
}

bool RawSequence::isOpen() const { return !m_paths.isEmpty(); }
QString RawSequence::path() const { return m_paths.isEmpty() ? QString() : m_paths.constFirst(); }
QString RawSequence::displayName() const
{
    return m_paths.isEmpty() ? QString() : QStringLiteral("%1 RAW frames").arg(m_paths.size());
}
QString RawSequence::sourceDirectory() const
{
    return m_paths.isEmpty() ? QString() : QFileInfo(m_paths.constFirst()).absolutePath();
}
const SerHeader &RawSequence::header() const { return m_header; }
const QString &RawSequence::errorString() const { return m_error; }
const QStringList &RawSequence::files() const { return m_paths; }

QByteArray RawSequence::readRawFrame(std::uint32_t index) const
{
    if (index >= static_cast<std::uint32_t>(m_paths.size())) return {};
    LibRaw raw;
    const QByteArray encoded = QFile::encodeName(m_paths.at(static_cast<qsizetype>(index)));
    int status = raw.open_file(encoded.constData());
    if (status != LIBRAW_SUCCESS || raw.unpack() != LIBRAW_SUCCESS
        || raw.raw2image() != LIBRAW_SUCCESS
        || raw.subtract_black() != LIBRAW_SUCCESS || !raw.imgdata.image) return {};

    const int width = raw.imgdata.sizes.width;
    const int height = raw.imgdata.sizes.height;
    const int left = raw.imgdata.sizes.left_margin;
    const int top = raw.imgdata.sizes.top_margin;
    const std::uint32_t maximum = std::max(1U, raw.imgdata.color.maximum);
    QByteArray output(static_cast<qsizetype>(width) * height * 2, '\0');
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int channel = raw.COLOR(y + top, x + left);
            const std::uint32_t value = raw.imgdata.image[
                static_cast<qsizetype>(y) * raw.imgdata.sizes.iwidth + x][channel];
            const quint16 normalized = static_cast<quint16>(std::min<std::uint64_t>(
                65535U, (static_cast<std::uint64_t>(value) * 65535U + maximum / 2U) / maximum));
            qToLittleEndian(normalized, reinterpret_cast<uchar *>(
                output.data() + (static_cast<qsizetype>(y) * width + x) * 2));
        }
    }
    return output;
}

QImage RawSequence::decodeFrame(const QByteArray &rawFrame) const
{
    const int width = static_cast<int>(m_header.width);
    const int height = static_cast<int>(m_header.height);
    if (rawFrame.size() != static_cast<qsizetype>(width) * height * 2) return {};
    QImage image(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const auto *sample = reinterpret_cast<const uchar *>(
                rawFrame.constData() + (static_cast<qsizetype>(y) * width + x) * 2);
            line[x] = static_cast<uchar>((qFromLittleEndian<quint16>(sample) + 128U) / 257U);
        }
    }
    return image;
}

QImage RawSequence::readFrame(std::uint32_t index) const { return decodeFrame(readRawFrame(index)); }
std::uint64_t RawSequence::frameTimestamp(std::uint32_t index) const
{
    return index < static_cast<std::uint32_t>(m_timestamps.size())
        ? m_timestamps.at(static_cast<qsizetype>(index)) : 0;
}

std::array<double, 3> RawSequence::cameraWhiteBalance() const
{
    return m_cameraWhiteBalance;
}

bool RawSequence::hasCameraWhiteBalance() const
{
    return m_cameraWhiteBalance != std::array<double, 3>{1.0, 1.0, 1.0};
}

} // namespace seeingforge
