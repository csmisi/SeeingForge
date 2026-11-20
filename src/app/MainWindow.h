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
#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QPoint>

#include <array>
#include <atomic>
#include <memory>

class QAction;
class QButtonGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QFrame;
class QLabel;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QToolButton;
class QualityGraph;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void openSerFile();
    void openRawFiles();
    void openRawFolder();
    void fitImage();
    void showActualSize();
    void zoomIn();
    void zoomOut();
    void showAbout();
    void selectWorkflowStep(int step);
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
    void togglePlayback();
    void stepFrame(int delta);
    void toggleDisplayPopover();
    void resetDisplayControls();
    void applySelectedPreset();
    void saveNamedPreset();

private:
    void applyThemeStyleSheet();
    void createActions();
    void createMenus();
    void createWorkspace();
    QWidget *createHeroHeader();
    QWidget *createWorkflowPanel();
    QWidget *createPreviewPanel();
    QWidget *createDisplayPopover(QWidget *parent);
    QWidget *createInspectorPanel();
    QWidget *createSourceInspector();
    QWidget *createCalibrationInspector();
    QWidget *createStabilizeInspector();
    QWidget *createQualityInspector();
    QWidget *createExportInspector();
    QWidget *createFooterBar();
    void loadSerFile(const QString &path);
    void loadRawFiles(const QStringList &paths);
    void rawSequenceLoadFinished();
    void finishLoadingSource();
    void checkBayerMetadata();
    void loadFrame(std::uint32_t index);
    QImage readCachedFrame(std::uint32_t index);
    void startPreviewRender();
    seeingforge::PreviewSettings previewSettings() const;
    std::uint32_t effectiveColorId() const;
    int currentOrientationTurns() const;
    std::uint32_t referenceFrameIndex() const;
    void updateImage();
    void centerPreview();
    void updateMetadata();
    void updateWorkflowSummaries();
    void updateHeroSummary();
    void updateFrameQualityReadout();
    void updateExportIntensityDisplay();
    void updateFooterNavigation();
    void positionDisplayPopover();
    double selectedExportIntensityGain() const;
    void loadUserPreferences();
    void populatePresetCombo();
    void savePreferredChoices();
    void resetCurrentStep();
    void setMetadataRow(int row, const QString &name, const QString &value);
    void showStatus(const QString &message, int timeoutMs = 0);

    std::shared_ptr<seeingforge::FrameSource> m_source;
    QImage m_rawFrame;
    QList<QPair<std::uint32_t, QImage>> m_frameCache;
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

    // Hero header widgets
    QLabel *m_heroTitle = nullptr;
    QLabel *m_heroFormatBadge = nullptr;
    QLabel *m_heroDimBadge = nullptr;
    QLabel *m_heroSubtitle = nullptr;
    QLabel *m_statusDot = nullptr;
    QLabel *m_previewStateLabel = nullptr;
    QLabel *m_pipelineStateLabel = nullptr;
    QPushButton *m_displayPopoverButton = nullptr;
    QButtonGroup *m_previewMode = nullptr;

    // Left pipeline rail & presets
    QButtonGroup *m_workflowGroup = nullptr;
    std::array<QPushButton *, 5> m_workflowButtons{};
    std::array<QLabel *, 5> m_navStepBadges{};
    std::array<QLabel *, 5> m_navSubLabels{};
    QComboBox *m_presetCombo = nullptr;

    // Center preview viewport & floating popover
    QWidget *m_previewPanel = nullptr;
    QWidget *m_overlayStrip = nullptr;
    QFrame *m_displayPopover = nullptr;
    QScrollArea *m_previewScrollArea = nullptr;
    QWidget *m_previewCanvas = nullptr;
    QLabel *m_imageLabel = nullptr;
    QFrame *m_emptyHub = nullptr;
    QToolButton *m_overlayDetectBtn = nullptr;
    QToolButton *m_overlayHotPixelsBtn = nullptr;
    QToolButton *m_overlayCrosshairBtn = nullptr;
    QLabel *m_canvasObjectBadge = nullptr;
    QLabel *m_canvasCropBadge = nullptr;
    QLabel *m_cfaHintToast = nullptr;

    // Transport & zoom dock
    QPushButton *m_prevFrameBtn = nullptr;
    QPushButton *m_playButton = nullptr;
    QPushButton *m_nextFrameBtn = nullptr;
    QLabel *m_frameLabel = nullptr;
    QSlider *m_frameSlider = nullptr;
    QLabel *m_frameQualityLabel = nullptr;
    QToolButton *m_zoomFitBtn = nullptr;
    QToolButton *m_zoomActualBtn = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QTimer *m_playTimer = nullptr;
    bool m_playing = false;
    bool m_panning = false;
    QPoint m_panLastPos;

    // Right inspector stack
    QStackedWidget *m_inspectorStack = nullptr;

    // Step 0: Source & Sensor
    QLabel *m_sourceStatusBadge = nullptr;
    QLabel *m_srcFramesTileValue = nullptr;
    QLabel *m_srcFramesTileSub = nullptr;
    QLabel *m_srcResTileValue = nullptr;
    QLabel *m_srcResTileSub = nullptr;
    QLabel *m_srcFormatTileValue = nullptr;
    QLabel *m_srcFormatTileSub = nullptr;
    QLabel *m_srcWbTileValue = nullptr;
    QLabel *m_srcWbTileSub = nullptr;
    QComboBox *m_bayerPattern = nullptr;
    QLabel *m_bayerWarning = nullptr;
    QTableWidget *m_metadataTable = nullptr;

    // Step 1: Calibration
    QComboBox *m_rawExportWhiteBalance = nullptr;
    QComboBox *m_hotPixelSamples = nullptr;
    QLabel *m_calHotCountTileValue = nullptr;
    QLabel *m_calSampledTileValue = nullptr;
    QSlider *m_hotPixelBrightness = nullptr;
    QSlider *m_hotPixelContrast = nullptr;
    QSlider *m_hotPixelPersistence = nullptr;
    QPushButton *m_analyzeHotPixels = nullptr;
    QLabel *m_hotPixelLabel = nullptr;

    // Floating Display (Preview-Only) Controls
    QComboBox *m_debayerMethod = nullptr;
    QCheckBox *m_whiteBalance = nullptr;
    QCheckBox *m_autoStretch = nullptr;
    QSlider *m_redBalance = nullptr;
    QSlider *m_blueBalance = nullptr;
    QSlider *m_saturation = nullptr;
    QSlider *m_hue = nullptr;
    QSlider *m_gain = nullptr;
    QSlider *m_gamma = nullptr;

    // Step 2: Stabilize & Crop
    QLabel *m_stabObjTileValue = nullptr;
    QLabel *m_stabCropTileValue = nullptr;
    QButtonGroup *m_orientationGroup = nullptr;
    QLabel *m_orientationChip = nullptr;
    QCheckBox *m_flipHorizontal = nullptr;
    QCheckBox *m_flipVertical = nullptr;
    QSlider *m_detectionThreshold = nullptr;
    QComboBox *m_cropMargin = nullptr;
    QComboBox *m_outputSizeMode = nullptr;
    QWidget *m_customSizeContainer = nullptr;
    QSpinBox *m_customOutputWidth = nullptr;
    QSpinBox *m_customOutputHeight = nullptr;
    QLabel *m_detectionLabel = nullptr;

    // Step 3: Quality Ranking
    QLabel *m_qualStatusBadge = nullptr;
    QLabel *m_qualKeptTileValue = nullptr;
    QLabel *m_qualCutoffTileValue = nullptr;
    QPushButton *m_analyzeQuality = nullptr;
    QPushButton *m_cancelQuality = nullptr;
    QProgressBar *m_qualityProgress = nullptr;
    QComboBox *m_qualityKeep = nullptr;
    QComboBox *m_qualityOrder = nullptr;
    QCheckBox *m_rejectClipped = nullptr;
    QSpinBox *m_maxClippedPercent = nullptr;
    QLabel *m_clippedRejectedCountLabel = nullptr;
    QLabel *m_qualityLabel = nullptr;
    QualityGraph *m_qualityGraph = nullptr;
    QButtonGroup *m_qualityFilterGroup = nullptr;
    QTableWidget *m_qualityTable = nullptr;

    // Step 4: Export SER
    QLabel *m_expStatusBadge = nullptr;
    QLabel *m_expFramesTileValue = nullptr;
    QLabel *m_expFramesTileSub = nullptr;
    QLabel *m_expPeakTileValue = nullptr;
    QLabel *m_expPeakTileSub = nullptr;
    QComboBox *m_exportIntensity = nullptr;
    QLabel *m_exportIntensityLabel = nullptr;
    QCheckBox *m_exportMonochrome = nullptr;
    QCheckBox *m_exportMonoBinning = nullptr;
    QLabel *m_exportSummary = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_cancelExport = nullptr;
    QProgressBar *m_exportProgress = nullptr;
    QLabel *m_exportResultLabel = nullptr;
    QFrame *m_exportDoneBanner = nullptr;
    QLabel *m_exportDoneText = nullptr;
    QString m_lastExportPath;

    // Footer bar
    QLabel *m_footerStatusLabel = nullptr;
    QString m_defaultStatusMessage;
    QTimer *m_statusResetTimer = nullptr;
    QPushButton *m_prevStepButton = nullptr;
    QPushButton *m_nextStepButton = nullptr;

    QTimer *m_frameDebounce = nullptr;
    QTimer *m_detectionDebounce = nullptr;
    QTimer *m_hotPixelDebounce = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_actualSizeAction = nullptr;

    int m_activeStep = 0;
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
