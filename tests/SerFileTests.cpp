#include "core/SerFile.h"
#include "core/PreviewProcessor.h"
#include "core/ObjectDetector.h"
#include "core/HotPixelDetector.h"
#include "core/QualityAnalyzer.h"
#include "core/SerExporter.h"
#include "core/BayerPatternAnalyzer.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

namespace {

void appendU32(QByteArray &data, quint32 value)
{
    const qsizetype offset = data.size();
    data.resize(offset + 4);
    qToLittleEndian(value, reinterpret_cast<uchar *>(data.data() + offset));
}

void appendU64(QByteArray &data, quint64 value)
{
    const qsizetype offset = data.size();
    data.resize(offset + 8);
    qToLittleEndian(value, reinterpret_cast<uchar *>(data.data() + offset));
}

void appendFixedText(QByteArray &data, const QByteArray &text, int size)
{
    QByteArray field(size, '\0');
    const qsizetype count = std::min(field.size(), text.size());
    std::copy_n(text.constData(), count, field.data());
    data.append(field);
}

QByteArray monoSer(const QByteArray &identifier = "LUCAM-RECORDER")
{
    QByteArray data;
    appendFixedText(data, identifier, 14);
    appendU32(data, 42);
    appendU32(data, 0);
    appendU32(data, 0);
    appendU32(data, 2);
    appendU32(data, 2);
    appendU32(data, 8);
    appendU32(data, 1);
    appendFixedText(data, "Observer", 40);
    appendFixedText(data, "Camera", 40);
    appendFixedText(data, "Telescope", 40);
    appendU64(data, 0);
    appendU64(data, 0);
    data.append(QByteArray::fromRawData("\x00\x40\x80\xff", 4));
    return data;
}

QByteArray hotPixelSer()
{
    QByteArray data = monoSer();
    data.truncate(static_cast<qsizetype>(seeingforge::SerFile::HeaderSize));
    qToLittleEndian<quint32>(8, reinterpret_cast<uchar *>(data.data() + 26));
    qToLittleEndian<quint32>(8, reinterpret_cast<uchar *>(data.data() + 30));
    qToLittleEndian<quint32>(4, reinterpret_cast<uchar *>(data.data() + 38));
    for (int frameIndex = 0; frameIndex < 4; ++frameIndex) {
        QByteArray frame(64, static_cast<char>(10));
        frame[4 * 8 + 4] = static_cast<char>(250);
        data.append(frame);
    }
    return data;
}

QByteArray movingObjectSer()
{
    QByteArray data = monoSer();
    data.truncate(static_cast<qsizetype>(seeingforge::SerFile::HeaderSize));
    qToLittleEndian<quint32>(8, reinterpret_cast<uchar *>(data.data() + 26));
    qToLittleEndian<quint32>(8, reinterpret_cast<uchar *>(data.data() + 30));
    qToLittleEndian<quint32>(2, reinterpret_cast<uchar *>(data.data() + 38));
    for (int frameIndex = 0; frameIndex < 2; ++frameIndex) {
        QByteArray frame(64, static_cast<char>(5));
        const int origin = frameIndex == 0 ? 0 : 5;
        for (int y = origin; y < origin + 3; ++y)
            for (int x = origin; x < origin + 3; ++x)
                frame[y * 8 + x] = static_cast<char>(220);
        data.append(frame);
    }
    appendU64(data, 1000);
    appendU64(data, 2000);
    return data;
}

QByteArray rgbSer()
{
    QByteArray data = monoSer();
    data.truncate(static_cast<qsizetype>(seeingforge::SerFile::HeaderSize));
    qToLittleEndian<quint32>(100, reinterpret_cast<uchar *>(data.data() + 18));
    qToLittleEndian<quint32>(2, reinterpret_cast<uchar *>(data.data() + 26));
    qToLittleEndian<quint32>(2, reinterpret_cast<uchar *>(data.data() + 30));
    qToLittleEndian<quint32>(1, reinterpret_cast<uchar *>(data.data() + 38));
    for (int pixel = 0; pixel < 4; ++pixel) {
        data.append(static_cast<char>(100));
        data.append(static_cast<char>(150));
        data.append(static_cast<char>(200));
    }
    return data;
}

QByteArray bayerSer()
{
    QByteArray data = monoSer();
    data.truncate(static_cast<qsizetype>(seeingforge::SerFile::HeaderSize));
    qToLittleEndian<quint32>(8, reinterpret_cast<uchar *>(data.data() + 18));
    qToLittleEndian<quint32>(4, reinterpret_cast<uchar *>(data.data() + 26));
    qToLittleEndian<quint32>(4, reinterpret_cast<uchar *>(data.data() + 30));
    qToLittleEndian<quint32>(1, reinterpret_cast<uchar *>(data.data() + 38));
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int value = (y & 1) == 0 && (x & 1) == 0 ? 200
                : ((y & 1) != 0 && (x & 1) != 0 ? 0 : 100);
            data.append(static_cast<char>(value));
        }
    }
    return data;
}

} // namespace

