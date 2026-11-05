#include "nallistmodel.h"

#include <QColor>
#include <QFont>
#include <array>

namespace {

const QColor& color_for(const NALUnit& nal) {
  static const QColor kNone;
  static const QColor kSps(220, 200, 255);
  static const QColor kPps(200, 255, 220);
  static const QColor kIdr(255, 200, 200);
  static const QColor kSei(255, 255, 200);
  static const QColor kVps(255, 220, 255);
  if (nal.codec == CodecType::kH264) {
    switch (nal.nal_type) {
      case 7:
        return kSps;
      case 8:
        return kPps;
      case 5:
        return kIdr;
      case 6:
        return kSei;
      default:
        return kNone;
    }
  }
  switch (nal.nal_type) {
    case 32:
      return kVps;
    case 33:
      return kSps;
    case 34:
      return kPps;
    case 19:
    case 20:
    case 21:
      return kIdr;
    case 39:
    case 40:
      return kSei;
    default:
      return kNone;
  }
}

// Names are immutable, so build the QString table once instead of converting
// a std::string for every visible cell on every repaint.
const QString& TypeNameText(const NALUnit& nal) {
  static const std::array<QString, 32> h264 = [] {
    std::array<QString, 32> names;
    NALUnit unit;
    unit.codec = CodecType::kH264;
    for (uint8_t type = 0; type < 32; type++) {
      unit.nal_type = type;
      names[type] = QString::fromStdString(unit.TypeName());
    }
    return names;
  }();
  static const std::array<QString, 64> h265 = [] {
    std::array<QString, 64> names;
    NALUnit unit;
    unit.codec = CodecType::kH265;
    for (uint8_t type = 0; type < 64; type++) {
      unit.nal_type = type;
      names[type] = QString::fromStdString(unit.TypeName());
    }
    return names;
  }();
  if (nal.codec == CodecType::kH264) {
    return h264[nal.nal_type & 31];
  }
  return h265[nal.nal_type & 63];
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
    for (const auto& nal : *nals_) {
      if (nal.frame_index > max_frame) {
        max_frame = nal.frame_index;
      }
    }
  }
  frame_start_.assign((size_t)max_frame + 1, -1);
  if (nals_) {
    for (int row = 0; row < (int)nals_->size(); row++) {
      const int f = (*nals_)[row].frame_index;
      if (f >= 0 && frame_start_[(size_t)f] < 0) {
        frame_start_[(size_t)f] = row;
      }
    }
  }
  endResetModel();
}

void NalListModel::SetHighlightedFrame(int frame_index) {
  if (highlighted_frame_ == frame_index) {
    return;
  }
  const int old = highlighted_frame_;
  highlighted_frame_ = frame_index;
  if (!nals_ || nals_->empty()) {
    return;
  }
  const auto notify = [this](int frame) {
    const int first = FirstRowForFrame(frame);
    if (first < 0) {
      return;
    }
    int last = first;
    while (last + 1 < (int)nals_->size() &&
           (*nals_)[(size_t)last + 1].frame_index == frame) {
      last++;
    }
    emit dataChanged(index(first, 0), index(last, columnCount() - 1),
                     {Qt::BackgroundRole});
  };
  notify(old);
  notify(frame_index);
}

int NalListModel::FirstRowForFrame(int frame_index) const {
  if (frame_index < 0 || (size_t)frame_index >= frame_start_.size()) {
    return -1;
  }
  return frame_start_[(size_t)frame_index];
}

int NalListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid() || !nals_) {
    return 0;
  }
  return (int)nals_->size();
}

int NalListModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : 8;
}

QVariant NalListModel::data(const QModelIndex& index, int role) const {
  if (!nals_ || !index.isValid()) {
    return {};
  }
  const int row = index.row();
  if (row < 0 || row >= (int)nals_->size()) {
    return {};
  }
  const NALUnit& nal = (*nals_)[(size_t)row];

  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case 0:
        return row;
      case 1:
        return nal.nal_type;
      case 2:
        return TypeNameText(nal);
      case 3:
        return static_cast<qulonglong>(nal.size);
      case 4:
        return QString("0x%1").arg(nal.offset, 8, 16, QChar('0')).toUpper();
      case 5:
        return nal.frame_type ? QChar(nal.frame_type) : QChar('-');
      case 6:
        return nal.dts_index >= 0 ? QVariant(nal.dts_index)
                                  : QVariant(QChar('-'));
      case 7:
        return nal.frame_index >= 0 ? QVariant(nal.frame_index)
                                    : QVariant(QChar('-'));
      default:
        return QString();
    }
  }

  if (role == Qt::BackgroundRole) {
    if (nal.frame_index >= 0 && nal.frame_index == highlighted_frame_) {
      static const QColor kHighlight(255, 165, 30);
      return kHighlight;
    }
    const QColor& color = color_for(nal);
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
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
    return {};
  }
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
