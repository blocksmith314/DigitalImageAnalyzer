#pragma once

#include <QMainWindow>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>

#include "bitstreamparser.h"
#include "nalanalyzer.h"
#include "decoder.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openFile();
    void onNALSelected(QTreeWidgetItem *current, QTreeWidgetItem *previous);

private:
    void setupUI();
    void buildNALList();
    void updateStats();
    void decodeThumbnails();

    BitstreamParser m_parser;
    NALAnalyzer m_analyzer;
    FFmpegDecoder m_decoder;
    std::string m_currentPath;

    QTreeWidget *m_nalList = nullptr;
    QTextBrowser *m_details = nullptr;
    QTextBrowser *m_statsPanel = nullptr;
    QLabel *m_thumbnailLabel = nullptr;
    QLabel *m_fileInfoLabel = nullptr;
};