// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/pivx/addressholder.h"
#include "qt/pivx/qtutils.h"

void AddressHolder::init(QWidget* holder,const QModelIndex &index, bool isHovered, bool isSelected) const {
    MyAddressRow *row = static_cast<MyAddressRow*>(holder);
    QString address = index.data(Qt::DisplayRole).toString();
    QString label = index.sibling(index.row(), AddressTableModel::Label).data(Qt::DisplayRole).toString();
    uint time = index.sibling(index.row(), AddressTableModel::Date).data(Qt::DisplayRole).toUInt();
    QString date = (time == 0) ? "" : GUIUtil::dateTimeStr(QDateTime::fromTime_t(time));
    row->updateView(address, label, date);
}

QColor AddressHolder::rectColor(bool isHovered, bool isSelected) {
    return getRowColor(isLightTheme, isHovered, isSelected);
}
