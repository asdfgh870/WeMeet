﻿// Author : lang @ nwpu
// All rights reserved

#ifndef TIMEMESSAGE_H
#define TIMEMESSAGE_H

#include <QWidget>

namespace Ui {
class timemessage;
}

// TimeMessage is a Widget show time in format hh::mm, and can be constructed and added to
// QListView as a QListViewItem
class TimeMessage : public QWidget
{
    Q_OBJECT

public:
    explicit TimeMessage(QWidget *parent = nullptr);
    //set time displayed in hh::mm format, pass in a time seconds from unix epoch
    void setTime(QString timeSecsFromUnixEpoch);

    ~TimeMessage();

private:
    Ui::timemessage *ui;
};

#endif // TIMEMESSAGE_H