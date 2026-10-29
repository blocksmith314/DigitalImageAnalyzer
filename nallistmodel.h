#ifndef DIA_NALLISTMODEL_H_
#define DIA_NALLISTMODEL_H_

#include <QAbstractTableModel>
#include <vector>

#include "core/nalunit.h"

// Read-only, virtualized model over all NAL units: every NAL has a row but
// only visible rows are queried, so large streams stay responsive and frame
// highlighting works for any frame index.
class NalListModel : public QAbstractTableModel {
  Q_OBJECT
 public:
  explicit NalListModel(QObject* parent = nullptr);

  void SetNalUnits(const std::vector<NALUnit>* nals);
  void SetHighlightedFrame(int frame_index);
  int FirstRowForFrame(int frame_index) const;

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;

 private:
  const std::vector<NALUnit>* nals_ = nullptr;
  std::vector<int> frame_start_;  // frame index -> first NAL row
  int highlighted_frame_ = -1;
};

#endif  // DIA_NALLISTMODEL_H_
