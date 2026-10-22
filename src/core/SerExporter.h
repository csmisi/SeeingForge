#pragma once

#include <QString>
#include <QSize>
#include <QVector>

#include <cstdint>
#include <functional>

namespace seeingforge {

class SerFile;

struct ExportResult {
    bool success = false;
    bool canceled = false;
    QString outputPath;
    QString error;
    int exportedFrames = 0;
    bool timestampsPreserved = false;
};

struct SerExportSettings {
    QVector<std::uint32_t> frameIndices;
    bool centerObject = true;
    bool monochrome = false;
    bool binMonochrome = false;
    int detectionThreshold = 12;
    QSize outputSize;
    int rotationQuarterTurns = 0;
    bool flipHorizontal = false;
    bool flipVertical = false;
};

class SerExporter {
public:
    [[nodiscard]] static ExportResult exportFrames(
        const SerFile &source, const QString &outputPath,
        const SerExportSettings &settings,
        const std::function<void(int, int)> &progress = {},
        const std::function<bool()> &shouldCancel = {});
};

} // namespace seeingforge
