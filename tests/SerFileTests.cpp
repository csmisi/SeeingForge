#include "core/SerFile.h"
#include "core/PreviewProcessor.h"
#include "core/ObjectDetector.h"
#include "core/HotPixelDetector.h"
#include "core/QualityAnalyzer.h"
#include "core/SerExporter.h"

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
    void exportsRgbAsMonochrome();
    void exportsBayerAsBinnedMonochrome();
    void rotatesBayerExportAndRemapsPattern();
    void cancelsSlowOperationsCleanly();
    void detectsObjectAndAddsCropMargin();
    void detectsPersistentHotPixel();
    void rejectsTruncatedFile();
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
    QCOMPARE(output.header().width, 2U);
    QCOMPARE(output.header().height, 2U);
    QCOMPARE(qGray(output.readFrame(0).pixel(0, 0)), 143);
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
    QCOMPARE(output.header().width, 2U);
    QCOMPARE(output.header().height, 2U);
    QCOMPARE(qGray(output.readFrame(0).pixel(0, 0)), 100);
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

QTEST_MAIN(SerFileTests)
#include "SerFileTests.moc"
