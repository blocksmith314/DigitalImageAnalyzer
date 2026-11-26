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
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVariantList>
#include <QVariantMap>
#include <unordered_map>

#include "bitstreamparser.h"
#include "decoder.h"
#include "framecache.h"
#include "nalanalyzer.h"

class AudioPlayer;
class DecoderService;
class NalListModel;
class QDockWidget;
class QPlainTextEdit;
class QSplitter;

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  // Load a file programmatically (same path as File > Open).
  bool LoadFile(const QString& path);

 protected:
  void closeEvent(QCloseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

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
  void ShowAbout();
  void ShowShortcuts();
  void UpdatePositionLabel();
  void ShowFirstFrameBlocking();
  void SetPerfLogVisible(bool visible);
  void OnSeekPreview(const QVariantMap& result);
  void OnSeekFrameReady(const QVariantMap& result);
  void OnSeekFailed(const QVariantMap& result);

 private:
  enum class ProgressMode { kFrames, kTime };

  static constexpr int MAX_RECENT_FILES = 10;

  void SetupUi();
  void BuildNalList();
  void UpdateStats();
  double SpsFrameRate() const;
  void SetupPerfLog();
  void AppendPerf(const QString& line);
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
  QVariantMap OpenDecoder(const QString& path, CodecType codec,
                          const QVariantList& raw_keyframes);
  QVariantMap SeekDecoder(int frame);
  QVariantMap DecodeFrame();

  BitstreamParser parser_;
  NALAnalyzer analyzer_;
  FFmpegDecoder decoder_;
  DecoderService* decoder_service_ = nullptr;
  QThread decode_thread_;
  bool has_timestamps_ = false;
  double duration_sec_ = 0.0;
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
  QComboBox* progress_mode_combo_ = nullptr;
  QDockWidget* perf_dock_ = nullptr;
  QPlainTextEdit* perf_log_ = nullptr;
  QAction* perf_action_ = nullptr;
  bool perf_log_enabled_ = false;
  quint64 seek_generation_ = 0;
  int pending_seek_frame_ = -1;
  bool pending_seek_jump_ = false;
  bool seek_pending_ = false;
  QElapsedTimer seek_request_timer_;
  ProgressMode progress_mode_ = ProgressMode::kFrames;

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
