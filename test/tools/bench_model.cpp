// Micro-benchmark for NalListModel repaint work. Developer tool, not a test.
//
//   bench_model
//
// Measures data() cost for one screenful of cells and the average row span of
// the dataChanged emissions produced by SetHighlightedFrame().

#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstdio>
#include <vector>

#include "nallistmodel.h"
#include "nalunit.h"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const int rows = 1000000;
  std::vector<NALUnit> nals(static_cast<size_t>(rows));
  for (int i = 0; i < rows; i++) {
    nals[static_cast<size_t>(i)].codec = CodecType::kH265;
    nals[static_cast<size_t>(i)].nal_type = static_cast<uint8_t>(i % 48);
    nals[static_cast<size_t>(i)].size = 1000;
    nals[static_cast<size_t>(i)].offset = static_cast<uint64_t>(i);
    nals[static_cast<size_t>(i)].frame_index = i / 50;
    nals[static_cast<size_t>(i)].dts_index = i / 50;
    nals[static_cast<size_t>(i)].frame_type = 'P';
  }
  NalListModel model;
  model.SetNalUnits(&nals);

  const int visible_rows = 40;
  const int columns = 8;
  const int iterations = 200;
  int valid = 0;
  QElapsedTimer timer;
  timer.start();
  for (int it = 0; it < iterations; it++) {
    for (int r = 0; r < visible_rows; r++) {
      for (int c = 0; c < columns; c++) {
        const QModelIndex index = model.index(r, c);
        valid += model.data(index, Qt::DisplayRole).isValid() ? 1 : 0;
        valid += model.data(index, Qt::BackgroundRole).isValid() ? 1 : 0;
      }
    }
  }
  const double calls = iterations * visible_rows * columns * 2.0;
  std::printf("data(): %.1f ns/call (%d valid variants)\n",
              timer.nsecsElapsed() / calls, valid);

  long long rows_sum = 0;
  int signal_count = 0;
  QObject::connect(
      &model, &QAbstractItemModel::dataChanged,
      [&](const QModelIndex& top_left, const QModelIndex& bottom_right) {
        rows_sum += bottom_right.row() - top_left.row() + 1;
        signal_count++;
      });
  for (int i = 0; i < 1000; i++) {
    model.SetHighlightedFrame(i % 100);
  }
  std::printf(
      "highlight: %d signals, %.1f rows/signal\n", signal_count,
      signal_count ? static_cast<double>(rows_sum) / signal_count : 0.0);
  return 0;
}
