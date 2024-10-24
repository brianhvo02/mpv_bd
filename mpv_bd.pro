CONFIG -= app_bundle
QT += openglwidgets

QT_CONFIG -= no-pkg-config
CONFIG += link_pkgconfig
PKGCONFIG += mpv libbluray

HEADERS = \
    src/mpvwidget.h \
    src/mainwindow.h
SOURCES = src/main.cpp \
    src/mpvwidget.cpp \
    src/mainwindow.cpp