class SerFileTests final : public QObject {
    Q_OBJECT

private slots:
    void readsHeaderAndFirstFrame();
    void acceptsIndiRecorderIdentifier();
    void debayersRggbPreview();
    void adjustsPreviewSaturationAndHue();
    void autoStretchPreservesCompactPlanetHighlights();
    void whiteBalanceIgnoresColouredBackgroundNoise();
    void measuresSharperFrameHigher();
    void exportsSelectedFramesCentered();
    void padsExportDimensionsToMultiplesOfFour();
    void padsBinnedExportDimensionsToMultiplesOfFour();
    void appliesExportIntensityGain();
    void exportsRgbAsMonochrome();
    void exportsBayerAsBinnedMonochrome();
    void rotatesBayerExportAndRemapsPattern();
    void detectsProbableRoiBayerMismatch();
    void calculatesHighlightSafeNeutralBalance();
    void exportsUsingSelectedBayerPattern();
    void appliesBayerChannelGains();
    void cancelsSlowOperationsCleanly();
    void detectsObjectAndAddsCropMargin();
    void detectsPersistentHotPixel();
    void rejectsTruncatedFile();
    void cachesObjectRectDuringQualityAndReusesInExport();
    void appliesPostCropGainAndChannelLutWithPadding();
    void parallelQualityAndExportPreserveOrder();
};

void SerFileTests::readsHeaderAndFirstFrame()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("sample.ser"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(monoSer()), 182);
    file.close();

    seeingforge::SerFile ser;
    QVERIFY2(ser.open(path), qPrintable(ser.errorString()));
    QCOMPARE(ser.header().width, 2U);
    QCOMPARE(ser.header().height, 2U);
    QCOMPARE(ser.header().frameCount, 1U);
    QCOMPARE(ser.header().observer, QStringLiteral("Observer"));

    const QImage image = ser.readFrame(0);
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(2, 2));
    QCOMPARE(qGray(image.pixel(0, 0)), 0);
    QCOMPARE(qGray(image.pixel(1, 1)), 255);
}

void SerFileTests::acceptsIndiRecorderIdentifier()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("indi.ser"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(monoSer("INDI-RECORDER"));
    file.close();

    seeingforge::SerFile ser;
    QVERIFY2(ser.open(path), qPrintable(ser.errorString()));
    QCOMPARE(ser.header().fileId, QStringLiteral("INDI-RECORDER"));
}

void SerFileTests::debayersRggbPreview()
{
    QImage raw(4, 4, QImage::Format_Grayscale8);
    for (int y = 0; y < raw.height(); ++y) {
        uchar *line = raw.scanLine(y);
        for (int x = 0; x < raw.width(); ++x) {
            if ((y & 1) == 0 && (x & 1) == 0) line[x] = 200;
            else if ((y & 1) != 0 && (x & 1) != 0) line[x] = 50;
            else line[x] = 100;
        }
    }
    seeingforge::PreviewSettings settings;
    settings.autoStretch = false;
    settings.whiteBalance = false;
    const auto previews = seeingforge::PreviewProcessor::render(raw, 8, settings);
    QVERIFY(!previews.processed.isNull());
    const QColor center = previews.processed.pixelColor(1, 1);
    QCOMPARE(center.red(), 200);
    QCOMPARE(center.green(), 100);
    QCOMPARE(center.blue(), 50);
}

void SerFileTests::adjustsPreviewSaturationAndHue()
{
    QImage raw(4, 4, QImage::Format_Grayscale8);
    for (int y = 0; y < raw.height(); ++y) {
        uchar *line = raw.scanLine(y);
        for (int x = 0; x < raw.width(); ++x) {
            if ((y & 1) == 0 && (x & 1) == 0) line[x] = 200;
            else if ((y & 1) != 0 && (x & 1) != 0) line[x] = 50;
            else line[x] = 100;
        }
    }
    seeingforge::PreviewSettings settings;
    settings.autoStretch = false;
    settings.whiteBalance = false;
    settings.saturation = 0.0;
    const QColor monochrome = seeingforge::PreviewProcessor::render(raw, 8, settings)
                                  .processed.pixelColor(1, 1);
    QCOMPARE(monochrome.red(), monochrome.green());
    QCOMPARE(monochrome.green(), monochrome.blue());

    settings.saturation = 1.0;
    settings.hueShiftDegrees = 120;
    const QColor shifted = seeingforge::PreviewProcessor::render(raw, 8, settings)
                               .processed.pixelColor(1, 1);
    QVERIFY(shifted.green() > shifted.red());
    QVERIFY(shifted.green() > shifted.blue());
}

