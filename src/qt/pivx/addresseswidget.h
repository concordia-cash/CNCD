// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ADDRESSESWIDGET_H
#define ADDRESSESWIDGET_H

#include "qt/pivx/pwidget.h"
#include "addresstablemodel.h"
#include "qt/pivx/tooltipmenu.h"
#include "furabstractlistitemdelegate.h"
#include "qt/pivx/addressfilterproxymodel.h"

#include <QWidget>

class AddressViewDelegate;
class TooltipMenu;
class PIVXGUI;
class WalletModel;

namespace Ui {
class AddressesWidget;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class AddressesWidget : public PWidget
{
    Q_OBJECT

public:
    explicit AddressesWidget(PIVXGUI* parent);
    ~AddressesWidget();

    void loadWalletModel() override;
    void onNewContactClicked();

private Q_SLOTS:
    void handleAddressClicked(const QModelIndex &index);
    void onStoreContactClicked();
    void onEditClicked();
    void onDeleteClicked();
    void onCopyClicked();
    void onAddContactShowHideClicked();
    void onSortChanged(int idx);
    void onSortOrderChanged(int idx);

    void changeTheme(bool isLightTheme, QString &theme) override;
private:
    Ui::AddressesWidget *ui;

    FurAbstractListItemDelegate* delegate = nullptr;
    AddressTableModel* addressTablemodel = nullptr;
    AddressFilterProxyModel *filter = nullptr;

    bool isOnMyAddresses = true;
    TooltipMenu* menu = nullptr;

    // Cached index
    QModelIndex index;

    // Cached sort type and order
    AddressTableModel::ColumnIndex sortType = AddressTableModel::Label;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;

    void updateListView();
    void sortAddresses();
};

#endif // ADDRESSESWIDGET_H
