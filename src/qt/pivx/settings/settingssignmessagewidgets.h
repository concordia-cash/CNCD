// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSSIGNMESSAGEWIDGETS_H
#define SETTINGSSIGNMESSAGEWIDGETS_H

#include <QWidget>
#include "qt/pivx/pwidget.h"
#include "qt/pivx/contactsdropdown.h"

namespace Ui {
class SettingsSignMessageWidgets;
}

class SettingsSignMessageWidgets : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsSignMessageWidgets(PIVXGUI* _window, QWidget *parent = nullptr);
    ~SettingsSignMessageWidgets();

    void setAddress_SM(const QString& address);
    void showEvent(QShowEvent *event) override;
protected:
    void resizeEvent(QResizeEvent *event) override;
public Q_SLOTS:
    void onSignMessageButtonSMClicked();
    void onVerifyMessage();
    void onPasteButtonSMClicked();
    void onAddressBookButtonSMClicked();
    void onGoClicked();
    void onClearAll();
    void onAddressesClicked();
    void onModeSelected(bool isSign);
    void updateMode();
private:
    Ui::SettingsSignMessageWidgets *ui;
    QAction *btnContact;
    ContactsDropdown *menuContacts = nullptr;
    bool isSign = true;
    void resizeMenu();
};

#endif // SETTINGSSIGNMESSAGEWIDGETS_H