void SerFileTests::autoStretchPreservesCompactPlanetHighlights()
{
    QImage raw(512, 512, QImage::Format_Grayscale8);
    raw.fill(4);
    for (int y = 250; y < 258; ++y) {
        uchar *line = raw.scanLine(y);
        for (int x = 240; x < 272; ++x)
            line[x] = static_cast<uchar>(40 + (x - 240) * 6);
    }

    seeingforge::PreviewSettings settings;
    settings.debayer = false;
    settings.autoStretch = true;
    const QImage preview = seeingforge::PreviewProcessor::render(raw, 0, settings).processed;

    QVERIFY(qGray(preview.pixel(240, 252)) < 80);
    QVERIFY(qGray(preview.pixel(255, 252)) > 100);
    QVERIFY(qGray(preview.pixel(271, 252)) > 220);
}

void SerFileTests::whiteBalanceIgnoresColouredBackgroundNoise()
{
    QImage raw(256, 256, QImage::Format_Grayscale8);
    for (int y = 0; y < raw.height(); ++y) {
        uchar *line = raw.scanLine(y);
        for (int x = 0; x < raw.width(); ++x) {
            const bool red = (y & 1) == 0 && (x & 1) == 0;
            const bool blue = (y & 1) != 0 && (x & 1) != 0;
            line[x] = static_cast<uchar>(red ? 12 : (blue ? 4 : 8));
        }
    }
    for (int y = 120; y < 136; ++y) {
        uchar *line = raw.scanLine(y);
        for (int x = 120; x < 136; ++x)
            line[x] = 180;
    }

    seeingforge::PreviewSettings settings;
    settings.autoStretch = false;
    settings.whiteBalance = true;
    const QColor background = seeingforge::PreviewProcessor::render(raw, 8, settings)
                                  .processed.pixelColor(32, 32);

    QVERIFY(background.red() < 30);
    QVERIFY(background.blue() < 30);
}

void SerFileTests::measuresSharperFrameHigher()
{
    QImage sharp(64, 64, QImage::Format_Grayscale8);
    for (int y = 0; y < sharp.height(); ++y) {
        uchar *line = sharp.scanLine(y);
        for (int x = 0; x < sharp.width(); ++x)
            line[x] = ((x / 8 + y / 8) & 1) != 0 ? 220 : 30;
    }
    const QImage soft = sharp.scaled(16, 16, Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation)
                              .scaled(64, 64, Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation);
    const auto sharpMetrics = seeingforge::QualityAnalyzer::measureFrame(sharp, 0);
    const auto softMetrics = seeingforge::QualityAnalyzer::measureFrame(soft, 1);
    QVERIFY(sharpMetrics.sharpness > softMetrics.sharpness);
}

void SerFileTests::exportsSelectedFramesCentered()
{
    QTemporaryDir directory;
    const QString inputPath = directory.filePath(QStringLiteral("moving.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(movingObjectSer());
    input.close();

    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));
    seeingforge::SerExportSettings settings;
    settings.frameIndices = {1, 0};
    settings.centerObject = true;
    settings.detectionThreshold = 100;
    settings.outputSize = QSize(4, 4);
    const QString outputPath = directory.filePath(QStringLiteral("centered.ser"));
    const auto exportResult = seeingforge::SerExporter::exportFrames(
        source, outputPath, settings);
    QVERIFY2(exportResult.success, qPrintable(exportResult.error));
    QVERIFY(exportResult.timestampsPreserved);

    seeingforge::SerFile output;
    QVERIFY2(output.open(outputPath), qPrintable(output.errorString()));
    QCOMPARE(output.header().width, 4U);
    QCOMPARE(output.header().height, 4U);
    QCOMPARE(output.header().frameCount, 2U);
    QCOMPARE(qGray(output.readFrame(0).pixel(2, 2)), 220);
    QCOMPARE(qGray(output.readFrame(1).pixel(2, 2)), 220);
}

void SerFileTests::padsExportDimensionsToMultiplesOfFour()
{
    QTemporaryDir directory;
    const QString inputPath = directory.filePath(QStringLiteral("moving.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(movingObjectSer());
    input.close();

    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));
    seeingforge::SerExportSettings settings;
    settings.frameIndices = {0};
    settings.centerObject = false;
    settings.outputSize = QSize(6, 6);
    const QString outputPath = directory.filePath(QStringLiteral("aligned.ser"));
    const auto result = seeingforge::SerExporter::exportFrames(source, outputPath, settings);
    QVERIFY2(result.success, qPrintable(result.error));

    seeingforge::SerFile output;
    QVERIFY2(output.open(outputPath), qPrintable(output.errorString()));
    QCOMPARE(output.header().width, 8U);
    QCOMPARE(output.header().height, 8U);
}

