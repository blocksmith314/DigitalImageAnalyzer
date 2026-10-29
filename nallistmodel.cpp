#include "nallistmodel.h"

#include <QColor>
#include <QFont>

namespace {

QColor color_for(const NALUnit& nal) {
  if (nal.codec == CodecType::kH264) {
    switch (nal.nal_type) {
      case 7:
        return QColor(220, 200, 255);
      case 8:
        return QColor(200, 255, 220);
      case 5:
        return QColor(255, 200, 200);
      case 6:
        return QColor(255, 255, 200);
      default:
        return {};
    }
  }
  switch (nal.nal_type) {
    case 32:
      return QColor(255, 220, 255);
    case 33:
      return QColor(220, 200, 255);
    case 34:
      return QColor(200, 255, 220);
    case 19:
    case 20:
    case 21:
      return QColor(255, 200, 200);
    case 39:
    case 40:
      return QColor(255, 255, 200);
    default:
      return {};
  }
}

}  // namespace

NalListModel::NalListModel(QObject* parent) : QAbstractTableModel(parent) {}

void NalListModel::SetNalUnits(const std::vector<NALUnit>* nals) {
  beginResetModel();
  nals_ = nals;
  frame_start_.clear();
  highlighted_frame_ = -1;

  int max_frame = -1;
  if (nals_) {
    for (const auto& nal : *nals_)
      if (nal.frame_index > max_frame) max_frame = nal.frame_index;
  }
  frame_start_.assign((size_t)max_frame + 1, -1);
  if (nals_) {
    for (int row = 0; row < (int)nals_->size(); row++) {
      const int f = (*nals_)[row].frame_index;
      if (f >= 0 && frame_start_[(size_t)f] < 0) frame_start_[(size_t)f] = row;
    }
  }
  endResetModel();
}

void NalListModel::SetHighlightedFrame(int frame_index) {
  if (highlighted_frame_ == frame_index) return;
  const int old = highlighted_frame_;
  highlighted_frame_ = frame_index;
  if (nals_ && !nals_->empty()) {
    emit dataChanged(index(0, 0),
                     index((int)nals_->size() - 1, columnCount() - 1),
                     {Qt::BackgroundRole});
    Q_UNUSED(old);
  }
}

int NalListModel::FirstRowForFrame(int frame_index) const {
  if (frame_index < 0 || (size_t)frame_index >= frame_start_.size()) return -1;
  return frame_start_[(size_t)frame_index];
}

int NalListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid() || !nals_) return 0;
  return (int)nals_->size();
}

int NalListModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : 8;
}

QVariant NalListModel::data(const QModelIndex& index, int role) const {
  if (!nals_ || !index.isValid()) return {};
  const int row = index.row();
  if (row < 0 || row >= (int)nals_->size()) return {};
  const NALUnit& nal = (*nals_)[(size_t)row];

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case 0:
        return row;
      case 1:
        return nal.nal_type;
      case 2:
        return QString::fromStdString(nal.TypeName());
      case 3:
        return QString::number(nal.size);
      case 4:
        return QString("0x%1").arg(nal.offset, 8, 16, QChar('0')).toUpper();
      case 5:
        return nal.frame_type ? QString(QChar(nal.frame_type)) : QString("-");
      case 6:
        return nal.dts_index >= 0 ? QString::number(nal.dts_index)
                                  : QString("-");
      case 7:
        return nal.frame_index >= 0 ? QString::number(nal.frame_index)
                                    : QString("-");
      default:
        return QString();
    }
  }

  if (role == Qt::BackgroundRole) {
    if (nal.frame_index >= 0 && nal.frame_index == highlighted_frame_)
      return QColor(255, 165, 30);
    const QColor color = color_for(nal);
    // Return a null QVariant (not an invalid QColor) so the view keeps its
    // default background instead of painting the row black.
    return color.isValid() ? QVariant(color) : QVariant();
  }

  if (role == Qt::TextAlignmentRole) {
    switch (index.column()) {
      case 0:
      case 1:
      case 3:
      case 4:
      case 6:
      case 7:
        return int(Qt::AlignRight | Qt::AlignVCenter);
      case 5:
        return int(Qt::AlignHCenter | Qt::AlignVCenter);
      default:
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }
  }

  if (role == Qt::FontRole && (index.column() == 3 || index.column() == 4)) {
    static QFont mono("Menlo");
    mono.setStyleHint(QFont::Monospace);
    return mono;
  }

  return {};
}

QVariant NalListModel::headerData(int section, Qt::Orientation orientation,
                                  int role) const {
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
  switch (section) {
    case 0:
      return "#";
    case 1:
      return "NAL Type";
    case 2:
      return "NAL Name";
    case 3:
      return "Size";
    case 4:
      return "Offset";
    case 5:
      return "Frame Type";
    case 6:
      return "DTS";
    case 7:
      return "PTS";
    default:
      return QString();
  }
}
