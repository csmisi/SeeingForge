#pragma once

#include "core/PreviewProcessor.h"
#include "core/FrameSource.h"
#include "core/SerFile.h"
#include "core/RawSequence.h"
#include "core/ObjectDetector.h"
#include "core/HotPixelDetector.h"
#include "core/QualityAnalyzer.h"
#include "core/SerExporter.h"
#include "core/BayerPatternAnalyzer.h"

#include <QFutureWatcher>
#include <QMainWindow>

#include <atomic>
#include <memory>

class QAction;
class QButtonGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QListWidget;
class QResizeEvent;
class QScrollArea;
class QPushButton;
class QProgressBar;
class QDialog;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QualityGraph;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void openSerFile();
    void openRawFiles();
    void openRawFolder();
    void fitImage();
    void showActualSize();
    void zoomIn();
    void zoomOut();
    void showAbout();
    void selectWorkflowStep(int row);
    void requestPreviewRender();
    void previewRenderFinished();
    void previewModeChanged();
    void loadSelectedFrame();
    void updateDetection();
    void startHotPixelAnalysis();
    void requestHotPixelAnalysis();
    void hotPixelAnalysisFinished();
    void startQualityAnalysis();
    void qualityAnalysisFinished();
    void updateQualityDisplay();
    void cancelQualityAnalysis();
    void startExport();
    void exportFinished();
    void cancelExport();

private:
    void createActions();
    void createMenus();
    void createWorkspace();
    QWidget *createWorkflowPanel();
    QWidget *createPreviewPanel();
    QWidget *createInspectorPanel();
    QWidget *createSourceInspector();
    QWidget *createCalibrationInspector();
    QWidget *createColourInspector();
    QWidget *createStabilizeInspector();
    QWidget *createQualityInspector();
    QWidget *createExportInspector();
    QWidget *createPlaceholderInspector(const QString &title, const QString &description);
    void loadSerFile(const QString &path);
    void loadRawFiles(const QStringList &paths);
    void rawSequenceLoadFinished();
    void finishLoadingSource();
    void checkBayerMetadata();
    void loadFrame(std::uint32_t index);
    void startPreviewRender();
    seeingforge::PreviewSettings previewSettings() const;
    std::uint32_t effectiveColorId() const;
    void updateImage();
    void centerPreview();
    void updateMetadata();
    void updateExportIntensityDisplay();
    double selectedExportIntensityGain() const;
    void loadUserPreferences();
    void savePreferredChoices();
    void resetCurrentStep();
    void setMetadataRow(int row, const QString &name, const QString &value);

    std::shared_ptr<seeingforge::FrameSource> m_source;
    QImage m_rawFrame;
    seeingforge::PreviewImages m_previews;
    QFutureWatcher<seeingforge::PreviewImages> *m_previewWatcher = nullptr;
    QFutureWatcher<seeingforge::HotPixelResult> *m_hotPixelWatcher = nullptr;
    QFutureWatcher<seeingforge::QualityResult> *m_qualityWatcher = nullptr;
    QFutureWatcher<seeingforge::ExportResult> *m_exportWatcher = nullptr;
    QFutureWatcher<std::shared_ptr<seeingforge::RawSequence>> *m_rawLoadWatcher = nullptr;
    QDialog *m_rawLoadDialog = nullptr;
    QLabel *m_rawLoadLabel = nullptr;
    QProgressBar *m_rawLoadProgress = nullptr;
    QPushButton *m_rawLoadCancelButton = nullptr;
    bool m_renderDirty = false;

    QLabel *m_imageLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QScrollArea *m_previewScrollArea = nullptr;
    QWidget *m_previewCanvas = nullptr;
    QLabel *m_previewStateLabel = nullptr;
    QLabel *m_frameLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QListWidget *m_workflowList = nullptr;
    QStackedWidget *m_inspectorStack = nullptr;
    QTableWidget *m_metadataTable = nullptr;
    QComboBox *m_debayerMethod = nullptr;
    QComboBox *m_bayerPattern = nullptr;
    QLabel *m_bayerWarning = nullptr;
    QComboBox *m_cropMargin = nullptr;
    QComboBox *m_hotPixelSamples = nullptr;
    QComboBox *m_rawExportWhiteBalance = nullptr;
    QCheckBox *m_whiteBalance = nullptr;
    QCheckBox *m_autoStretch = nullptr;
    QSlider *m_redBalance = nullptr;
    QSlider *m_blueBalance = nullptr;
    QSlider *m_saturation = nullptr;
    QSlider *m_hue = nullptr;
    QSlider *m_gain = nullptr;
    QSlider *m_gamma = nullptr;
    QComboBox *m_orientation = nullptr;
    QCheckBox *m_flipHorizontal = nullptr;
    QCheckBox *m_flipVertical = nullptr;
    QSlider *m_frameSlider = nullptr;
    QSlider *m_detectionThreshold = nullptr;
    QCheckBox *m_showDetection = nullptr;
    QCheckBox *m_showHotPixels = nullptr;
    QCheckBox *m_exportMonochrome = nullptr;
    QCheckBox *m_exportMonoBinning = nullptr;
    QLabel *m_detectionLabel = nullptr;
    QLabel *m_hotPixelLabel = nullptr;
    QLabel *m_qualityLabel = nullptr;
    QLabel *m_exportSummary = nullptr;
    QLabel *m_exportIntensityLabel = nullptr;
    QLabel *m_exportResultLabel = nullptr;
    QSlider *m_hotPixelBrightness = nullptr;
    QSlider *m_hotPixelContrast = nullptr;
    QSlider *m_hotPixelPersistence = nullptr;
    QPushButton *m_analyzeHotPixels = nullptr;
    QPushButton *m_analyzeQuality = nullptr;
    QPushButton *m_cancelQuality = nullptr;
    QComboBox *m_qualityKeep = nullptr;
    QComboBox *m_qualityOrder = nullptr;
    QCheckBox *m_rejectClipped = nullptr;
    QSpinBox *m_maxClippedPercent = nullptr;
    QTableWidget *m_qualityTable = nullptr;
    QualityGraph *m_qualityGraph = nullptr;
    QProgressBar *m_qualityProgress = nullptr;
    QProgressBar *m_exportProgress = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_cancelExport = nullptr;
    QComboBox *m_outputSizeMode = nullptr;
    QComboBox *m_exportIntensity = nullptr;
    QSpinBox *m_customOutputWidth = nullptr;
    QSpinBox *m_customOutputHeight = nullptr;
    QTimer *m_frameDebounce = nullptr;
    QTimer *m_detectionDebounce = nullptr;
    QTimer *m_hotPixelDebounce = nullptr;
    QButtonGroup *m_previewMode = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_actualSizeAction = nullptr;
    bool m_fitToWindow = true;
    bool m_hotPixelDirty = false;
    double m_zoomFactor = 1.0;
    std::uint32_t m_currentFrame = 0;
    seeingforge::DetectionResult m_detection;
    seeingforge::HotPixelResult m_hotPixels;
    seeingforge::QualityResult m_qualityResult;
    std::shared_ptr<std::atomic_bool> m_qualityCancel;
    std::shared_ptr<std::atomic_bool> m_exportCancel;
    std::shared_ptr<std::atomic_bool> m_rawLoadCancel;
};