void SerFileTests::padsBinnedExportDimensionsToMultiplesOfFour()
{
    QTemporaryDir directory;
    const QString inputPath = directory.filePath(QStringLiteral("bayer.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(bayerSer());
    input.close();

    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));
    seeingforge::SerExportSettings settings;
    settings.frameIndices = {0};
    settings.centerObject = false;
    settings.monochrome = true;
    settings.binMonochrome = true;
    settings.outputSize = QSize(6, 6);
    const QString outputPath = directory.filePath(QStringLiteral("aligned-binned.ser"));
    const auto result = seeingforge::SerExporter::exportFrames(source, outputPath, settings);
    QVERIFY2(result.success, qPrintable(result.error));

    seeingforge::SerFile output;
    QVERIFY2(output.open(outputPath), qPrintable(output.errorString()));
    QCOMPARE(output.header().width, 4U);
    QCOMPARE(output.header().height, 4U);
}

void SerFileTests::appliesExportIntensityGain()
{
    QTemporaryDir directory;
    const QString inputPath = directory.filePath(QStringLiteral("input.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(monoSer());
    input.close();

    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));
    seeingforge::SerExportSettings settings;
    settings.frameIndices = {0};
    settings.centerObject = false;
    settings.outputSize = QSize(2, 2);
    settings.intensityGain = 2.0;
    const QString outputPath = directory.filePath(QStringLiteral("brighter.ser"));
    const auto exportResult = seeingforge::SerExporter::exportFrames(
        source, outputPath, settings);
    QVERIFY2(exportResult.success, qPrintable(exportResult.error));

    seeingforge::SerFile output;
    QVERIFY2(output.open(outputPath), qPrintable(output.errorString()));
    const QImage frame = output.readFrame(0);
    QCOMPARE(output.header().width, 4U);
    QCOMPARE(output.header().height, 4U);
    QCOMPARE(qGray(frame.pixel(1, 1)), 0);
    QCOMPARE(qGray(frame.pixel(2, 1)), 128);
    QCOMPARE(qGray(frame.pixel(1, 2)), 255);
    QCOMPARE(qGray(frame.pixel(2, 2)), 255);
}

void SerFileTests::exportsRgbAsMonochrome()
{
    QTemporaryDir directory;
    const QString inputPath = directory.filePath(QStringLiteral("rgb.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(rgbSer());
    input.close();

    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));
    seeingforge::SerExportSettings settings;
    settings.frameIndices = {0};
    settings.centerObject = false;
    settings.monochrome = true;
    settings.outputSize = QSize(2, 2);
    const QString outputPath = directory.filePath(QStringLiteral("mono.ser"));
    const auto exportResult = seeingforge::SerExporter::exportFrames(
        source, outputPath, settings);
    QVERIFY2(exportResult.success, qPrintable(exportResult.error));

    seeingforge::SerFile output;
    QVERIFY2(output.open(outputPath), qPrintable(output.errorString()));
    QCOMPARE(output.header().colorId, 0U);
    QCOMPARE(output.header().width, 4U);
    QCOMPARE(output.header().height, 4U);
    QCOMPARE(qGray(output.readFrame(0).pixel(1, 1)), 143);
}

void SerFileTests::exportsBayerAsBinnedMonochrome()
{
    QTemporaryDir directory;
    const QString inputPath = directory.filePath(QStringLiteral("bayer.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(bayerSer());
    input.close();

    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));
    seeingforge::SerExportSettings settings;
    settings.frameIndices = {0};
    settings.centerObject = false;
    settings.monochrome = true;
    settings.outputSize = QSize(4, 4);
    const QString outputPath = directory.filePath(QStringLiteral("bayer-mono.ser"));
    const auto fullResult = seeingforge::SerExporter::exportFrames(
        source, outputPath, settings);
    QVERIFY2(fullResult.success, qPrintable(fullResult.error));
    seeingforge::SerFile fullOutput;
    QVERIFY(fullOutput.open(outputPath));
    QCOMPARE(fullOutput.header().colorId, 0U);
    QCOMPARE(fullOutput.header().width, 4U);
    QCOMPARE(fullOutput.header().height, 4U);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            QCOMPARE(qGray(fullOutput.readFrame(0).pixel(x, y)), 114);
    settings.binMonochrome = true;
    const auto exportResult = seeingforge::SerExporter::exportFrames(
        source, outputPath, settings);
    QVERIFY2(exportResult.success, qPrintable(exportResult.error));

    seeingforge::SerFile output;
    QVERIFY2(output.open(outputPath), qPrintable(output.errorString()));
    QCOMPARE(output.header().colorId, 0U);
    QCOMPARE(output.header().width, 4U);
    QCOMPARE(output.header().height, 4U);
    QCOMPARE(qGray(output.readFrame(0).pixel(1, 1)), 100);
}

