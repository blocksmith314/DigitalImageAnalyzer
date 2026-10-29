#ifndef DIA_MAINWINDOW_H_
#define DIA_MAINWINDOW_H_

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

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  MainWindow(QWidget* parent = nullptr);
  ~MainWindow();

  // Load a file programmatically (same path as File > Open).
  bool LoadFile(const QString& path);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void OpenFile();
  void OpenRecentFile();
  void OnNalSelected(int row);
  void OnPlayPause();
  void OnStop();
  void OnStepForward();
  void OnStepBackward();
  void OnSeek(int value);
  void ToggleFullscreen();
  void OnPlayerTick();
  void OnSpeedChanged(int index);

 private:
  static constexpr int MAX_RECENT_FILES = 10;

  void SetupUi();
  void BuildNalList();
  void UpdateStats();
  void DecodeThumbnails();
  void LoadRecentFiles();
  void SaveRecentFiles();
  void UpdateRecentMenu();
  void AddRecentFile(const QString& path);
  void StartPlayer();
  void ShowFrame(const QImage& image, int frame_idx);
  void ShowFrameAt(int idx);
  void EnterFullscreen();
  void ExitFullscreen();
  void UpdateFullscreenFrame();
  void SyncSplitterWidths(bool from_top = true);
  void ApplySplitFraction();
  void StopPlayback();
  void HighlightFrame(int frame_idx);
  int PlayerIntervalMs() const;

  BitstreamParser parser_;
  NALAnalyzer analyzer_;
  FFmpegDecoder decoder_;
  FFmpegDecoder player_decoder_;
  AudioPlayer* audio_ = nullptr;
  std::string current_path_;

  QTreeView* nal_list_ = nullptr;
  QSplitter* top_split_ = nullptr;
  QSplitter* bottom_split_ = nullptr;
  double split_fraction_ = 600.0 / 1400.0;
  bool syncing_splitters_ = false;
  NalListModel* nal_model_ = nullptr;
  QTextBrowser* details_ = nullptr;
  QTextBrowser* stats_panel_ = nullptr;
  QLabel* video_label_ = nullptr;
  QLabel* file_info_label_ = nullptr;
  QLabel* time_info_label_ = nullptr;
  QLabel* frame_info_label_ = nullptr;
  QMenu* recent_menu_ = nullptr;
  QList<QAction*> recent_actions_;
  QStringList recent_files_;

  QTimer* player_timer_ = nullptr;
  QPushButton* play_btn_ = nullptr;
  QPushButton* stop_btn_ = nullptr;
  QPushButton* prev_frame_btn_ = nullptr;
  QPushButton* next_frame_btn_ = nullptr;
  QPushButton* fullscreen_btn_ = nullptr;
  QSlider* seek_slider_ = nullptr;
  QComboBox* speed_combo_ = nullptr;
  QSpinBox* step_spin_ = nullptr;

  bool is_playing_ = false;
  int total_frames_ = 0;
  int current_frame_index_ = -1;
  double frame_rate_ = 30.0;
  double playback_speed_ = 1.0;
  QSize video_size_;
  QImage current_image_;
  DecodedFrame pending_frame_;
  bool has_pending_ = false;
  double current_pts_ = 0.0;
  double clock_start_pts_ = 0.0;
  QElapsedTimer play_clock_;
  QWidget* fullscreen_window_ = nullptr;
  QLabel* fullscreen_label_ = nullptr;
  bool fullscreen_ = false;

 protected:
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
};

#endif  // DIA_MAINWINDOW_H_
