// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/pivx/contactdropdownrow.h"
#include "qt/pivx/forms/ui_contactdropdownrow.h"

ContactDropdownRow::ContactDropdownRow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ContactDropdownRow)
{
    ui->setupUi(this);
    ui->lblAddress->setProperty("cssClass", "text-list-contact-body1");
    ui->lblLabel->setProperty("cssClass", "text-list-contact-title1");
}

void ContactDropdownRow::init(bool isLightTheme, bool isHover) {
    update(isLightTheme, isHover, false);
}

void ContactDropdownRow::update(bool isLightTheme, bool isHover, bool isSelected){
    ui->lblDivisory->setStyleSheet("background-color:#bababa");
}

void ContactDropdownRow::setData(QString address, QString label){
    ui->lblAddress->setText(address);
    ui->lblLabel->setText(label);
}

ContactDropdownRow::~ContactDropdownRow()
{
    delete ui;
}