void SerFileTests::rotatesBayerExportAndRemapsPattern()
{
    QTemporaryDir directory;
    const QString inputPath = directory.filePath(QStringLiteral("bayer.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(bayerSer());
    input.close();

    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));
    seeingforge::SerExportSettings settings;
    settings.frameIndices = {0};
    settings.centerObject = false;
    settings.outputSize = QSize(4, 4);
    settings.rotationQuarterTurns = 1;
    const QString outputPath = directory.filePath(QStringLiteral("rotated-bayer.ser"));
    const auto exportResult = seeingforge::SerExporter::exportFrames(
        source, outputPath, settings);
    QVERIFY2(exportResult.success, qPrintable(exportResult.error));

    seeingforge::SerFile output;
    QVERIFY2(output.open(outputPath), qPrintable(output.errorString()));
    QCOMPARE(output.header().colorId, 9U); // RGGB rotated clockwise becomes GRBG.
    QCOMPARE(output.header().width, 4U);
    QCOMPARE(output.header().height, 4U);
}

void SerFileTests::detectsProbableRoiBayerMismatch()
{
    QImage raw(128, 128, QImage::Format_Grayscale8);
    for (int y = 0; y < raw.height(); ++y) {
        uchar *line = raw.scanLine(y);
        for (int x = 0; x < raw.width(); ++x) {
            const int gradient = (x + y) / 16;
            if ((y & 1) == 0 && (x & 1) == 0) line[x] = static_cast<uchar>(100 + gradient);
            else if ((y & 1) != 0 && (x & 1) != 0) line[x] = static_cast<uchar>(100 + gradient);
            else if ((y & 1) != 0) line[x] = static_cast<uchar>(180 + gradient);
            else line[x] = static_cast<uchar>(50 + gradient);
        }
    }

    const auto analysis = seeingforge::BayerPatternAnalyzer::analyze(raw, 8);
    QVERIFY(analysis.discrepancy);
    QCOMPARE(analysis.suggestedColorId, 10U);
    QVERIFY(analysis.confidence > 0.35);

    const auto matching = seeingforge::BayerPatternAnalyzer::analyze(raw, 10);
    QVERIFY(!matching.discrepancy);
    QCOMPARE(matching.suggestedColorId, 10U);
}

void SerFileTests::calculatesHighlightSafeNeutralBalance()
{
    QImage raw(32, 32, QImage::Format_Grayscale8);
    for (int y = 0; y < raw.height(); ++y) {
        uchar *line = raw.scanLine(y);
        for (int x = 0; x < raw.width(); ++x) {
            if ((y & 1) == 0 && (x & 1) == 0) line[x] = 200;
            else if ((y & 1) != 0 && (x & 1) != 0) line[x] = 50;
            else line[x] = 100;
        }
    }
    const auto gains = seeingforge::BayerPatternAnalyzer::neutralBalanceGains(raw, 8);
    QCOMPARE(gains[0], 0.25);
    QCOMPARE(gains[1], 0.5);
    QCOMPARE(gains[2], 1.0);
}

void SerFileTests::exportsUsingSelectedBayerPattern()
{
    QTemporaryDir directory;
    const QString inputPath = directory.filePath(QStringLiteral("bayer-override.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(bayerSer());
    input.close();

    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));
    seeingforge::SerExportSettings settings;
    settings.frameIndices = {0};
    settings.centerObject = false;
    settings.outputSize = QSize(4, 4);
    settings.sourceColorId = 10;
    settings.rotationQuarterTurns = 3;
    const QString outputPath = directory.filePath(QStringLiteral("corrected-bayer.ser"));
    const auto exportResult = seeingforge::SerExporter::exportFrames(
        source, outputPath, settings);
    QVERIFY2(exportResult.success, qPrintable(exportResult.error));

    seeingforge::SerFile output;
    QVERIFY2(output.open(outputPath), qPrintable(output.errorString()));
    QCOMPARE(output.header().colorId, 11U); // GBRG rotated counter-clockwise becomes BGGR.
}

void SerFileTests::appliesBayerChannelGains()
{
    QTemporaryDir directory;
    const QString inputPath = directory.filePath(QStringLiteral("bayer-gains.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(bayerSer());
    input.close();

    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));
    seeingforge::SerExportSettings settings;
    settings.frameIndices = {0};
    settings.centerObject = false;
    settings.outputSize = QSize(4, 4);
    settings.bayerChannelGains = {0.25, 0.5, 1.0};
    const QString outputPath = directory.filePath(QStringLiteral("balanced.ser"));
    const auto result = seeingforge::SerExporter::exportFrames(source, outputPath, settings);
    QVERIFY2(result.success, qPrintable(result.error));

    seeingforge::SerFile output;
    QVERIFY(output.open(outputPath));
    const QByteArray frame = output.readRawFrame(0);
    QCOMPARE(static_cast<uchar>(frame[0]), static_cast<uchar>(50));
    QCOMPARE(static_cast<uchar>(frame[1]), static_cast<uchar>(50));
    QCOMPARE(static_cast<uchar>(frame[5]), static_cast<uchar>(0));
}

