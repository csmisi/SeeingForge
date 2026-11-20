#include "core/RawSequence.h"

#include <QDir>
#include <QFileInfo>
#include <QtEndian>

#include <libraw/libraw.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

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

struct InspectOutcome {
    bool ok = false;
    SerHeader header;
    std::uint64_t timestamp = 0;
    std::array<double, 3> whiteBalance{1.0, 1.0, 1.0};
    QString error;
};

template <typename PixelValue, typename MakeLut, typename EmitRow>
bool decodeRawWithLut(const QString &path, int &outWidth, int &outHeight,
                      MakeLut &&makeLut, EmitRow &&emitRow)
{
    LibRaw raw;
    const QByteArray encoded = QFile::encodeName(path);
    if (raw.open_file(encoded.constData()) != LIBRAW_SUCCESS
        || raw.unpack() != LIBRAW_SUCCESS) {
        return false;
    }

    const int width = raw.imgdata.sizes.width;
    const int height = raw.imgdata.sizes.height;
    const int left = raw.imgdata.sizes.left_margin;
    const int top = raw.imgdata.sizes.top_margin;
    if (width <= 0 || height <= 0) return false;
    outWidth = width;
    outHeight = height;

    int ch[2][2]{};
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            ch[dy][dx] = raw.COLOR(top + dy, left + dx);
        }
    }

    const bool canDirectRaw = raw.imgdata.rawdata.raw_image != nullptr
        && raw.imgdata.color.cblack[4] == 0
        && raw.imgdata.color.cblack[5] == 0
        && raw.imgdata.rawdata.ph1_cblack == nullptr
        && raw.imgdata.sizes.mask[0][2] == 0
        && raw.imgdata.sizes.mask[0][3] == 0
        && (raw.imgdata.sizes.raw_pitch == 0
            || raw.imgdata.sizes.raw_pitch
                   == static_cast<unsigned>(raw.imgdata.sizes.raw_width) * 2U)
        && left + width <= static_cast<int>(raw.imgdata.sizes.raw_width)
        && top + height <= static_cast<int>(raw.imgdata.sizes.raw_height);

    if (canDirectRaw) {
        std::array<std::uint32_t, 4> blackLevels{};
        std::uint32_t minBlack = std::numeric_limits<std::uint32_t>::max();
        for (std::size_t i = 0; i < 4; ++i) {
            blackLevels[i] = raw.imgdata.color.black + raw.imgdata.color.cblack[i];
            minBlack = std::min(minBlack, blackLevels[i]);
        }
        const std::uint32_t effectiveMax = std::max(
            1U, raw.imgdata.color.maximum > minBlack
                    ? raw.imgdata.color.maximum - minBlack
                    : raw.imgdata.color.maximum);
        const std::vector<PixelValue> lut = makeLut(effectiveMax);
        const ushort *rawImage = raw.imgdata.rawdata.raw_image;
        const int rawWidth = raw.imgdata.sizes.raw_width;
        for (int y = 0; y < height; ++y) {
            const ushort *srcRow = rawImage + static_cast<qsizetype>(top + y) * rawWidth + left;
            const int *rowCh = ch[y & 1];
            emitRow(y, width, [&](int x) -> PixelValue {
                const std::uint32_t rawVal = srcRow[x];
                const std::uint32_t blk = blackLevels[static_cast<std::size_t>(rowCh[x & 1])];
                const std::uint32_t sub = rawVal > blk ? rawVal - blk : 0U;
                return lut[static_cast<std::size_t>(sub)];
            });
        }
        return true;
    }

    if (raw.raw2image_ex(1) != LIBRAW_SUCCESS || !raw.imgdata.image) return false;
    const std::uint32_t effectiveMax = std::max(1U, raw.imgdata.color.maximum);
    const std::vector<PixelValue> lut = makeLut(effectiveMax);
    const int iwidth = raw.imgdata.sizes.iwidth;
    for (int y = 0; y < height; ++y) {
        const auto *srcRow = raw.imgdata.image + static_cast<qsizetype>(y) * iwidth;
        const int *rowCh = ch[y & 1];
        emitRow(y, width, [&](int x) -> PixelValue {
            const std::uint16_t val = srcRow[x][rowCh[x & 1]];
            return lut[static_cast<std::size_t>(val)];
        });
    }
    return true;
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
    if (shouldCancel && shouldCancel()) {
        m_error = QStringLiteral("RAW sequence loading was canceled.");
        return false;
    }

    const std::size_t total = static_cast<std::size_t>(paths.size());
    std::vector<InspectOutcome> outcomes(total);
    std::atomic<std::size_t> nextIndex{0};
    std::atomic_bool stop{false};
    std::atomic<int> completedCount{0};
    std::mutex callbackMutex;

    const unsigned hwThreads = std::thread::hardware_concurrency();
    const std::size_t workerCount = std::min<std::size_t>(
        total, std::clamp<std::size_t>(hwThreads == 0 ? 4U : hwThreads, 1U, 8U));

    auto worker = [&] {
        for (;;) {
            if (stop.load()) break;
            if (shouldCancel) {
                std::lock_guard<std::mutex> lock(callbackMutex);
                if (shouldCancel()) {
                    stop.store(true);
                    break;
                }
            }
            const std::size_t idx = nextIndex.fetch_add(1);
            if (idx >= total) break;
            InspectOutcome outcome;
            outcome.ok = inspect(paths.at(static_cast<qsizetype>(idx)),
                                 outcome.header, outcome.timestamp,
                                 outcome.whiteBalance, outcome.error);
            if (!outcome.ok) {
                outcomes[idx] = std::move(outcome);
                stop.store(true);
                break;
            }
            outcomes[idx] = std::move(outcome);
            const int done = completedCount.fetch_add(1) + 1;
            if (progress) {
                std::lock_guard<std::mutex> lock(callbackMutex);
                progress(done, static_cast<int>(total));
            }
        }
    };

    if (workerCount <= 1) {
        worker();
    } else {
        std::vector<std::thread> threads;
        threads.reserve(workerCount);
        for (std::size_t t = 0; t < workerCount; ++t) {
            threads.emplace_back(worker);
        }
        for (auto &thread : threads) {
            thread.join();
        }
    }

    if (shouldCancel && shouldCancel()) {
        m_error = QStringLiteral("RAW sequence loading was canceled.");
        close();
        return false;
    }

    SerHeader reference;
    for (std::size_t i = 0; i < total; ++i) {
        const QString &path = paths.at(static_cast<qsizetype>(i));
        const InspectOutcome &outcome = outcomes[i];
        if (!outcome.ok) {
            m_error = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), outcome.error);
            close();
            return false;
        }
        if (!m_paths.isEmpty()
            && (outcome.header.width != reference.width
                || outcome.header.height != reference.height
                || outcome.header.colorId != reference.colorId
                || outcome.header.instrument != reference.instrument)) {
            m_error = QStringLiteral("%1 is not compatible with the first frame (camera, dimensions, or Bayer pattern differ).")
                          .arg(QFileInfo(path).fileName());
            close();
            return false;
        }
        if (m_paths.isEmpty()) {
            reference = outcome.header;
            m_cameraWhiteBalance = outcome.whiteBalance;
        }
        m_paths.append(QFileInfo(path).absoluteFilePath());
        m_timestamps.append(outcome.timestamp);
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
    int width = 0;
    int height = 0;
    QByteArray output;
    const auto makeLut16 = [](std::uint32_t maximum) {
        std::vector<quint16> lut(65536U);
        const std::uint32_t halfMax = maximum / 2U;
        for (std::uint32_t v = 0; v < 65536U; ++v) {
            lut[v] = static_cast<quint16>(std::min<std::uint64_t>(
                65535U, (static_cast<std::uint64_t>(v) * 65535U + halfMax) / maximum));
        }
        return lut;
    };
    const bool ok = decodeRawWithLut<quint16>(
        m_paths.at(static_cast<qsizetype>(index)), width, height, makeLut16,
        [&](int y, int w, const auto &readPixel) {
            if (output.isEmpty()) {
                output.resize(static_cast<qsizetype>(w) * height * 2);
            }
            auto *dst = reinterpret_cast<uchar *>(output.data())
                        + static_cast<qsizetype>(y) * w * 2;
            for (int x = 0; x < w; ++x) {
                qToLittleEndian(readPixel(x), dst + static_cast<qsizetype>(x) * 2);
            }
        });
    return ok ? output : QByteArray{};
}

