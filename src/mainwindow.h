#ifndef MainWindow_H
#define MainWindow_H

#include <QtWidgets/QWidget>
#include "mpvwidget.h"

#include <QPushButton>
#include <QCheckBox>
#include <QSlider>
#include <QLayout>
#include <QFileDialog>
#include <QStackedLayout>
#include <QLabel>
#include <QPainter>

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);
public Q_SLOTS:
    void openMedia();
    void seek(int pos);
    void pauseResume();
    void openMenu();
    void openPopup();
private Q_SLOTS:
    void setMenuButton(bool value);
    void setPopupButton(bool value);
    void setSliderRange(int duration);
private:
    MpvWidget *m_mpv;
    QHBoxLayout *hb;
    QStackedLayout *s;
    QSlider *m_slider;
    QPushButton *m_openBtn;
    QPushButton *m_playBtn;
    QPushButton *m_menuBtn;
    QPushButton *m_popupBtn;
    QCheckBox *m_firstPlayBox;
};

#endif // MainWindow_H