void SerFileTests::cancelsSlowOperationsCleanly()
{
    QTemporaryDir directory;
    const QString inputPath = directory.filePath(QStringLiteral("cancel-source.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(movingObjectSer());
    input.close();
    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));

    const auto quality = seeingforge::QualityAnalyzer::analyze(
        source, 12, {}, [] { return true; });
    QVERIFY(quality.canceled);

    seeingforge::SerExportSettings settings;
    settings.frameIndices = {0, 1};
    settings.outputSize = QSize(4, 4);
    const QString outputPath = directory.filePath(QStringLiteral("canceled.ser"));
    int cancellationChecks = 0;
    const auto exportResult = seeingforge::SerExporter::exportFrames(
        source, outputPath, settings, {},
        [&cancellationChecks] { return ++cancellationChecks > 2; });
    QVERIFY(exportResult.canceled);
    QVERIFY(!QFileInfo::exists(outputPath));
}

void SerFileTests::detectsObjectAndAddsCropMargin()
{
    QImage frame(100, 80, QImage::Format_Grayscale8);
    frame.fill(0);
    for (int y = 20; y < 60; ++y) {
        uchar *line = frame.scanLine(y);
        for (int x = 30; x < 70; ++x) line[x] = 180;
    }
    const auto result = seeingforge::ObjectDetector::detect(frame, 100, 20, 200);
    QVERIFY(result.found());
    QCOMPARE(result.objectRect, QRect(30, 20, 40, 40));
    QCOMPARE(result.cropRect, QRect(22, 12, 56, 56));
}

void SerFileTests::detectsPersistentHotPixel()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("hot-pixel.ser"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(hotPixelSer());
    file.close();
    seeingforge::SerFile ser;
    QVERIFY2(ser.open(path), qPrintable(ser.errorString()));
    seeingforge::HotPixelSettings settings;
    settings.sampleCount = 4;
    const auto result = seeingforge::HotPixelDetector::detect(ser, settings);
    QCOMPARE(result.sampledFrames, 4);
    QCOMPARE(result.hotPixels, QVector<QPoint>{QPoint(4, 4)});
}

void SerFileTests::rejectsTruncatedFile()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("truncated.ser"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray data = monoSer();
    data.chop(2);
    file.write(data);
    file.close();

    seeingforge::SerFile ser;
    QVERIFY(!ser.open(path));
    QVERIFY(ser.errorString().contains(QStringLiteral("truncated"), Qt::CaseInsensitive));
}

