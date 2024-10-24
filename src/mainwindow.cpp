#include "mainwindow.h"
#include <iostream>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
    m_mpv = new MpvWidget(this);
    m_slider = new QSlider();
    m_slider->setOrientation(Qt::Horizontal);
    m_openBtn = new QPushButton("Open");
    m_playBtn = new QPushButton("Pause");
    m_firstPlayBox = new QCheckBox("Skip First Play");
    m_firstPlayBox->setCheckState(Qt::Checked);
    m_menuBtn = new QPushButton("Main Menu");
    m_popupBtn = new QPushButton("Popup Menu");
    m_popupBtn->setVisible(false);
    hb = new QHBoxLayout();
    hb->addWidget(m_openBtn);
    hb->addWidget(m_firstPlayBox);
    hb->addWidget(m_playBtn);
    hb->addWidget(m_popupBtn);
    QVBoxLayout *vl = new QVBoxLayout();
    vl->addWidget(m_mpv);
    vl->addWidget(m_slider);
    vl->addLayout(hb);
    setLayout(vl);
    connect(m_slider, SIGNAL(sliderMoved(int)), SLOT(seek(int)));
    connect(m_openBtn, SIGNAL(clicked()), SLOT(openMedia()));
    connect(m_playBtn, SIGNAL(clicked()), SLOT(pauseResume()));
    connect(m_menuBtn, SIGNAL(clicked()), SLOT(openMenu()));
    connect(m_popupBtn, SIGNAL(clicked()), SLOT(openPopup()));
    connect(m_mpv, SIGNAL(positionChanged(int)), m_slider, SLOT(setValue(int)));
    connect(m_mpv, SIGNAL(durationChanged(int)), this, SLOT(setSliderRange(int)));
    connect(m_mpv, SIGNAL(menuButton(bool)), this, SLOT(setMenuButton(bool)));
    connect(m_mpv, SIGNAL(popupButton(bool)), this, SLOT(setPopupButton(bool)));
}

void MainWindow::openMedia() {
    QString dir = QFileDialog::getExistingDirectory(0, "Open disc", "/mnt/c/Users/Brian Vo/Videos/Volume 1", QFileDialog::ShowDirsOnly);
    if (dir.isEmpty() || !std::filesystem::exists(dir.toStdString() + "/BDMV"))
        return;
    m_mpv->open_disc(dir, m_firstPlayBox->checkState() == Qt::Checked);
    hb->addWidget(m_menuBtn);
}

void MainWindow::seek(int pos) {
    m_mpv->command(QVariantList() << "seek" << pos << "absolute");
}

void MainWindow::pauseResume() {
    const bool paused = m_mpv->getProperty("pause").toBool();
    m_mpv->setProperty("pause", !paused);
}

void MainWindow::openMenu() {
    m_mpv->open_menu();
}

void MainWindow::openPopup() {
    m_mpv->open_popup();
}

void MainWindow::setSliderRange(int duration) {
    m_slider->setRange(0, duration);
}

void MainWindow::setMenuButton(bool value) {
    m_menuBtn->setVisible(value);
}

void MainWindow::setPopupButton(bool value) {
    m_popupBtn->setVisible(value);
}