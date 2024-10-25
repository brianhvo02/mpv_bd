#ifndef PLAYERWINDOW_H
#define PLAYERWINDOW_H

#include <libbluray/bluray.h>

#include <QOpenGLWidget>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include "qthelper.hpp"

#include <QKeyEvent>
#include <QMouseEvent>

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    QImage g;
} graphic_t;

class MpvWidget Q_DECL_FINAL: public QOpenGLWidget {
    Q_OBJECT
public:
    MpvWidget(QWidget *parent = 0, Qt::WindowFlags f = (Qt::WindowType)0);
    ~MpvWidget();
    void command(const QVariant& params);
    void setProperty(const QString& name, const QVariant& value);
    QVariant getProperty(const QString& name) const;
    QSize sizeHint() const { return QSize(640, 360);}
    void open_disc(QString dir, bool skip_first_play);
    void player_end_file();
    void update_player_info();
    void open_menu();
    void open_popup();

    std::vector<graphic_t> graphics;
    bool menu_flush = false;
Q_SIGNALS:
    void durationChanged(int value);
    void positionChanged(int value);
    void menuButton(bool value);
    void popupButton(bool value);
protected:
    void initializeGL() Q_DECL_OVERRIDE;
    void paintGL() Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseDoubleClickEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
private Q_SLOTS:
    void on_mpv_events();
    void maybeUpdate();
private:
    void handle_mpv_event(mpv_event *event);
    static void on_update(void *ctx);
    bool _wait_idle();
    void _play();
    void _read_to_eof();
    BLURAY_CLIP_INFO _get_clip_info();
    BLURAY_TITLE_INFO _get_playlist_info();

    mpv_handle *mpv;
    mpv_render_context *mpv_gl;

    QString dir;
    BLURAY *bd = NULL;
    std::map<bd_event_e, uint32_t> player_info;
    bool seek = false;
    uint32_t sid = 0;
    uint64_t start_time = 0;
};

#endif // PLAYERWINDOW_H