void SerFileTests::cachesObjectRectDuringQualityAndReusesInExport()
{
    QByteArray data = monoSer();
    data.truncate(static_cast<qsizetype>(seeingforge::SerFile::HeaderSize));
    constexpr int w = 16;
    constexpr int h = 16;
    constexpr int count = 16;
    qToLittleEndian<quint32>(w, reinterpret_cast<uchar *>(data.data() + 26));
    qToLittleEndian<quint32>(h, reinterpret_cast<uchar *>(data.data() + 30));
    qToLittleEndian<quint32>(count, reinterpret_cast<uchar *>(data.data() + 38));
    for (int f = 0; f < count; ++f) {
        QByteArray frame(w * h, static_cast<char>(5));
        const int ox = f % 10;
        const int oy = (f * 3) % 10;
        for (int y = oy; y < oy + 4; ++y) {
            for (int x = ox; x < ox + 4; ++x) {
                frame[y * w + x] = static_cast<char>(220);
            }
        }
        data.append(frame);
    }
    for (int f = 0; f < count; ++f) {
        appendU64(data, static_cast<quint64>(1000 + f * 100));
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("multi-moving.ser"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write(data), data.size());
    input.close();

    seeingforge::SerFile source;
    QVERIFY2(source.open(inputPath), qPrintable(source.errorString()));
    const auto quality = seeingforge::QualityAnalyzer::analyze(source, 100);
    QVERIFY(!quality.canceled);
    QCOMPARE(quality.frames.size(), count);
    QCOMPARE(quality.detectionThreshold, 100);

    seeingforge::SerExportSettings cachedSettings;
    cachedSettings.centerObject = true;
    cachedSettings.detectionThreshold = 100;
    cachedSettings.outputSize = QSize(8, 8);
    for (int f = 0; f < count; ++f) {
        const auto &fq = quality.frames[f];
        QCOMPARE(fq.frameIndex, static_cast<std::uint32_t>(f));
        QVERIFY(fq.objectRect.isValid());
        const int ox = f % 10;
        const int oy = (f * 3) % 10;
        QCOMPARE(fq.objectRect, QRect(ox, oy, 4, 4));
        cachedSettings.frameIndices.append(fq.frameIndex);
        cachedSettings.cachedObjectRects.insert(fq.frameIndex, fq.objectRect);
    }

    seeingforge::SerExportSettings uncachedSettings = cachedSettings;
    uncachedSettings.cachedObjectRects.clear();

    const QString cachedPath = directory.filePath(QStringLiteral("cached.ser"));
    const QString uncachedPath = directory.filePath(QStringLiteral("uncached.ser"));
    const auto resCached = seeingforge::SerExporter::exportFrames(source, cachedPath, cachedSettings);
    const auto resUncached = seeingforge::SerExporter::exportFrames(source, uncachedPath, uncachedSettings);
    QVERIFY2(resCached.success, qPrintable(resCached.error));
    QVERIFY2(resUncached.success, qPrintable(resUncached.error));
    QVERIFY(resCached.timestampsPreserved);

    seeingforge::SerFile outCached;
    seeingforge::SerFile outUncached;
    QVERIFY(outCached.open(cachedPath));
    QVERIFY(outUncached.open(uncachedPath));
    QCOMPARE(outCached.header().frameCount, static_cast<std::uint32_t>(count));
    for (int f = 0; f < count; ++f) {
        QCOMPARE(outCached.readRawFrame(static_cast<std::uint32_t>(f)),
                 outUncached.readRawFrame(static_cast<std::uint32_t>(f)));
        QCOMPARE(outCached.frameTimestamp(static_cast<std::uint32_t>(f)),
                 static_cast<quint64>(1000 + f * 100));
        QCOMPARE(qGray(outCached.readFrame(static_cast<std::uint32_t>(f)).pixel(4, 4)), 220);
    }
}

void SerFileTests::appliesPostCropGainAndChannelLutWithPadding()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // Part A: Padded output (8x8 around 4x4 RGGB source) with gain + channel LUT
    const QString bayerPath = directory.filePath(QStringLiteral("bayer4.ser"));
    {
        QFile f(bayerPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bayerSer());
        f.close();
    }
    seeingforge::SerFile bayerSource;
    QVERIFY(bayerSource.open(bayerPath));
    seeingforge::SerExportSettings paddedSettings;
    paddedSettings.frameIndices = {0};
    paddedSettings.centerObject = false;
    paddedSettings.outputSize = QSize(8, 8);
    paddedSettings.intensityGain = 2.0;
    paddedSettings.bayerChannelGains = {0.25, 0.5, 1.0};
    const QString paddedOutPath = directory.filePath(QStringLiteral("padded-lut.ser"));
    const auto paddedRes = seeingforge::SerExporter::exportFrames(
        bayerSource, paddedOutPath, paddedSettings);
    QVERIFY2(paddedRes.success, qPrintable(paddedRes.error));

    seeingforge::SerFile paddedOut;
    QVERIFY(paddedOut.open(paddedOutPath));
    QCOMPARE(paddedOut.header().width, 8U);
    QCOMPARE(paddedOut.header().height, 8U);
    const QByteArray paddedFrame = paddedOut.readRawFrame(0);
    QCOMPARE(paddedFrame.size(), 64);
    // Border padding must stay 0
    QCOMPARE(static_cast<uchar>(paddedFrame[0]), static_cast<uchar>(0));
    QCOMPARE(static_cast<uchar>(paddedFrame[7 * 8 + 7]), static_cast<uchar>(0));
    // Source (0,0)=R=200 * 2.0 * 0.25 = 100 at dest (2,2)
    QCOMPARE(static_cast<uchar>(paddedFrame[2 * 8 + 2]), static_cast<uchar>(100));
    // Source (1,0)=G=100 * 2.0 * 0.5 = 100 at dest (3,2)
    QCOMPARE(static_cast<uchar>(paddedFrame[2 * 8 + 3]), static_cast<uchar>(100));
    // Source (1,1)=B=0 * 2.0 * 1.0 = 0 at dest (3,3)
    QCOMPARE(static_cast<uchar>(paddedFrame[3 * 8 + 3]), static_cast<uchar>(0));

    // Part B: Cropped sub-region (4x4 from 8x8 RGGB source) with positive even origin
    QByteArray bigBayer = monoSer();
    bigBayer.truncate(static_cast<qsizetype>(seeingforge::SerFile::HeaderSize));
    qToLittleEndian<quint32>(8, reinterpret_cast<uchar *>(bigBayer.data() + 18));
    qToLittleEndian<quint32>(8, reinterpret_cast<uchar *>(bigBayer.data() + 26));
    qToLittleEndian<quint32>(8, reinterpret_cast<uchar *>(bigBayer.data() + 30));
    qToLittleEndian<quint32>(1, reinterpret_cast<uchar *>(bigBayer.data() + 38));
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const int v = ((y & 1) == 0 && (x & 1) == 0) ? 100
                        : (((y & 1) != 0 && (x & 1) != 0) ? 80 : 60);
            bigBayer.append(static_cast<char>(v));
        }
    }
    const QString bigPath = directory.filePath(QStringLiteral("bayer8.ser"));
    {
        QFile f(bigPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bigBayer);
        f.close();
    }
    seeingforge::SerFile bigSource;
    QVERIFY(bigSource.open(bigPath));
    seeingforge::SerExportSettings cropSettings;
    cropSettings.frameIndices = {0};
    cropSettings.centerObject = false;
    cropSettings.outputSize = QSize(4, 4);
    cropSettings.intensityGain = 2.0;
    cropSettings.bayerChannelGains = {0.5, 1.0, 0.25};
    const QString cropOutPath = directory.filePath(QStringLiteral("cropped-lut.ser"));
    const auto cropRes = seeingforge::SerExporter::exportFrames(bigSource, cropOutPath, cropSettings);
    QVERIFY2(cropRes.success, qPrintable(cropRes.error));

    seeingforge::SerFile cropOut;
    QVERIFY(cropOut.open(cropOutPath));
    const QByteArray cropFrame = cropOut.readRawFrame(0);
    QCOMPARE(cropFrame.size(), 16);
    // (0,0) in cropped 4x4 is R=100 * 2.0 * 0.5 = 100
    QCOMPARE(static_cast<uchar>(cropFrame[0]), static_cast<uchar>(100));
    // (1,0) is G=60 * 2.0 * 1.0 = 120
    QCOMPARE(static_cast<uchar>(cropFrame[1]), static_cast<uchar>(120));
    // (1,1) is B=80 * 2.0 * 0.25 = 40
    QCOMPARE(static_cast<uchar>(cropFrame[4 + 1]), static_cast<uchar>(40));
}

