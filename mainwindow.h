#pragma once

#include <QMainWindow>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>
#include <QMenu>
#include <QAction>
#include <QStringList>
#include <QTimer>
#include <QSlider>
#include <QPushButton>
#include "bitstreamparser.h"
#include "nalanalyzer.h"
#include "decoder.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openFile();
    void openRecentFile();
    void onNALSelected(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void onPlayPause();
    void onStop();
    void onSeek(int value);
    void onPlayerTick();

private:
    static constexpr int MAX_RECENT_FILES = 10;

    void setupUI();
    void buildNALList();
    void updateStats();
    void decodeThumbnails();
    void loadRecentFiles();
    void saveRecentFiles();
    void updateRecentMenu();
    void addRecentFile(const QString &path);
    bool loadFile(const QString &path);
    void startPlayer();
    void showFrame(const QImage &image, int frameIdx);
    void highlightFrame(int frameIdx);

    BitstreamParser m_parser;
    NALAnalyzer m_analyzer;
    FFmpegDecoder m_decoder;
    FFmpegDecoder m_playerDecoder;
    std::string m_currentPath;

    QTreeWidget *m_nalList = nullptr;
    QTextBrowser *m_details = nullptr;
    QTextBrowser *m_statsPanel = nullptr;
    QLabel *m_videoLabel = nullptr;
    QLabel *m_fileInfoLabel = nullptr;
    QLabel *m_frameInfoLabel = nullptr;
    QMenu *m_recentMenu = nullptr;
    QList<QAction *> m_recentActions;
    QStringList m_recentFiles;

    QTimer *m_playerTimer = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QSlider *m_seekSlider = nullptr;

    bool m_isPlaying = false;
    int m_totalFrames = 0;
    double m_frameRate = 30.0;
    QSize m_videoSize;
    std::vector<DecodedFrame> m_frames;

protected:
    void resizeEvent(QResizeEvent *event) override;
};