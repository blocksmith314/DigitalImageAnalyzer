#pragma once

#include <QAbstractTableModel>
#include <vector>

#include "core/nalunit.h"

// Read-only, virtualized model over all NAL units: every NAL has a row but
// only visible rows are queried, so large streams stay responsive and frame
// highlighting works for any frame index.
class NalListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit NalListModel(QObject *parent = nullptr);

    void setNalUnits(const std::vector<NALUnit> *nals);
    void setHighlightedFrame(int frameIndex);
    int firstRowForFrame(int frameIndex) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    const std::vector<NALUnit> *m_nals = nullptr;
    std::vector<int> m_frameStart;   // frame index -> first NAL row
    int m_highlightedFrame = -1;
};
