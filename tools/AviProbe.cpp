#include "core/AviFile.h"
#include "core/BayerPatternAnalyzer.h"
#include "core/HotPixelDetector.h"
#include "core/ObjectDetector.h"
#include "core/PreviewProcessor.h"
#include "core/QualityAnalyzer.h"
#include "core/SerExporter.h"
#include "core/SerFile.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    QTextStream error(stderr);

    if (application.arguments().size() < 2 || application.arguments().size() > 4) {
        error << "Usage: seeingforge_avi_probe <recording.avi> [preview.png] [export.ser]\n";
        return 2;
    }

    seeingforge::AviFile avi;
    if (!avi.open(application.arguments().at(1))) {
        error << "Error: " << avi.errorString() << '\n';
        return 1;
    }

    const auto &header = avi.header();
    const auto &meta = avi.metadata();
    output << "Identifier: " << header.fileId << '\n'
           << "Dimensions: " << header.width << " x " << header.height << '\n'
           << "Frames: " << header.frameCount << '\n'
           << "Pixel depth: " << header.pixelDepth << '\n'
           << "Colour: " << header.colorName() << " (colorId=" << header.colorId << ")\n"
           << "Frame bytes: " << header.bytesPerFrame() << '\n'
           << "FPS: " << QString::number(meta.frameRate, 'f', 4)
           << " | topDown=" << (meta.topDown ? "true" : "false")
           << " | openDml=" << (meta.openDml ? "true" : "false") << '\n'
           << "hasSidecar=" << (meta.hasSidecar ? "true" : "false")
           << " | missingSidecar=" << (avi.isMissingSidecar() ? "true" : "false") << '\n';
    if (meta.hasSidecar) {
        output << "Sidecar: " << QFileInfo(meta.sidecarPath).fileName()
               << " | Object=" << (meta.objectName.isEmpty() ? QStringLiteral("-") : meta.objectName)
               << " | Bayer=" << (meta.bayerText.isEmpty() ? QStringLiteral("-") : meta.bayerText)
               << " | DATE-OBS=" << (meta.dateObsText.isEmpty() ? QStringLiteral("-") : meta.dateObsText)
               << " | Temp=" << (meta.temperatureText.isEmpty() ? QStringLiteral("-") : meta.temperatureText)
               << '\n';
    }
    output << "Timestamp[0]=" << avi.frameTimestamp(0)
           << " Timestamp[last]=" << avi.frameTimestamp(header.frameCount - 1U) << '\n';

    const QImage firstFrame = avi.readFrame(0);
    if (firstFrame.isNull()) {
        error << "Error: the first frame could not be decoded.\n";
        return 1;
    }

    std::uint32_t effectiveColorId = header.colorId;
    if (avi.isMissingSidecar()) {
        const auto detected = seeingforge::BayerPatternAnalyzer::detectPattern(firstFrame);
        if (detected.discrepancy) {
            seeingforge::SerHeader suggested = header;
            suggested.colorId = detected.suggestedColorId;
            effectiveColorId = detected.suggestedColorId;
            output << "Missing sidecar auto-detected Bayer: " << suggested.colorName()
                   << " (confidence " << QString::number(detected.confidence * 100.0, 'f', 1)
                   << "%)\n";
        } else {
            output << "Missing sidecar: no Bayer pattern detected, keeping Monochrome\n";
        }
    } else if (header.colorId >= 8U && header.colorId <= 11U) {
        const auto bayerAnalysis = seeingforge::BayerPatternAnalyzer::analyze(
            firstFrame, header.colorId);
        if (bayerAnalysis.discrepancy) {
            seeingforge::SerHeader suggested = header;
            suggested.colorId = bayerAnalysis.suggestedColorId;
            output << "Probable Bayer metadata mismatch: " << header.colorName()
                   << " -> " << suggested.colorName() << " (confidence "
                   << QString::number(bayerAnalysis.confidence * 100.0, 'f', 1) << "%)\n";
        } else {
            output << "Bayer verification: matches " << header.colorName()
                   << " (confidence " << QString::number(bayerAnalysis.confidence * 100.0, 'f', 1)
                   << "%)\n";
        }
    }

    QElapsedTimer timer;
    timer.start();
    const auto previews = seeingforge::PreviewProcessor::render(
        firstFrame, effectiveColorId, seeingforge::PreviewSettings{});
    if (previews.processed.isNull()) {
        error << "Error: the processed preview could not be rendered.\n";
        return 1;
    }
    output << "Processed preview: " << previews.processed.width() << " x "
           << previews.processed.height() << " in " << timer.elapsed() << " ms\n";
    if (application.arguments().size() >= 3
        && !previews.processed.save(application.arguments().at(2))) {
        error << "Error: could not save preview image.\n";
        return 1;
    }

    const bool bayerMosaic = effectiveColorId >= 8U && effectiveColorId <= 19U;
    timer.restart();
    const auto detection = seeingforge::ObjectDetector::detect(
        firstFrame, 16, 20, 900, bayerMosaic);
    if (detection.found()) {
        output << "Detected object: " << detection.objectRect.width() << " x "
               << detection.objectRect.height() << " @ ("
               << detection.objectRect.x() << ", " << detection.objectRect.y() << "); crop: "
               << detection.cropRect.width() << " x " << detection.cropRect.height()
               << " in " << timer.elapsed() << " ms\n";
    } else {
        output << "Detected object: none at threshold 16\n";
    }

    timer.restart();
    const auto hotPixels = seeingforge::HotPixelDetector::detect(
        avi, seeingforge::HotPixelSettings{});
    output << "Hot pixels: " << hotPixels.hotPixels.size()
           << " from " << hotPixels.initialCandidates
           << " initial candidates across " << hotPixels.sampledFrames
           << " frames in " << timer.elapsed() << " ms\n";

    timer.restart();
    const auto quality = seeingforge::QualityAnalyzer::analyze(
        avi, 16, {}, {}, effectiveColorId);
    output << "Quality analyzed " << quality.frames.size() << " frames in "
           << timer.elapsed() << " ms\n";

    QVector<qsizetype> posByFrame(static_cast<qsizetype>(header.frameCount), -1);
    for (qsizetype i = 0; i < quality.frames.size(); ++i) {
        const std::uint32_t fIdx = quality.frames.at(i).frameIndex;
        if (fIdx < header.frameCount) {
            posByFrame[static_cast<qsizetype>(fIdx)] = i;
        }
    }

    int clippedOver5 = 0;
    int obscuredBelow80 = 0;
    QVector<std::uint32_t> eligibleRanked;
    for (const std::uint32_t idx : quality.rankedFrameIndices) {
        const qsizetype qPos = idx < header.frameCount
            ? posByFrame.at(static_cast<qsizetype>(idx)) : -1;
        if (qPos < 0) continue;
        const auto &fq = quality.frames.at(qPos);
        const bool clipped = fq.clippedPercent > 5.0;
        const bool obscured = fq.targetCoverage * 100.0 < 80.0;
        if (clipped) ++clippedOver5;
        if (obscured) ++obscuredBelow80;
        if (!clipped && !obscured) eligibleRanked.append(idx);
    }
    output << "Clipped (>5%): " << clippedOver5
           << " | Obscured (<80% signal): " << obscuredBelow80
           << " | Eligible: " << eligibleRanked.size() << '\n';

    const int topShow = std::min<int>(5, static_cast<int>(quality.rankedFrameIndices.size()));
    output << "Top " << topShow << " ranked frames:\n";
    for (int i = 0; i < topShow; ++i) {
        const auto idx = quality.rankedFrameIndices.at(i);
        const qsizetype qPos = posByFrame.at(static_cast<qsizetype>(idx));
        const auto &fq = quality.frames.at(qPos);
        output << "  #" << (idx + 1U) << ": score=" << QString::number(fq.score, 'f', 1)
               << " cov=" << QString::number(fq.targetCoverage * 100.0, 'f', 1) << "%"
               << " sharp=" << QString::number(fq.sharpness, 'f', 4)
               << " clip=" << QString::number(fq.clippedPercent, 'f', 2) << "%\n";
    }
    const int totalRanked = static_cast<int>(quality.rankedFrameIndices.size());
    const int botShow = std::min<int>(5, totalRanked);
    output << "Bottom " << botShow << " ranked frames:\n";
    for (int i = std::max(0, totalRanked - botShow); i < totalRanked; ++i) {
        const auto idx = quality.rankedFrameIndices.at(i);
        const qsizetype qPos = posByFrame.at(static_cast<qsizetype>(idx));
        const auto &fq = quality.frames.at(qPos);
        output << "  #" << (idx + 1U) << ": score=" << QString::number(fq.score, 'f', 1)
               << " cov=" << QString::number(fq.targetCoverage * 100.0, 'f', 1) << "%"
               << " sharp=" << QString::number(fq.sharpness, 'f', 4)
               << " clip=" << QString::number(fq.clippedPercent, 'f', 2) << "%\n";
    }

    if (application.arguments().size() == 4) {
        QVector<std::uint32_t> exportIndices;
        const int keepCount = eligibleRanked.isEmpty() ? 0 : std::clamp(
            static_cast<int>(std::ceil(eligibleRanked.size() * 0.25)),
            1, static_cast<int>(eligibleRanked.size()));
        for (int i = 0; i < keepCount; ++i) {
            exportIndices.append(eligibleRanked.at(i));
        }
        std::sort(exportIndices.begin(), exportIndices.end());

        seeingforge::SerExportSettings exportSettings;
        exportSettings.frameIndices = exportIndices;
        exportSettings.centerObject = true;
        exportSettings.sourceColorId = effectiveColorId;
        exportSettings.detectionThreshold = 16;
        if (detection.found()) {
            const int w = std::min<int>(
                static_cast<int>(header.width),
                ((detection.cropRect.width() + 3) & ~3));
            const int h = std::min<int>(
                static_cast<int>(header.height),
                ((detection.cropRect.height() + 3) & ~3));
            exportSettings.outputSize = QSize(std::max(4, w), std::max(4, h));
        }
        for (const auto &fq : quality.frames) {
            if (fq.objectRect.isValid()) {
                exportSettings.cachedObjectRects.insert(fq.frameIndex, fq.objectRect);
            }
        }

        timer.restart();
        const auto exportResult = seeingforge::SerExporter::exportFrames(
            avi, application.arguments().at(3), exportSettings);
        if (!exportResult.success) {
            error << "Export error: " << exportResult.error << '\n';
            return 1;
        }
        output << "Exported " << exportResult.exportedFrames << " frames to "
               << exportResult.outputPath << " in " << timer.elapsed() << " ms"
               << " (timestampsPreserved="
               << (exportResult.timestampsPreserved ? "true" : "false") << ")\n";

        seeingforge::SerFile verifySer;
        if (!verifySer.open(application.arguments().at(3))) {
            error << "SER verification error: " << verifySer.errorString() << '\n';
            return 1;
        }
        output << "Verified exported SER: " << verifySer.header().width << " x "
               << verifySer.header().height << ", " << verifySer.header().frameCount
               << " frames, " << verifySer.header().colorName()
               << ", ts[0]=" << verifySer.frameTimestamp(0) << '\n';
    }

    return 0;
}
