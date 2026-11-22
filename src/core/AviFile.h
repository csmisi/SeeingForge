#pragma once

#include "core/FrameSource.h"
#include "core/SerFile.h"

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QVector>

#include <cstdint>

namespace seeingforge {

struct AviMetadata {
    QString fourCc;
    double frameRate = 0.0;
    bool topDown = true;
    bool openDml = false;
    bool hasSidecar = false;
    bool expectsSidecar = false;
    QString sidecarPath;
    QString sensorSection;
    QString colorFormatText;
    QString bayerText;
    QString flipText;
    QString captureAreaText;
    QString cropText;
    QString temperatureText;
    QString dateObsText;
    QString siteLocationText;
    QString objectName;
    QString durationText;
};

class AviFile final : public FrameSource {
public:
    ~AviFile() override;

    [[nodiscard]] bool open(const QString &path);
    void close();

    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] QString path() const override;
    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] QString sourceDirectory() const override;
    [[nodiscard]] const SerHeader &header() const override;
    [[nodiscard]] const QString &errorString() const;
    [[nodiscard]] const AviMetadata &metadata() const;
    [[nodiscard]] bool isMissingSidecar() const;

    [[nodiscard]] QImage readFrame(std::uint32_t index) const override;
    [[nodiscard]] QByteArray readRawFrame(std::uint32_t index) const override;
    [[nodiscard]] QImage decodeFrame(const QByteArray &rawFrame) const override;
    [[nodiscard]] std::uint64_t frameTimestamp(std::uint32_t index) const override;

private:
    struct FrameEntry {
        std::uint64_t dataOffset = 0;
        std::uint32_t dataSize = 0;
    };

    QString m_path;
    SerHeader m_header;
    AviMetadata m_metadata;
    QString m_error;
    mutable QFile m_file;
    mutable QMutex m_ioMutex;
    QVector<FrameEntry> m_frames;
    double m_framePeriodTicks = 0.0;
    std::uint32_t m_sourceBytesPerPixel = 1;
    bool m_stripAlpha = false;
    bool m_isOpen = false;
};

} // namespace seeingforge
