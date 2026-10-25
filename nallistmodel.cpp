#include "nallistmodel.h"

#include <QColor>
#include <QFont>

namespace {

QColor colorFor(const NALUnit &nal) {
    if (nal.codec == CodecType::H264) {
        switch (nal.nalType) {
            case 7: return QColor(220, 200, 255);
            case 8: return QColor(200, 255, 220);
            case 5: return QColor(255, 200, 200);
            case 6: return QColor(255, 255, 200);
            default: return {};
        }
    }
    switch (nal.nalType) {
        case 32: return QColor(255, 220, 255);
        case 33: return QColor(220, 200, 255);
        case 34: return QColor(200, 255, 220);
        case 19: case 20: case 21: return QColor(255, 200, 200);
        case 39: case 40: return QColor(255, 255, 200);
        default: return {};
    }
}

} // namespace

NalListModel::NalListModel(QObject *parent) : QAbstractTableModel(parent) {}

void NalListModel::setNalUnits(const std::vector<NALUnit> *nals) {
    beginResetModel();
    m_nals = nals;
    m_frameStart.clear();
    m_highlightedFrame = -1;

    int maxFrame = -1;
    if (m_nals) {
        for (const auto &nal : *m_nals)
            if (nal.frameIndex > maxFrame) maxFrame = nal.frameIndex;
    }
    m_frameStart.assign((size_t)maxFrame + 1, -1);
    if (m_nals) {
        for (int row = 0; row < (int)m_nals->size(); row++) {
            const int f = (*m_nals)[row].frameIndex;
            if (f >= 0 && m_frameStart[(size_t)f] < 0) m_frameStart[(size_t)f] = row;
        }
    }
    endResetModel();
}

void NalListModel::setHighlightedFrame(int frameIndex) {
    if (m_highlightedFrame == frameIndex) return;
    const int old = m_highlightedFrame;
    m_highlightedFrame = frameIndex;
    if (m_nals && !m_nals->empty()) {
        emit dataChanged(index(0, 0), index((int)m_nals->size() - 1, columnCount() - 1),
                         {Qt::BackgroundRole});
        Q_UNUSED(old);
    }
}

int NalListModel::firstRowForFrame(int frameIndex) const {
    if (frameIndex < 0 || (size_t)frameIndex >= m_frameStart.size()) return -1;
    return m_frameStart[(size_t)frameIndex];
}

int NalListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid() || !m_nals) return 0;
    return (int)m_nals->size();
}

int NalListModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : 9;
}

QVariant NalListModel::data(const QModelIndex &index, int role) const {
    if (!m_nals || !index.isValid()) return {};
    const int row = index.row();
    if (row < 0 || row >= (int)m_nals->size()) return {};
    const NALUnit &nal = (*m_nals)[(size_t)row];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return row;
            case 1: return nal.nalType;
            case 2: return QString::fromStdString(nal.typeName());
            case 3: return QString::number(nal.size);
            case 4:
                return QString("0x%1").arg(nal.offset, 8, 16, QChar('0')).toUpper();
            case 5: return nal.frameType ? QString(QChar(nal.frameType)) : QString("-");
            case 6: return nal.dtsIndex >= 0 ? QString::number(nal.dtsIndex) : QString("-");
            case 7: return nal.frameIndex >= 0 ? QString::number(nal.frameIndex) : QString("-");
            default: return QString();
        }
    }

    if (role == Qt::BackgroundRole) {
        if (nal.frameIndex >= 0 && nal.frameIndex == m_highlightedFrame)
            return QColor(255, 165, 30);
        const QColor color = colorFor(nal);
        // Return a null QVariant (not an invalid QColor) so the view keeps its
        // default background instead of painting the row black.
        return color.isValid() ? QVariant(color) : QVariant();
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
            case 0: case 1: case 3: case 4: case 6: case 7:
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

QVariant NalListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
    switch (section) {
        case 0: return "#";
        case 1: return "NAL Type";
        case 2: return "NAL Name";
        case 3: return "Size";
        case 4: return "Offset";
        case 5: return "Frame Type";
        case 6: return "DTS";
        case 7: return "PTS";
        default: return QString();
    }
}
