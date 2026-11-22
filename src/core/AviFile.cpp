#include "core/AviFile.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTime>
#include <QTimeZone>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace seeingforge {
namespace {

constexpr std::uint64_t UnixEpochToSerEpochDays = 719162ULL;
constexpr std::uint64_t TicksPerSecond = 10000000ULL;
constexpr std::uint64_t UnixEpochSerTicks = UnixEpochToSerEpochDays * 86400ULL * TicksPerSecond;

std::uint16_t readU16(const QByteArray &data, qsizetype offset)
{
    return qFromLittleEndian<std::uint16_t>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

std::uint32_t readU32(const QByteArray &data, qsizetype offset)
{
    return qFromLittleEndian<std::uint32_t>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

qint32 readI32(const QByteArray &data, qsizetype offset)
{
    return qFromLittleEndian<qint32>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

std::uint64_t readU64(const QByteArray &data, qsizetype offset)
{
    return qFromLittleEndian<std::uint64_t>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

QString fourCcDisplay(const QByteArray &fourCc)
{
    QString out;
    out.reserve(4);
    for (int i = 0; i < std::min<int>(4, static_cast<int>(fourCc.size())); ++i) {
        const unsigned char ch = static_cast<unsigned char>(fourCc.at(i));
        out.append(ch >= 32 && ch <= 126 ? QChar::fromLatin1(static_cast<char>(ch))
                                         : QLatin1Char(' '));
    }
    return out;
}

bool readAt(QFile &file, std::uint64_t fileSize, std::uint64_t offset, std::uint64_t size,
            QByteArray &out)
{
    if (offset > fileSize || size > fileSize - offset
        || size > static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    if (!file.seek(static_cast<qint64>(offset))) {
        return false;
    }
    out = file.read(static_cast<qint64>(size));
    return static_cast<std::uint64_t>(out.size()) == size;
}

bool addChunkNext(std::uint64_t pos, std::uint32_t size, std::uint64_t &nextOut)
{
    const std::uint64_t padded = static_cast<std::uint64_t>(size) + (size & 1U);
    if (pos > std::numeric_limits<std::uint64_t>::max() - 8ULL - padded) {
        return false;
    }
    nextOut = pos + 8ULL + padded;
    return true;
}

struct MoviRange {
    std::uint64_t moviTypeOffset = 0;
    std::uint64_t dataStart = 0;
    std::uint64_t dataEnd = 0;
};

struct ParsedAviChunks {
    bool hasAvih = false;
    std::uint32_t usPerFrame = 0;
    std::uint32_t avihWidth = 0;
    std::uint32_t avihHeight = 0;

    bool hasVideoStream = false;
    int videoStreamIndex = 0;
    QByteArray fccHandler;
    std::uint32_t strhScale = 0;
    std::uint32_t strhRate = 0;
    std::uint32_t strhLength = 0;
    QByteArray strfData;
    QByteArray indxData;

    bool openDml = false;
    bool hasAvix = false;
    QVector<MoviRange> moviRanges;
    std::uint64_t idx1Offset = 0;
    std::uint32_t idx1Size = 0;
    bool hasIdx1 = false;
};

bool parseStrlList(QFile &file, std::uint64_t fileSize, std::uint64_t start, std::uint64_t end,
                   int streamIndex, ParsedAviChunks &parsed)
{
    QByteArray fccType;
    QByteArray fccHandler;
    std::uint32_t scale = 0;
    std::uint32_t rate = 0;
    std::uint32_t length = 0;
    QByteArray strfData;
    QByteArray indxData;

    std::uint64_t pos = start;
    while (pos + 8ULL <= end) {
        QByteArray hdr;
        if (!readAt(file, fileSize, pos, 8, hdr)) break;
        const QByteArray chunkId = hdr.left(4);
        const std::uint32_t chunkSize = readU32(hdr, 4);
        const std::uint64_t payloadPos = pos + 8ULL;
        const std::uint64_t payloadEnd = std::min<std::uint64_t>(
            end, payloadPos + static_cast<std::uint64_t>(chunkSize));

        if (chunkId == "strh" && payloadEnd - payloadPos >= 48ULL) {
            QByteArray data;
            if (readAt(file, fileSize, payloadPos, 48ULL, data)) {
                fccType = data.mid(0, 4);
                fccHandler = data.mid(4, 4);
                scale = readU32(data, 20);
                rate = readU32(data, 24);
                length = readU32(data, 32);
            }
        } else if (chunkId == "strf" && payloadEnd > payloadPos) {
            const std::uint64_t toRead = std::min<std::uint64_t>(
                payloadEnd - payloadPos, 65536ULL);
            readAt(file, fileSize, payloadPos, toRead, strfData);
        } else if (chunkId == "indx" && payloadEnd - payloadPos >= 24ULL) {
            parsed.openDml = true;
            const std::uint64_t toRead = std::min<std::uint64_t>(
                payloadEnd - payloadPos, 16ULL * 1024ULL * 1024ULL);
            readAt(file, fileSize, payloadPos, toRead, indxData);
        }

        std::uint64_t nextPos = 0;
        if (!addChunkNext(pos, chunkSize, nextPos) || nextPos <= pos) break;
        pos = nextPos;
    }

    if (!parsed.hasVideoStream && fccType == "vids") {
        parsed.hasVideoStream = true;
        parsed.videoStreamIndex = streamIndex;
        parsed.fccHandler = fccHandler;
        parsed.strhScale = scale;
        parsed.strhRate = rate;
        parsed.strhLength = length;
        parsed.strfData = std::move(strfData);
        parsed.indxData = std::move(indxData);
    }
    return true;
}

void parseHdrlList(QFile &file, std::uint64_t fileSize, std::uint64_t start, std::uint64_t end,
                   ParsedAviChunks &parsed)
{
    int streamIndex = 0;
    std::uint64_t pos = start;
    while (pos + 8ULL <= end) {
        QByteArray hdr;
        if (!readAt(file, fileSize, pos, 8, hdr)) break;
        const QByteArray chunkId = hdr.left(4);
        const std::uint32_t chunkSize = readU32(hdr, 4);
        const std::uint64_t payloadPos = pos + 8ULL;
        const std::uint64_t payloadEnd = std::min<std::uint64_t>(
            end, payloadPos + static_cast<std::uint64_t>(chunkSize));

        if (chunkId == "avih" && payloadEnd - payloadPos >= 56ULL) {
            QByteArray data;
            if (readAt(file, fileSize, payloadPos, 56ULL, data)) {
                parsed.hasAvih = true;
                parsed.usPerFrame = readU32(data, 0);
                parsed.avihWidth = readU32(data, 32);
                parsed.avihHeight = readU32(data, 36);
            }
        } else if (chunkId == "LIST" && payloadEnd - payloadPos >= 4ULL) {
            QByteArray listType;
            if (readAt(file, fileSize, payloadPos, 4, listType)) {
                if (listType == "strl") {
                    parseStrlList(file, fileSize, payloadPos + 4ULL, payloadEnd,
                                  streamIndex++, parsed);
                } else if (listType == "odml") {
                    parsed.openDml = true;
                }
            }
        }

        std::uint64_t nextPos = 0;
        if (!addChunkNext(pos, chunkSize, nextPos) || nextPos <= pos) break;
        pos = nextPos;
    }
}

void parseRiffBody(QFile &file, std::uint64_t fileSize, std::uint64_t start, std::uint64_t end,
                   bool parseHeaders, ParsedAviChunks &parsed)
{
    std::uint64_t pos = start;
    while (pos + 8ULL <= end) {
        QByteArray hdr;
        if (!readAt(file, fileSize, pos, 8, hdr)) break;
        const QByteArray chunkId = hdr.left(4);
        const std::uint32_t chunkSize = readU32(hdr, 4);
        const std::uint64_t payloadPos = pos + 8ULL;
        const std::uint64_t payloadEnd = std::min<std::uint64_t>(
            end, payloadPos + static_cast<std::uint64_t>(chunkSize));

        if (chunkId == "LIST" && payloadEnd - payloadPos >= 4ULL) {
            QByteArray listType;
            if (readAt(file, fileSize, payloadPos, 4, listType)) {
                if (listType == "hdrl" && parseHeaders) {
                    parseHdrlList(file, fileSize, payloadPos + 4ULL, payloadEnd, parsed);
                } else if (listType == "movi") {
                    parsed.moviRanges.append(
                        {payloadPos, payloadPos + 4ULL, payloadEnd});
                }
            }
        } else if (chunkId == "idx1" && parseHeaders && !parsed.hasIdx1) {
            parsed.hasIdx1 = true;
            parsed.idx1Offset = payloadPos;
            parsed.idx1Size = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(payloadEnd - payloadPos,
                                        std::numeric_limits<std::uint32_t>::max()));
        }

        std::uint64_t nextPos = 0;
        if (!addChunkNext(pos, chunkSize, nextPos) || nextPos <= pos) break;
        pos = nextPos;
    }
}

bool isVideoChunkId(const QByteArray &chunkId, const QByteArray &dbId, const QByteArray &dcId)
{
    return chunkId == dbId || chunkId == dcId;
}

template <typename EntryList>
void parseStandardIndexPayload(const QByteArray &payload, std::uint64_t fileSize,
                               std::uint64_t minFrameBytes,
                               const QByteArray &dbId, const QByteArray &dcId,
                               EntryList &outFrames)
{
    if (payload.size() < 24) return;
    const std::uint16_t wLongsPerEntry = readU16(payload, 0);
    const std::uint8_t bIndexSubType = static_cast<std::uint8_t>(payload.at(2));
    const std::uint8_t bIndexType = static_cast<std::uint8_t>(payload.at(3));
    const std::uint32_t nEntriesInUse = readU32(payload, 4);
    const QByteArray chunkId = payload.mid(8, 4);
    if (bIndexType != 1 || bIndexSubType != 0 || wLongsPerEntry < 2) return;
    if (!isVideoChunkId(chunkId, dbId, dcId)) return;

    const std::uint64_t qwBaseOffset = readU64(payload, 12);
    const qsizetype entryStride = static_cast<qsizetype>(wLongsPerEntry) * 4;
    const qsizetype maxEntries = (payload.size() - 24) / entryStride;
    const qsizetype count = std::min<qsizetype>(
        static_cast<qsizetype>(nEntriesInUse), maxEntries);
    outFrames.reserve(outFrames.size() + count);

    for (qsizetype i = 0; i < count; ++i) {
        const qsizetype entryOffset = 24 + i * entryStride;
        const std::uint32_t dwOffset = readU32(payload, entryOffset);
        const std::uint32_t dwSize = readU32(payload, entryOffset + 4) & 0x7FFFFFFFU;
        if (static_cast<std::uint64_t>(dwSize) < minFrameBytes) continue;
        if (dwOffset > fileSize || qwBaseOffset > fileSize - dwOffset) continue;
        const std::uint64_t dataOffset = qwBaseOffset + dwOffset;
        if (dataOffset > fileSize || static_cast<std::uint64_t>(dwSize) > fileSize - dataOffset) {
            continue;
        }
        outFrames.append({dataOffset, dwSize});
    }
}

template <typename EntryList>
void scanMoviRange(QFile &file, std::uint64_t fileSize, std::uint64_t start, std::uint64_t end,
                   std::uint64_t minFrameBytes, const QByteArray &dbId, const QByteArray &dcId,
                   EntryList &outFrames)
{
    std::uint64_t pos = start;
    while (pos + 8ULL <= end) {
        QByteArray hdr;
        if (!readAt(file, fileSize, pos, 8, hdr)) break;
        const QByteArray chunkId = hdr.left(4);
        const std::uint32_t chunkSize = readU32(hdr, 4);
        const std::uint64_t payloadPos = pos + 8ULL;
        const std::uint64_t payloadEnd = std::min<std::uint64_t>(
            end, payloadPos + static_cast<std::uint64_t>(chunkSize));

        if (chunkId == "LIST" && payloadEnd - payloadPos >= 4ULL) {
            QByteArray listType;
            if (readAt(file, fileSize, payloadPos, 4, listType) && listType == "rec ") {
                scanMoviRange(file, fileSize, payloadPos + 4ULL, payloadEnd,
                              minFrameBytes, dbId, dcId, outFrames);
            }
        } else if (isVideoChunkId(chunkId, dbId, dcId)) {
            if (static_cast<std::uint64_t>(chunkSize) >= minFrameBytes
                && payloadPos <= fileSize
                && static_cast<std::uint64_t>(chunkSize) <= fileSize - payloadPos) {
                outFrames.append({payloadPos, chunkSize});
            }
        }

        std::uint64_t nextPos = 0;
        if (!addChunkNext(pos, chunkSize, nextPos) || nextPos <= pos) break;
        pos = nextPos;
    }
}

QString findSidecarPath(const QString &aviPath)
{
    const QFileInfo aviInfo(aviPath);
    const QString direct = aviPath + QStringLiteral(".txt");
    if (QFileInfo::exists(direct) && QFileInfo(direct).isFile()) {
        return direct;
    }

    const QDir dir = aviInfo.dir();
    const QStringList txtFiles = dir.entryList(
        {QStringLiteral("*.txt"), QStringLiteral("*.TXT")}, QDir::Files);
    const QString expectedFull = aviInfo.fileName() + QStringLiteral(".txt");
    for (const QString &candidate : txtFiles) {
        if (candidate.compare(expectedFull, Qt::CaseInsensitive) == 0) {
            return dir.filePath(candidate);
        }
    }

    const QString baseExact = dir.filePath(aviInfo.completeBaseName() + QStringLiteral(".txt"));
    if (QFileInfo::exists(baseExact) && QFileInfo(baseExact).isFile()) {
        return baseExact;
    }
    const QString expectedBase = aviInfo.completeBaseName() + QStringLiteral(".txt");
    for (const QString &candidate : txtFiles) {
        if (candidate.compare(expectedBase, Qt::CaseInsensitive) == 0) {
            return dir.filePath(candidate);
        }
    }
    return {};
}

std::uint32_t parseBayerText(const QString &value)
{
    const QString upper = value.trimmed().toUpper();
    if (upper == QLatin1String("RG") || upper == QLatin1String("RGGB")) return 8U;
    if (upper == QLatin1String("GR") || upper == QLatin1String("GRBG")) return 9U;
    if (upper == QLatin1String("GB") || upper == QLatin1String("GBRG")) return 10U;
    if (upper == QLatin1String("BG") || upper == QLatin1String("BGGR")) return 11U;
    return 0U;
}

bool parseIsoUtcTicks(const QString &text, std::uint64_t &outTicks)
{
    static const QRegularExpression pattern(QStringLiteral(
        R"(^\s*(\d{4})-(\d{2})-(\d{2})[T ](\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?(?:(Z)|([+-])(\d{2}):?(\d{2}))?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) return false;

    const int year = match.captured(1).toInt();
    const int month = match.captured(2).toInt();
    const int day = match.captured(3).toInt();
    const int hour = match.captured(4).toInt();
    const int minute = match.captured(5).toInt();
    const int second = match.captured(6).toInt();

    const QDate date(year, month, day);
    const QTime time(hour, minute, second);
    if (!date.isValid() || !time.isValid()) return false;

    QDateTime dt(date, time, QTimeZone::utc());
    if (!dt.isValid()) return false;

    if (!match.captured(9).isEmpty()) {
        const qint64 tzHours = match.captured(10).toLongLong();
        const qint64 tzMins = match.captured(11).toLongLong();
        const qint64 offsetSecs = (match.captured(9) == QLatin1String("-") ? -1 : 1)
                                  * (tzHours * 3600LL + tzMins * 60LL);
        dt = dt.addSecs(-offsetSecs);
    }

    std::uint64_t fracTicks = 0;
    const QString frac = match.captured(7);
    if (!frac.isEmpty()) {
        const QString frac7 = frac.left(7).leftJustified(7, QLatin1Char('0'));
        fracTicks = frac7.toULongLong();
        if (frac.size() > 7 && frac.at(7) >= QLatin1Char('5')) {
            ++fracTicks;
        }
    }

    const qint64 unixSecs = dt.toSecsSinceEpoch();
    const std::int64_t serSecs = static_cast<std::int64_t>(UnixEpochToSerEpochDays * 86400ULL)
                                 + unixSecs;
    if (serSecs < 0) return false;

    outTicks = static_cast<std::uint64_t>(serSecs) * TicksPerSecond + fracTicks;
    return true;
}

void parseSidecarFile(const QString &sidecarPath, SerHeader &header, AviMetadata &metadata)
{
    QFile file(sidecarPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    metadata.hasSidecar = true;
    metadata.sidecarPath = sidecarPath;

    QString cropLeft, cropTop, cropWidth, cropHeight;
    QString startX, startY;
    QString siteLat, siteLon;

    while (!file.atEnd()) {
        const QByteArray rawLine = file.readLine();
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))
            && line.size() > 2) {
            metadata.sensorSection = line.mid(1, line.size() - 2).trimmed();
            if (header.instrument.isEmpty()) {
                header.instrument = metadata.sensorSection;
            }
            continue;
        }

        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed().toLower();
        const QString value = line.mid(eq + 1).trimmed();

        if (key == QLatin1String("bayer") || key == QLatin1String("bayer pattern")
            || key == QLatin1String("bayerpattern")) {
            metadata.bayerText = value;
            if (header.planeCount() == 1) {
                const std::uint32_t bayerId = parseBayerText(value);
                if (bayerId != 0U) {
                    header.colorId = bayerId;
                }
            }
        } else if (key == QLatin1String("colour format") || key == QLatin1String("color format")) {
            metadata.colorFormatText = value;
        } else if (key == QLatin1String("flip")) {
            metadata.flipText = value;
        } else if (key == QLatin1String("capture area size")) {
            metadata.captureAreaText = value;
        } else if (key == QLatin1String("startx")) {
            startX = value;
        } else if (key == QLatin1String("starty")) {
            startY = value;
        } else if (key == QLatin1String("cropleft")) {
            cropLeft = value;
        } else if (key == QLatin1String("croptop")) {
            cropTop = value;
        } else if (key == QLatin1String("cropwidth")) {
            cropWidth = value;
        } else if (key == QLatin1String("cropheight")) {
            cropHeight = value;
        } else if (key == QLatin1String("temperature")) {
            metadata.temperatureText = value;
        } else if (key == QLatin1String("date-obs")) {
            metadata.dateObsText = value;
            std::uint64_t ticks = 0;
            if (parseIsoUtcTicks(value, ticks)) {
                header.dateTimeUtc = ticks;
                header.dateTimeLocal = ticks;
            }
        } else if (key == QLatin1String("sitelat")) {
            siteLat = value;
        } else if (key == QLatin1String("sitelong")) {
            siteLon = value;
        } else if (key == QLatin1String("object")) {
            metadata.objectName = value;
        } else if (key == QLatin1String("duration")) {
            metadata.durationText = value;
        }
    }

    if (!cropWidth.isEmpty() && !cropHeight.isEmpty()) {
        if (!cropLeft.isEmpty() && !cropTop.isEmpty()) {
            metadata.cropText = QStringLiteral("%1 × %2 @ (%3, %4)")
                                    .arg(cropWidth, cropHeight, cropLeft, cropTop);
        } else {
            metadata.cropText = QStringLiteral("%1 × %2").arg(cropWidth, cropHeight);
        }
    } else if (!startX.isEmpty() && !startY.isEmpty()
               && (startX != QLatin1String("0") || startY != QLatin1String("0"))) {
        metadata.cropText = QStringLiteral("ROI origin (%1, %2)").arg(startX, startY);
    }

    if (!siteLat.isEmpty() && !siteLon.isEmpty()) {
        metadata.siteLocationText = QStringLiteral("%1°, %2°").arg(siteLat, siteLon);
    } else if (!siteLat.isEmpty()) {
        metadata.siteLocationText = QStringLiteral("Lat %1°").arg(siteLat);
    } else if (!siteLon.isEmpty()) {
        metadata.siteLocationText = QStringLiteral("Lon %1°").arg(siteLon);
    }
}

int toDisplayValue(std::uint16_t value, std::uint32_t depth)
{
    if (depth <= 8U) {
        return static_cast<int>(value);
    }
    const std::uint32_t effectiveDepth = std::min(depth, 16U);
    const std::uint32_t maximum = (1U << effectiveDepth) - 1U;
    return static_cast<int>((static_cast<std::uint32_t>(value) * 255U + maximum / 2U) / maximum);
}

} // namespace

AviFile::~AviFile()
{
    close();
}

bool AviFile::open(const QString &path)
{
    close();

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        m_error = m_file.errorString();
        return false;
    }

    const qint64 signedSize = m_file.size();
    if (signedSize < 12) {
        m_file.close();
        m_error = QStringLiteral("The file is too small to be a valid AVI recording.");
        return false;
    }
    const std::uint64_t fileSize = static_cast<std::uint64_t>(signedSize);

    QByteArray initial;
    if (!readAt(m_file, fileSize, 0, 12, initial)) {
        m_file.close();
        m_error = QStringLiteral("Could not read the AVI RIFF header.");
        return false;
    }
    if (initial.left(4) != "RIFF" || initial.mid(8, 4) != "AVI ") {
        m_file.close();
        m_error = QStringLiteral("The file does not begin with a RIFF AVI header.");
        return false;
    }

    ParsedAviChunks parsed;
    std::uint64_t topPos = 0;
    while (topPos + 12ULL <= fileSize) {
        QByteArray riffHdr;
        if (!readAt(m_file, fileSize, topPos, 12, riffHdr)) break;
        const QByteArray chunkId = riffHdr.left(4);
        const std::uint32_t chunkSize = readU32(riffHdr, 4);
        const QByteArray formType = riffHdr.mid(8, 4);

        const std::uint64_t bodyStart = topPos + 12ULL;
        const std::uint64_t bodyEnd = chunkSize >= 4U
            ? std::min<std::uint64_t>(fileSize, topPos + 8ULL + static_cast<std::uint64_t>(chunkSize))
            : bodyStart;

        if (chunkId == "RIFF" && (formType == "AVI " || formType == "AVIX")) {
            if (formType == "AVIX") {
                parsed.hasAvix = true;
                parsed.openDml = true;
            }
            parseRiffBody(m_file, fileSize, bodyStart, bodyEnd,
                          formType == "AVI " && !parsed.hasVideoStream, parsed);
        }

        std::uint64_t nextTop = 0;
        if (!addChunkNext(topPos, chunkSize, nextTop) || nextTop <= topPos) break;
        topPos = nextTop;
    }

    if (!parsed.hasVideoStream || parsed.strfData.size() < 40) {
        m_file.close();
        m_error = QStringLiteral("No valid video stream format header was found in the AVI file.");
        return false;
    }

    const qint32 biWidthSigned = readI32(parsed.strfData, 4);
    const qint32 biHeightSigned = readI32(parsed.strfData, 8);
    const std::uint16_t biBitCount = readU16(parsed.strfData, 14);
    const QByteArray biCompressionBytes = parsed.strfData.mid(16, 4);
    const std::uint32_t biCompression = readU32(parsed.strfData, 16);

    const std::int64_t width64 = static_cast<std::int64_t>(biWidthSigned);
    const std::int64_t height64 = std::abs(static_cast<std::int64_t>(biHeightSigned));
    if (width64 <= 0 || height64 <= 0
        || width64 > std::numeric_limits<std::uint32_t>::max()
        || height64 > std::numeric_limits<std::uint32_t>::max()) {
        m_file.close();
        m_error = QStringLiteral("The AVI video stream declares invalid image dimensions.");
        return false;
    }

    const QString compUpper = fourCcDisplay(biCompressionBytes).toUpper();
    const bool isRgbOrDib = (biCompression == 0U)
                            || compUpper == QLatin1String("DIB ")
                            || compUpper == QLatin1String("RAW ");
    const bool isMono8 = compUpper == QLatin1String("Y800")
                         || compUpper == QLatin1String("Y8  ")
                         || compUpper == QLatin1String("GREY");
    const bool isMono16 = compUpper == QLatin1String("Y16 ")
                          || compUpper == QLatin1String("Y160");

    SerHeader candidate;
    AviMetadata meta;
    meta.openDml = parsed.openDml;
    if (biCompression != 0U) {
        meta.fourCc = fourCcDisplay(biCompressionBytes).trimmed();
    } else if (!fourCcDisplay(parsed.fccHandler).trimmed().isEmpty()) {
        meta.fourCc = fourCcDisplay(parsed.fccHandler).trimmed();
    } else {
        meta.fourCc = QStringLiteral("DIB");
    }
    if (meta.fourCc.isEmpty()) {
        meta.fourCc = QStringLiteral("DIB");
    }

    candidate.fileId = QStringLiteral("AVI (%1)").arg(meta.fourCc);
    candidate.width = static_cast<std::uint32_t>(width64);
    candidate.height = static_cast<std::uint32_t>(height64);
    candidate.endianFlag = 0U;

    std::uint32_t sourceBpp = 1U;
    bool stripAlpha = false;
    if ((isRgbOrDib || isMono8) && biBitCount == 8U) {
        candidate.pixelDepth = 8U;
        candidate.colorId = 0U;
        sourceBpp = 1U;
        meta.expectsSidecar = true;
        meta.topDown = isRgbOrDib ? (biHeightSigned < 0) : true;
    } else if ((isMono16 || (isRgbOrDib && biBitCount == 16U)) && biBitCount == 16U) {
        candidate.pixelDepth = 16U;
        candidate.colorId = 0U;
        sourceBpp = 2U;
        meta.expectsSidecar = true;
        meta.topDown = isRgbOrDib ? (biHeightSigned < 0) : true;
    } else if (isRgbOrDib && biBitCount == 24U) {
        candidate.pixelDepth = 8U;
        candidate.colorId = 101U; // BGR
        sourceBpp = 3U;
        meta.expectsSidecar = false;
        meta.topDown = (biHeightSigned < 0);
    } else if ((biCompression == 0U || compUpper == QLatin1String("DIB ")) && biBitCount == 32U) {
        candidate.pixelDepth = 8U;
        candidate.colorId = 101U; // BGRA stripped to BGR
        sourceBpp = 4U;
        stripAlpha = true;
        meta.expectsSidecar = false;
        meta.topDown = (biHeightSigned < 0);
    } else {
        m_file.close();
        m_error = QStringLiteral(
            "Unsupported AVI pixel format: codec '%1', %2 bits per pixel. "
            "Only uncompressed 8-bit/16-bit raw and 24-bit/32-bit RGB/BGR AVIs are supported.")
                      .arg(meta.fourCc)
                      .arg(biBitCount);
        return false;
    }

    if (parsed.strhScale > 0U && parsed.strhRate > 0U) {
        meta.frameRate = static_cast<double>(parsed.strhRate)
                         / static_cast<double>(parsed.strhScale);
    } else if (parsed.usPerFrame > 0U) {
        meta.frameRate = 1000000.0 / static_cast<double>(parsed.usPerFrame);
    }

    const double framePeriodTicks = (parsed.strhScale > 0U && parsed.strhRate > 0U)
        ? (static_cast<double>(TicksPerSecond) * static_cast<double>(parsed.strhScale)
           / static_cast<double>(parsed.strhRate))
        : (parsed.usPerFrame > 0U ? static_cast<double>(parsed.usPerFrame) * 10.0 : 0.0);

    const std::uint64_t packedRowBytes = static_cast<std::uint64_t>(candidate.width) * sourceBpp;
    const std::uint64_t minFrameBytes = packedRowBytes * candidate.height;
    if (minFrameBytes == 0ULL
        || minFrameBytes > static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())
        || candidate.bytesPerFrame() > static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())) {
        m_file.close();
        m_error = QStringLiteral("The AVI frame size is invalid or exceeds supported limits.");
        return false;
    }

    const QByteArray dbId = QStringLiteral("%1db")
                                .arg(parsed.videoStreamIndex, 2, 10, QLatin1Char('0'))
                                .toLatin1();
    const QByteArray dcId = QStringLiteral("%1dc")
                                .arg(parsed.videoStreamIndex, 2, 10, QLatin1Char('0'))
                                .toLatin1();

    QVector<FrameEntry> frames;

    // Priority 1: OpenDML indx super-index or standard index
    if (parsed.indxData.size() >= 24) {
        const std::uint16_t wLongsPerEntry = readU16(parsed.indxData, 0);
        const std::uint8_t bIndexSubType = static_cast<std::uint8_t>(parsed.indxData.at(2));
        const std::uint8_t bIndexType = static_cast<std::uint8_t>(parsed.indxData.at(3));
        const std::uint32_t nEntriesInUse = readU32(parsed.indxData, 4);
        const QByteArray indexChunkId = parsed.indxData.mid(8, 4);

        if (bIndexType == 0 && bIndexSubType == 0 && wLongsPerEntry >= 4
            && isVideoChunkId(indexChunkId, dbId, dcId)) {
            const qsizetype entryStride = static_cast<qsizetype>(wLongsPerEntry) * 4;
            const qsizetype maxEntries = (parsed.indxData.size() - 24) / entryStride;
            const qsizetype count = std::min<qsizetype>(
                static_cast<qsizetype>(nEntriesInUse), maxEntries);
            for (qsizetype i = 0; i < count; ++i) {
                const qsizetype off = 24 + i * entryStride;
                const std::uint64_t qwOffset = readU64(parsed.indxData, off);
                const std::uint32_t dwSize = readU32(parsed.indxData, off + 8);
                if (dwSize <= 8U) continue;
                QByteArray ixHdr;
                if (!readAt(m_file, fileSize, qwOffset, 8, ixHdr)) continue;
                const std::uint32_t ixPayloadSize = readU32(ixHdr, 4);
                const std::uint64_t readLen = std::min<std::uint64_t>(
                    ixPayloadSize, 64ULL * 1024ULL * 1024ULL);
                QByteArray ixPayload;
                if (qwOffset <= fileSize - 8ULL && readLen >= 24ULL
                    && readAt(m_file, fileSize, qwOffset + 8ULL, readLen, ixPayload)) {
                    parseStandardIndexPayload(ixPayload, fileSize, minFrameBytes,
                                              dbId, dcId, frames);
                }
            }
        } else if (bIndexType == 1) {
            parseStandardIndexPayload(parsed.indxData, fileSize, minFrameBytes,
                                      dbId, dcId, frames);
        }
    }

    // Priority 2: Legacy idx1 (only when OpenDML indx yielded no frames and no AVIX chunks exist)
    if (frames.isEmpty() && !parsed.hasAvix && parsed.hasIdx1 && parsed.idx1Size >= 16U) {
        QByteArray idx1Data;
        if (readAt(m_file, fileSize, parsed.idx1Offset, parsed.idx1Size, idx1Data)) {
            const qsizetype entryCount = idx1Data.size() / 16;
            qsizetype firstVideo = -1;
            for (qsizetype i = 0; i < entryCount; ++i) {
                const QByteArray cid = idx1Data.mid(i * 16, 4);
                if (isVideoChunkId(cid, dbId, dcId) && readU32(idx1Data, i * 16 + 12) >= minFrameBytes) {
                    firstVideo = i;
                    break;
                }
            }

            if (firstVideo >= 0) {
                const QByteArray firstCid = idx1Data.mid(firstVideo * 16, 4);
                const std::uint32_t firstOff = readU32(idx1Data, firstVideo * 16 + 8);
                const std::uint32_t firstLen = readU32(idx1Data, firstVideo * 16 + 12);

                QVector<std::uint64_t> candidateBases;
                if (!parsed.moviRanges.isEmpty()) {
                    candidateBases.append(parsed.moviRanges.constFirst().moviTypeOffset);
                }
                candidateBases.append(0ULL);
                if (!parsed.moviRanges.isEmpty()) {
                    candidateBases.append(parsed.moviRanges.constFirst().dataStart);
                }

                bool foundBase = false;
                std::uint64_t idxBase = 0;
                for (const std::uint64_t base : candidateBases) {
                    QByteArray probe;
                    if (base <= fileSize && firstOff <= fileSize - base
                        && readAt(m_file, fileSize, base + firstOff, 8, probe)
                        && probe.left(4) == firstCid
                        && readU32(probe, 4) == firstLen) {
                        idxBase = base;
                        foundBase = true;
                        break;
                    }
                }

                if (foundBase) {
                    frames.reserve(entryCount);
                    for (qsizetype i = 0; i < entryCount; ++i) {
                        const qsizetype off = i * 16;
                        const QByteArray cid = idx1Data.mid(off, 4);
                        if (!isVideoChunkId(cid, dbId, dcId)) continue;
                        const std::uint32_t chunkRelOff = readU32(idx1Data, off + 8);
                        const std::uint32_t chunkLen = readU32(idx1Data, off + 12);
                        if (static_cast<std::uint64_t>(chunkLen) < minFrameBytes) continue;
                        if (idxBase > fileSize || chunkRelOff > fileSize - idxBase
                            || fileSize - idxBase - chunkRelOff < 8ULL) {
                            continue;
                        }
                        const std::uint64_t dataOffset = idxBase + chunkRelOff + 8ULL;
                        if (static_cast<std::uint64_t>(chunkLen) > fileSize - dataOffset) {
                            continue;
                        }
                        frames.append({dataOffset, chunkLen});
                    }
                }
            }
        }
    }

    // Priority 3: Direct movi chunk scan fallback
    if (frames.isEmpty()) {
        for (const MoviRange &range : parsed.moviRanges) {
            scanMoviRange(m_file, fileSize, range.dataStart, range.dataEnd,
                          minFrameBytes, dbId, dcId, frames);
        }
    }

    if (frames.isEmpty()) {
        m_file.close();
        m_error = QStringLiteral("No readable uncompressed video frames were found in the AVI file.");
        return false;
    }

    const QString sidecar = findSidecarPath(path);
    if (!sidecar.isEmpty()) {
        parseSidecarFile(sidecar, candidate, meta);
    }

    candidate.frameCount = static_cast<std::uint32_t>(frames.size());

    m_path = QFileInfo(path).absoluteFilePath();
    m_header = candidate;
    m_metadata = meta;
    m_frames = std::move(frames);
    m_framePeriodTicks = framePeriodTicks;
    m_sourceBytesPerPixel = sourceBpp;
    m_stripAlpha = stripAlpha;
    m_error.clear();
    m_isOpen = true;
    return true;
}

void AviFile::close()
{
    QMutexLocker locker(&m_ioMutex);
    if (m_file.isOpen()) m_file.close();
    m_file.setFileName(QString());
    m_path.clear();
    m_header = {};
    m_metadata = {};
    m_error.clear();
    m_frames.clear();
    m_framePeriodTicks = 0.0;
    m_sourceBytesPerPixel = 1;
    m_stripAlpha = false;
    m_isOpen = false;
}

bool AviFile::isOpen() const { return m_isOpen; }
QString AviFile::path() const { return m_path; }
QString AviFile::displayName() const { return QFileInfo(m_path).fileName(); }
QString AviFile::sourceDirectory() const { return QFileInfo(m_path).absolutePath(); }
const SerHeader &AviFile::header() const { return m_header; }
const QString &AviFile::errorString() const { return m_error; }
const AviMetadata &AviFile::metadata() const { return m_metadata; }
bool AviFile::isMissingSidecar() const { return m_metadata.expectsSidecar && !m_metadata.hasSidecar; }

QImage AviFile::readFrame(std::uint32_t index) const
{
    return decodeFrame(readRawFrame(index));
}

QByteArray AviFile::readRawFrame(std::uint32_t index) const
{
    if (!m_isOpen || index >= static_cast<std::uint32_t>(m_frames.size())) {
        return {};
    }

    const FrameEntry entry = m_frames.at(static_cast<qsizetype>(index));
    QByteArray raw;
    {
        QMutexLocker locker(&m_ioMutex);
        if (!m_file.isOpen()) {
            m_file.setFileName(m_path);
            if (!m_file.open(QIODevice::ReadOnly)) return {};
        }
        if (!m_file.seek(static_cast<qint64>(entry.dataOffset))) {
            return {};
        }
        raw = m_file.read(static_cast<qint64>(entry.dataSize));
    }

    if (static_cast<std::uint32_t>(raw.size()) != entry.dataSize) {
        return {};
    }

    const std::uint64_t width = m_header.width;
    const std::uint64_t height = m_header.height;
    const std::uint64_t packedRowBytes = width * m_sourceBytesPerPixel;
    const std::uint64_t alignedRowBytes = (packedRowBytes + 3ULL) & ~3ULL;
    const std::uint64_t expectedPackedBytes = packedRowBytes * height;

    if (m_metadata.topDown && !m_stripAlpha
        && static_cast<std::uint64_t>(raw.size()) == expectedPackedBytes) {
        return raw;
    }

    const std::uint64_t minAlignedBytes = height > 0ULL
        ? alignedRowBytes * (height - 1ULL) + packedRowBytes
        : 0ULL;
    const std::uint64_t srcStride = static_cast<std::uint64_t>(raw.size()) >= minAlignedBytes
        ? alignedRowBytes : packedRowBytes;
    if (height == 0ULL
        || static_cast<std::uint64_t>(raw.size()) < srcStride * (height - 1ULL) + packedRowBytes) {
        return {};
    }

    const std::uint64_t outBytes = m_header.bytesPerFrame();
    const std::uint64_t outRowBytes = width
        * static_cast<std::uint64_t>(m_header.planeCount())
        * (m_header.pixelDepth <= 8U ? 1ULL : 2ULL);
    QByteArray out(static_cast<qsizetype>(outBytes), '\0');

    for (std::uint64_t y = 0; y < height; ++y) {
        const std::uint64_t srcY = m_metadata.topDown ? y : (height - 1ULL - y);
        const char *srcLine = raw.constData() + static_cast<qsizetype>(srcY * srcStride);
        char *dstLine = out.data() + static_cast<qsizetype>(y * outRowBytes);
        if (!m_stripAlpha) {
            std::memcpy(dstLine, srcLine, static_cast<std::size_t>(outRowBytes));
        } else {
            for (std::uint64_t x = 0; x < width; ++x) {
                const qsizetype s = static_cast<qsizetype>(x * 4ULL);
                const qsizetype d = static_cast<qsizetype>(x * 3ULL);
                dstLine[d] = srcLine[s];
                dstLine[d + 1] = srcLine[s + 1];
                dstLine[d + 2] = srcLine[s + 2];
            }
        }
    }
    return out;
}

QImage AviFile::decodeFrame(const QByteArray &rawFrame) const
{
    if (!m_isOpen || rawFrame.size() != static_cast<qsizetype>(m_header.bytesPerFrame())) {
        return {};
    }

    const int width = static_cast<int>(m_header.width);
    const int height = static_cast<int>(m_header.height);
    const bool bigEndian = m_header.endianFlag != 0U;

    if (m_header.planeCount() == 1) {
        QImage image(width, height, QImage::Format_Grayscale8);
        if (m_header.pixelDepth <= 8U) {
            const auto rowBytes = static_cast<std::size_t>(width);
            for (int y = 0; y < height; ++y) {
                std::memcpy(image.scanLine(y),
                            rawFrame.constData() + static_cast<qsizetype>(y) * width,
                            rowBytes);
            }
            return image;
        }
        const std::uint32_t effectiveDepth = std::min(m_header.pixelDepth, 16U);
        const std::uint32_t maximum = (1U << effectiveDepth) - 1U;
        const std::uint32_t halfMax = maximum / 2U;
        for (int y = 0; y < height; ++y) {
            uchar *line = image.scanLine(y);
            const auto *rowPtr = reinterpret_cast<const uchar *>(
                rawFrame.constData() + static_cast<qsizetype>(y) * width * 2);
            for (int x = 0; x < width; ++x) {
                const auto *samplePtr = rowPtr + static_cast<qsizetype>(x) * 2;
                const std::uint16_t val = bigEndian
                    ? qFromBigEndian<std::uint16_t>(samplePtr)
                    : qFromLittleEndian<std::uint16_t>(samplePtr);
                line[x] = static_cast<uchar>(
                    (static_cast<std::uint32_t>(val) * 255U + halfMax) / maximum);
            }
        }
        return image;
    }

    QImage image(width, height, QImage::Format_RGB888);
    const bool bgr = m_header.colorId == 101U;
    for (int y = 0; y < height; ++y) {
        uchar *line = image.scanLine(y);
        const auto *srcRow = reinterpret_cast<const uchar *>(
            rawFrame.constData() + static_cast<qsizetype>(y) * width * 3);
        for (int x = 0; x < width; ++x) {
            const qsizetype byte = static_cast<qsizetype>(x) * 3;
            const int first = toDisplayValue(srcRow[byte], m_header.pixelDepth);
            const int green = toDisplayValue(srcRow[byte + 1], m_header.pixelDepth);
            const int third = toDisplayValue(srcRow[byte + 2], m_header.pixelDepth);
            line[byte] = static_cast<uchar>(bgr ? third : first);
            line[byte + 1] = static_cast<uchar>(green);
            line[byte + 2] = static_cast<uchar>(bgr ? first : third);
        }
    }
    return image;
}

std::uint64_t AviFile::frameTimestamp(std::uint32_t index) const
{
    if (!m_isOpen || m_header.dateTimeUtc == 0ULL
        || index >= static_cast<std::uint32_t>(m_frames.size())) {
        return 0ULL;
    }
    if (m_framePeriodTicks <= 0.0) {
        return m_header.dateTimeUtc;
    }
    const double delta = static_cast<double>(index) * m_framePeriodTicks;
    return m_header.dateTimeUtc + static_cast<std::uint64_t>(std::llround(delta));
}

} // namespace seeingforge