QImage RawSequence::decodeFrame(const QByteArray &rawFrame) const
{
    const int width = static_cast<int>(m_header.width);
    const int height = static_cast<int>(m_header.height);
    if (rawFrame.size() != static_cast<qsizetype>(width) * height * 2) return {};
    QImage image(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        uchar *line = image.scanLine(y);
        const auto *rowPtr = reinterpret_cast<const uchar *>(
            rawFrame.constData() + static_cast<qsizetype>(y) * width * 2);
        for (int x = 0; x < width; ++x) {
            line[x] = static_cast<uchar>(
                (qFromLittleEndian<quint16>(rowPtr + static_cast<qsizetype>(x) * 2) + 128U) / 257U);
        }
    }
    return image;
}

QImage RawSequence::readFrame(std::uint32_t index) const
{
    if (index >= static_cast<std::uint32_t>(m_paths.size())) return {};
    int width = 0;
    int height = 0;
    QImage image;
    const auto makeLut8 = [](std::uint32_t maximum) {
        std::vector<uchar> lut(65536U);
        const std::uint32_t halfMax = maximum / 2U;
        for (std::uint32_t v = 0; v < 65536U; ++v) {
            const std::uint32_t norm16 = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                65535U, (static_cast<std::uint64_t>(v) * 65535U + halfMax) / maximum));
            lut[v] = static_cast<uchar>((norm16 + 128U) / 257U);
        }
        return lut;
    };
    const bool ok = decodeRawWithLut<uchar>(
        m_paths.at(static_cast<qsizetype>(index)), width, height, makeLut8,
        [&](int y, int w, const auto &readPixel) {
            if (image.isNull()) {
                image = QImage(w, height, QImage::Format_Grayscale8);
            }
            uchar *line = image.scanLine(y);
            for (int x = 0; x < w; ++x) {
                line[x] = readPixel(x);
            }
        });
    return ok ? image : QImage{};
}
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
