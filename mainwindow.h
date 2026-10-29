#pragma once

#include <QAction>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <unordered_map>
#include "bitstreamparser.h"
#include "decoder.h"
#include "nalanalyzer.h"

class AudioPlayer;
class NalListModel;
class QSplitter;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    // Load a file programmatically (same path as File > Open).
    bool loadFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void openFile();
    void openRecentFile();
    void onNALSelected(int row);
    void onPlayPause();
    void onStop();
    void onStepForward();
    void onStepBackward();
    void onSeek(int value);
    void toggleFullscreen();
    void onPlayerTick();
    void onSpeedChanged(int index);

private:
    static constexpr int MAX_RECENT_FILES = 10;

    void setupUI();
    void buildNALList();
    void updateStats();
    void decodeThumbnails();
    void loadRecentFiles();
    void saveRecentFiles();
    void updateRecentMenu();
    void addRecentFile(const QString& path);
    void startPlayer();
    void showFrame(const QImage& image, int frameIdx);
    void showFrameAt(int idx);
    void enterFullscreen();
    void exitFullscreen();
    void updateFullscreenFrame();
    void syncSplitterWidths(bool fromTop = true);
    void applySplitFraction();
    void stopPlayback();
    void highlightFrame(int frameIdx);
    int playerIntervalMs() const;

    BitstreamParser m_parser;
    NALAnalyzer m_analyzer;
    FFmpegDecoder m_decoder;
    FFmpegDecoder m_playerDecoder;
    AudioPlayer* m_audio = nullptr;
    std::string m_currentPath;

    QTreeView* m_nalList = nullptr;
    QSplitter* m_topSplit = nullptr;
    QSplitter* m_bottomSplit = nullptr;
    double m_splitFraction = 600.0 / 1400.0;
    bool m_syncingSplitters = false;
    NalListModel* m_nalModel = nullptr;
    QTextBrowser* m_details = nullptr;
    QTextBrowser* m_statsPanel = nullptr;
    QLabel* m_videoLabel = nullptr;
    QLabel* m_fileInfoLabel = nullptr;
    QLabel* m_timeInfoLabel = nullptr;
    QLabel* m_frameInfoLabel = nullptr;
    QMenu* m_recentMenu = nullptr;
    QList<QAction*> m_recentActions;
    QStringList m_recentFiles;

    QTimer* m_playerTimer = nullptr;
    QPushButton* m_playBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QPushButton* m_prevFrameBtn = nullptr;
    QPushButton* m_nextFrameBtn = nullptr;
    QPushButton* m_fullscreenBtn = nullptr;
    QSlider* m_seekSlider = nullptr;
    QComboBox* m_speedCombo = nullptr;
    QSpinBox* m_stepSpin = nullptr;

    bool m_isPlaying = false;
    int m_totalFrames = 0;
    int m_currentFrameIndex = -1;
    double m_frameRate = 30.0;
    double m_playbackSpeed = 1.0;
    QSize m_videoSize;
    QImage m_currentImage;
    DecodedFrame m_pendingFrame;
    bool m_hasPending = false;
    double m_currentPts = 0.0;
    double m_clockStartPts = 0.0;
    QElapsedTimer m_playClock;
    QWidget* m_fullscreenWindow = nullptr;
    QLabel* m_fullscreenLabel = nullptr;
    bool m_fullscreen = false;

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
};