void SerFileTests::parallelQualityAndExportPreserveOrder()
{
    QByteArray data = monoSer();
    data.truncate(static_cast<qsizetype>(seeingforge::SerFile::HeaderSize));
    constexpr int w = 32;
    constexpr int h = 32;
    constexpr int count = 20;
    qToLittleEndian<quint32>(w, reinterpret_cast<uchar *>(data.data() + 26));
    qToLittleEndian<quint32>(h, reinterpret_cast<uchar *>(data.data() + 30));
    qToLittleEndian<quint32>(count, reinterpret_cast<uchar *>(data.data() + 38));

    for (int f = 0; f < count; ++f) {
        QByteArray frame(w * h, static_cast<char>(4));
        const int highVal = 120 + (f * 5);
        const int lowVal = 110 - (f * 4);
        for (int y = 8; y < 24; ++y) {
            for (int x = 8; x < 24; ++x) {
                const bool high = ((x / 2 + y / 2) & 1) != 0;
                frame[y * w + x] = static_cast<char>(high ? highVal : lowVal);
            }
        }
        data.append(frame);
    }
    for (int f = 0; f < count; ++f) {
        appendU64(data, static_cast<quint64>(5000 + f * 17));
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("parallel20.ser"));
    {
        QFile f(inputPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
        f.close();
    }

    seeingforge::SerFile source;
    QVERIFY(source.open(inputPath));
    const auto quality = seeingforge::QualityAnalyzer::analyze(source, 20);
    QVERIFY(!quality.canceled);
    QCOMPARE(quality.frames.size(), count);
    for (int f = 0; f < count; ++f) {
        QCOMPARE(quality.frames[f].frameIndex, static_cast<std::uint32_t>(f));
    }
    QCOMPARE(quality.rankedFrameIndices.first(), static_cast<std::uint32_t>(count - 1));

    seeingforge::SerExportSettings settings;
    settings.frameIndices = quality.rankedFrameIndices;
    settings.centerObject = true;
    settings.detectionThreshold = 20;
    settings.outputSize = QSize(20, 20);
    const QString outPath = directory.filePath(QStringLiteral("ranked20.ser"));
    const auto exportRes = seeingforge::SerExporter::exportFrames(source, outPath, settings);
    QVERIFY2(exportRes.success, qPrintable(exportRes.error));
    QVERIFY(exportRes.timestampsPreserved);

    seeingforge::SerFile out;
    QVERIFY(out.open(outPath));
    QCOMPARE(out.header().frameCount, static_cast<std::uint32_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::uint32_t origIdx = quality.rankedFrameIndices[i];
        QCOMPARE(out.frameTimestamp(static_cast<std::uint32_t>(i)),
                 static_cast<quint64>(5000 + origIdx * 17));
    }
}

QTEST_MAIN(SerFileTests)
#include "SerFileTests.moc"
