#include "mpvwidget.h"
#include "mainwindow.h"

#include <map>
#include <iostream>
#include <filesystem>

#include <libbluray/overlay.h>
#include <libbluray/keys.h>

#include <stdexcept>
#include <QtGui/QOpenGLContext>
#include <QtCore/QMetaObject>
#include <QGuiApplication>
#include <QScreen>
#include <QOpenGLTexture>
#include <QPainter>
#include <QWindow>

static void wakeup(void *ctx) {
    QMetaObject::invokeMethod((MpvWidget*)ctx, "on_mpv_events", Qt::QueuedConnection);
}

static void *get_proc_address(void *ctx, const char *name) {
    Q_UNUSED(ctx);
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (!glctx)
        return nullptr;
    return reinterpret_cast<void *>(glctx->getProcAddress(QByteArray(name)));
}

static void _argb_overlay_cb(void *h, const struct bd_argb_overlay_s * const ov) {
    (void)h;

    if (ov) {
      printf("ARGB OVERLAY @%ld p%d %d: %d,%d %dx%d\n", (long)ov->pts, ov->plane, ov->cmd, ov->x, ov->y, ov->w, ov->h);

    } else {
        printf("ARGB OVERLAY CLOSE\n");
    }
}

MpvWidget::MpvWidget(QWidget *parent, Qt::WindowFlags f): QOpenGLWidget(parent, f) {
    mpv = mpv_create();
    if (!mpv)
        throw std::runtime_error("could not create mpv context");

    // mpv_set_option_string(mpv, "terminal", "yes");
    mpv_set_option_string(mpv, "msg-level", "all=v");
    mpv_set_option_string(mpv, "vo", "libmpv");
    int flag = 1;
    mpv_set_option(mpv, "orawts", MPV_FORMAT_FLAG, &flag);
    if (mpv_initialize(mpv) < 0)
        throw std::runtime_error("could not initialize mpv context");

    // Request hw decoding, just for testing.
    mpv::qt::set_option_variant(mpv, "hwdec", "auto");

    mpv_observe_property(mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_set_wakeup_callback(mpv, wakeup, this);
    setFocusPolicy(Qt::StrongFocus);
}

MpvWidget::~MpvWidget() {
    makeCurrent();
    if (mpv_gl)
        mpv_render_context_free(mpv_gl);
    mpv_terminate_destroy(mpv);
}

void MpvWidget::command(const QVariant& params) {
    mpv::qt::command_variant(mpv, params);
}

void MpvWidget::setProperty(const QString& name, const QVariant& value) {
    mpv::qt::set_property_variant(mpv, name, value);
}

QVariant MpvWidget::getProperty(const QString &name) const {
    return mpv::qt::get_property_variant(mpv, name);
}

void MpvWidget::initializeGL() {
    mpv_opengl_init_params gl_init_params[1] = {get_proc_address, nullptr};
    mpv_render_param params[]{
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (mpv_render_context_create(&mpv_gl, mpv, params) < 0)
        throw std::runtime_error("failed to initialize mpv GL context");
    mpv_render_context_set_update_callback(mpv_gl, MpvWidget::on_update, reinterpret_cast<void *>(this));
}

void MpvWidget::paintGL() {
    mpv_opengl_fbo mpfbo {
        static_cast<int>(defaultFramebufferObject()), 
        static_cast<int>(width() * devicePixelRatio()), 
        static_cast<int>(height() * devicePixelRatio()),
        0
    };

    int flip_y{1};

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    
    mpv_render_context_render(mpv_gl, params);

    if (!menu_flush) return;

    double rx = width() / getProperty("width").toDouble();
    double ry = height() / getProperty("height").toDouble();

    QPainter p(this);
    for (auto graphic : graphics) {
        QRectF target(graphic.x * rx, graphic.y * ry, graphic.w * rx, graphic.h * ry);
        p.drawImage(target, graphic.g);
    }
}

void MpvWidget::keyPressEvent(QKeyEvent *event) {
    if (bd == NULL) return;

    uint64_t sec = getProperty("time-pos").toUInt();
    BLURAY_CLIP_INFO clip_info = _get_clip_info();
    uint64_t pts = clip_info.in_time + sec * 45000;

    switch (event->key()) {
        case Qt::Key_Left:
            bd_user_input(bd, pts, BD_VK_LEFT);
            break;
        case Qt::Key_Right:
            bd_user_input(bd, pts, BD_VK_RIGHT);
            break;
        case Qt::Key_Up:
            bd_user_input(bd, pts, BD_VK_UP);
            break;
        case Qt::Key_Down:
            bd_user_input(bd, pts, BD_VK_DOWN);
            break;
        case Qt::Key_Return:
            bd_user_input(bd, pts, BD_VK_ENTER);
            if (_wait_idle())
                _play();
            
            break;

        default: ;
    }
}

void MpvWidget::mousePressEvent(QMouseEvent *event) {
    (void)event;
    if (bd == NULL || !player_info[BD_EVENT_MENU])
        return;
    uint64_t sec = getProperty("time-pos").toUInt();
    BLURAY_CLIP_INFO clip_info = _get_clip_info();
    uint64_t pts = clip_info.in_time + sec * 45000;
    double rx = getProperty("width").toDouble() / width();
    double ry = getProperty("height").toDouble() / height();
    QPointF point = event->pos();
    bd_mouse_select(bd, pts, point.x() * rx, point.y() * ry);
}

void MpvWidget::mouseDoubleClickEvent(QMouseEvent *event) {
    (void)event;
    if (bd == NULL || !player_info[BD_EVENT_MENU])
        return;
    uint64_t sec = getProperty("time-pos").toUInt();
    BLURAY_CLIP_INFO clip_info = _get_clip_info();
    uint64_t pts = clip_info.in_time + sec * 45000;
    bd_user_input(bd, pts, BD_VK_MOUSE_ACTIVATE);
    if (_wait_idle())
        _play();
}

void MpvWidget::on_mpv_events() {
    // Process all events, until the event queue is empty.
    while (mpv) {
        mpv_event *event = mpv_wait_event(mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        handle_mpv_event(event);
    }
}

void MpvWidget::handle_mpv_event(mpv_event *event) {
    switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            mpv_event_property *prop = (mpv_event_property *)event->data;
            if (strcmp(prop->name, "time-pos") == 0) {
                if (prop->format == MPV_FORMAT_DOUBLE) {
                    double time = *(double *)prop->data;
                    Q_EMIT positionChanged(time);
                }
                
                if (bd != NULL && !player_info[BD_EVENT_TITLE])
                    update_player_info();
            } else if (strcmp(prop->name, "duration") == 0) {
                if (prop->format == MPV_FORMAT_DOUBLE) {
                    double time = *(double *)prop->data;
                    Q_EMIT durationChanged(time);
                }
            }
            break;
        }
        case MPV_EVENT_SEEK: {
            seek = true;
            break;
        }
        case MPV_EVENT_PLAYBACK_RESTART: {
            // setMinimumSize(640, 360);
            // setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            if (!seek) break;
            update_player_info();
            seek = false;
            break;
        }
        case MPV_EVENT_END_FILE: {
            auto data = (mpv_event_end_file *)event->data;
            if (data->reason == MPV_END_FILE_REASON_EOF)
                player_end_file();
            break;
        }
        case MPV_EVENT_FILE_LOADED: {
            // QRect screenGeometry = screen()->geometry();
            // if (getProperty("width").toInt() > screenGeometry.width() || getProperty("height").toInt() > screenGeometry.height())
            //     ((MainWindow*)parentWidget())->resize(screenGeometry.width(), screenGeometry.height());
            // else setFixedSize(getProperty("width").toInt(), getProperty("height").toInt());

            if (start_time) {
                setProperty("time-pos", (qulonglong)start_time);
                start_time = 0;
            }
            
            break;
        }
        default: ;
        // Ignore uninteresting or unknown events.
    }
}

// Make Qt invoke mpv_render_context_render() to draw a new/updated video frame.
void MpvWidget::maybeUpdate() {
    // If the Qt window is not visible, Qt's update() will just skip rendering.
    // This confuses mpv's render API, and may lead to small occasional
    // freezes due to video rendering timing out.
    // Handle this by manually redrawing.
    // Note: Qt doesn't seem to provide a way to query whether update() will
    //       be skipped, and the following code still fails when e.g. switching
    //       to a different workspace with a reparenting window manager.
    if (window()->isMinimized()) {
        makeCurrent();
        paintGL();
        context()->swapBuffers(context()->surface());
        doneCurrent();
    } else {
        update();
    }
}

void MpvWidget::on_update(void *ctx) {
    QMetaObject::invokeMethod((MpvWidget*)ctx, "maybeUpdate");
}

#define PRINT_EV0(e)                                \
  case BD_EVENT_##e:                                \
      printf(#e "\n");                              \
      break
#define PRINT_EV1(e,f)                              \
  case BD_EVENT_##e:                                \
    printf("%-25s " f "\n", #e ":", ev.param);     \
      break

bool MpvWidget::_wait_idle() {
    BD_EVENT ev;
    bool new_play = false;

    do {
        bd_read_ext(bd, NULL, 0, &ev);
        switch ((bd_event_e)ev.event) {

            case BD_EVENT_NONE:
            case BD_EVENT_SEEK:
                break;

            /* errors */

            PRINT_EV1(ERROR,      "%u");
            PRINT_EV1(READ_ERROR, "%u");
            PRINT_EV1(ENCRYPTED,  "%u");

            /* current playback position */

            // PRINT_EV1(ANGLE,    "%u");
            // PRINT_EV1(TITLE,    "%u");
            // PRINT_EV1(PLAYLIST, "%05u.mpls");
            // PRINT_EV1(PLAYITEM, "%u");
            // PRINT_EV1(PLAYMARK, "%u");
            // PRINT_EV1(CHAPTER,  "%u");
            case BD_EVENT_ANGLE:
            case BD_EVENT_TITLE:
            case BD_EVENT_PLAYLIST:
            case BD_EVENT_PLAYITEM:
            case BD_EVENT_PLAYMARK:
            case BD_EVENT_CHAPTER:
                new_play = true;
                break;
            
            PRINT_EV0(END_OF_TITLE);

            PRINT_EV1(STEREOSCOPIC_STATUS,  "%u");

            // PRINT_EV1(SEEK,     "%u");
            PRINT_EV0(DISCONTINUITY);
            // PRINT_EV0(PLAYLIST_STOP);
            case BD_EVENT_PLAYLIST_STOP:
                _wait_idle();
                _play();
                return false;

            /* Interactive */

            PRINT_EV1(STILL_TIME,           "%u");
            PRINT_EV1(STILL,                "%u");
            PRINT_EV1(SOUND_EFFECT,         "%u");
            PRINT_EV1(IDLE,                 "%u");
            // PRINT_EV1(POPUP,                "%u");
            case BD_EVENT_POPUP:
                Q_EMIT popupButton(ev.param);
                break;
            PRINT_EV1(MENU,                 "%u");
            // PRINT_EV1(UO_MASK_CHANGED,      "0x%04x");
            case BD_EVENT_UO_MASK_CHANGED:
                Q_EMIT menuButton(!(ev.param & BLURAY_UO_MENU_CALL));
                break;
            PRINT_EV1(KEY_INTEREST_TABLE,   "0x%04x");

            /* stream selection */

            // PRINT_EV1(PG_TEXTST,              "%u");
            case BD_EVENT_PG_TEXTST:
                setProperty("sid", ev.param ? sid : 0);
                break;
            PRINT_EV1(SECONDARY_AUDIO,        "%u");
            PRINT_EV1(SECONDARY_VIDEO,        "%u");
            PRINT_EV1(PIP_PG_TEXTST,          "%u");

            // PRINT_EV1(AUDIO_STREAM,           "%u");
            case BD_EVENT_AUDIO_STREAM:
                setProperty("aid", ev.param);
                break;
            PRINT_EV1(IG_STREAM,              "%u");
            // PRINT_EV1(PG_TEXTST_STREAM,       "%u");
            case BD_EVENT_PG_TEXTST_STREAM:
                sid = ev.param;
                if (player_info[BD_EVENT_PG_TEXTST])
                    setProperty("sid", ev.param);
                break;
            PRINT_EV1(SECONDARY_AUDIO_STREAM, "%u");
            PRINT_EV1(SECONDARY_VIDEO_STREAM, "%u");
            PRINT_EV1(SECONDARY_VIDEO_SIZE,   "%u");
            PRINT_EV1(PIP_PG_TEXTST_STREAM,   "%u");
        }
        fflush(stdout);
        
        player_info[(bd_event_e)ev.event] = ev.param;
    } while (ev.event != BD_EVENT_NONE && ev.event != BD_EVENT_ERROR);

    return new_play;
}

BLURAY_TITLE_INFO MpvWidget::_get_playlist_info() {
    return *bd_get_playlist_info(bd, 
        player_info[BD_EVENT_PLAYLIST], player_info[BD_EVENT_ANGLE]);
}

BLURAY_CLIP_INFO MpvWidget::_get_clip_info() {
    BLURAY_TITLE_INFO playlist_info = _get_playlist_info();
    
    return playlist_info.clips[player_info[BD_EVENT_PLAYITEM]];
}

void MpvWidget::_play() {
    BLURAY_TITLE_INFO playlist_info = _get_playlist_info();
    BLURAY_CLIP_INFO clip_info = _get_clip_info();
    
    if (player_info[BD_EVENT_TITLE] != 0) {
        uint64_t chapter_start = playlist_info.chapters[player_info[BD_EVENT_CHAPTER] - 1].start;
        for (uint i = 0; i < player_info[BD_EVENT_PLAYITEM]; i++) {
            BLURAY_CLIP_INFO clip_info = playlist_info.clips[i];
            chapter_start -= clip_info.out_time - clip_info.in_time;
        }
        start_time = chapter_start / 90000;
    }
    QString filepath = dir + "/BDMV/STREAM/" + QString::fromUtf8(clip_info.clip_id) + ".m2ts";
    command(QStringList() << "loadfile" << filepath);
}

static void _overlay_cb(void *h, const struct bd_overlay_s * const ov) {
    MpvWidget *m_mpv = (MpvWidget *)h;

    if (ov) {
        // printf("OVERLAY @%ld p%d %d: %d,%d %dx%d\n", (long)ov->pts, ov->plane, ov->cmd, ov->x, ov->y, ov->w, ov->h);

        if (ov->cmd == BD_OVERLAY_CLEAR || ov->cmd == BD_OVERLAY_CLOSE) {
            m_mpv->menu_flush = false;
            return m_mpv->graphics.clear();
        }
        if (ov->cmd == BD_OVERLAY_FLUSH)
            m_mpv->menu_flush = true;
        if (ov->cmd == BD_OVERLAY_CLEAR)
            return m_mpv->graphics.clear();
        if (ov->cmd != BD_OVERLAY_DRAW) return;

        int pixels_drawn = 0;
        int img_idx = 0;

        double kr;
        double kg;
        double kb;

        double offset_y = 16;
        double scale_y = 255.0 / 219.0;
        double scale_uv = 255.0 / 112.0;

        QImage *graphic = new QImage(ov->w, ov->h, QImage::Format_Indexed8);

        if (ov->h >= 600) {
            kr = 0.2126;
            kg = 0.7152;
            kb = 0.0722;
        } else {
            kr = 0.299;
            kg = 0.587;
            kb = 0.114;
        }

        QList<QRgb> palettes;
        for (int i = 0; i < 256; i++) {
            const BD_PG_PALETTE_ENTRY palette = ov->palette[i];
            
            double sy = scale_y * (palette.Y - offset_y);
            double scb = scale_uv * (palette.Cb - 128);
            double scr = scale_uv * (palette.Cr - 128);

            int r = sy                            + scr * (1 - kr);
            int g = sy - scb * (1 - kb) * kb / kg - scr * (1 - kr) * kr / kg;
            int b = sy + scb * (1 - kb);

            r = std::max(0, std::min(255, r));
            g = std::max(0, std::min(255, g));
            b = std::max(0, std::min(255, b));

            palettes.append(qRgba(r, g, b, palette.T));
        }

        graphic->setColorTable(palettes);

        int x = 0;
        int y = 0;
        
        while (pixels_drawn < ov->w * ov->h) {
            const BD_PG_RLE_ELEM img = ov->img[img_idx++];

            for (int i = 0; i < img.len; i++) {
                graphic->setPixel(x++, y, img.color);
                if (x == ov->w) {
                    x = 0;
                    y++;
                }
            }

            pixels_drawn += img.len;
        }

        m_mpv->graphics.push_back(graphic_t({
            .x = ov->x, .y = ov->y,
            .w = ov->w, .h = ov->h,
            .g = *graphic
        }));
    } else {
        printf("OVERLAY CLOSE\n");
    }
}

void MpvWidget::open_disc(QString bd_dir, bool skip_first_play) {
    printf("Opening %s\n", bd_dir.toLocal8Bit().data());

    if (bd != NULL)
        bd_close(bd);

    bd = bd_open(bd_dir.toLocal8Bit().data(), NULL);
    const BLURAY_DISC_INFO *disc_info = bd_get_disc_info(bd);
    if (!disc_info->bluray_detected) {
        std::cout << "Could not open disc." << std::endl;
        return;
    }

    dir = bd_dir;

    bd_get_event(bd, NULL);
    bd_register_overlay_proc(bd, this, _overlay_cb);
    bd_register_argb_overlay_proc(bd, this, _argb_overlay_cb, NULL);

    bd_play(bd);
    
    player_info = std::map<bd_event_e, uint32_t>(); 
    _wait_idle();

    if (disc_info->first_play_supported && skip_first_play)
        bd_seek(bd, bd_get_title_size(bd) - 1);

    _wait_idle();
    player_end_file();

    _play();
}

void MpvWidget::_read_to_eof() {
    BD_EVENT ev;
    int      bytes;
    uint64_t total = 0;
    uint8_t  buf[1];

    bd_seek(bd, bd_get_title_size(bd) - 1);

    do {
        bytes = bd_read_ext(bd, buf, 1, &ev);
        total += bytes < 0 ? 0 : bytes;
        switch ((bd_event_e)ev.event) {
            case BD_EVENT_NONE:
            case BD_EVENT_SEEK:
                break;

            /* errors */

            PRINT_EV1(ERROR,      "%u");
            PRINT_EV1(READ_ERROR, "%u");
            PRINT_EV1(ENCRYPTED,  "%u");

            /* current playback position */

            PRINT_EV1(ANGLE,    "%u");
            PRINT_EV1(TITLE,    "%u");
            PRINT_EV1(PLAYLIST, "%05u.mpls");
            PRINT_EV1(PLAYITEM, "%u");
            PRINT_EV1(PLAYMARK, "%u");
            PRINT_EV1(CHAPTER,  "%u");
            PRINT_EV0(END_OF_TITLE);

            PRINT_EV1(STEREOSCOPIC_STATUS,  "%u");

            // PRINT_EV1(SEEK,     "%u");
            PRINT_EV0(DISCONTINUITY);
            PRINT_EV0(PLAYLIST_STOP);

            /* Interactive */

            PRINT_EV1(STILL_TIME,           "%u");
            PRINT_EV1(STILL,                "%u");
            PRINT_EV1(SOUND_EFFECT,         "%u");
            PRINT_EV1(IDLE,                 "%u");
            PRINT_EV1(POPUP,                "%u");
            PRINT_EV1(MENU,                 "%u");
            PRINT_EV1(UO_MASK_CHANGED,      "0x%04x");
            PRINT_EV1(KEY_INTEREST_TABLE,   "0x%04x");

            /* stream selection */

            PRINT_EV1(PG_TEXTST,              "%u");
            PRINT_EV1(SECONDARY_AUDIO,        "%u");
            PRINT_EV1(SECONDARY_VIDEO,        "%u");
            PRINT_EV1(PIP_PG_TEXTST,          "%u");

            PRINT_EV1(AUDIO_STREAM,           "%u");
            PRINT_EV1(IG_STREAM,              "%u");
            PRINT_EV1(PG_TEXTST_STREAM,       "%u");
            PRINT_EV1(SECONDARY_AUDIO_STREAM, "%u");
            PRINT_EV1(SECONDARY_VIDEO_STREAM, "%u");
            PRINT_EV1(SECONDARY_VIDEO_SIZE,   "%u");
            PRINT_EV1(PIP_PG_TEXTST_STREAM,   "%u");
        }
        fflush(stdout);

        if (ev.event == BD_EVENT_END_OF_TITLE)
            break;
        
        player_info[(bd_event_e)ev.event] = ev.param;
    } while (bytes >= 0);

    printf("_read_to_eof(): read %" PRIu64 " bytes\n", total);
}

void MpvWidget::update_player_info() {
    if (bd == NULL) return;
    uint64_t sec = getProperty("time-pos").toUInt();
    BLURAY_CLIP_INFO clip_info = _get_clip_info();
    bd_seek_time(bd, clip_info.start_time + sec * 45000);
    _wait_idle();
}

void MpvWidget::player_end_file() {
    BLURAY_CLIP_INFO clip_info = _get_clip_info();
    uint64_t time = clip_info.start_time + clip_info.out_time - clip_info.in_time;
    if (time == _get_playlist_info().duration) {
        _read_to_eof();
        _wait_idle();
    } else bd_seek_time(bd, time);
    _wait_idle();
    _play();
}

void MpvWidget::open_menu() {
    uint64_t sec = getProperty("time-pos").toUInt();
    BLURAY_CLIP_INFO clip_info = _get_clip_info();
    bd_menu_call(bd, clip_info.in_time + sec * 45000);
    _wait_idle();
    _play();
}

void MpvWidget::open_popup() {
    uint64_t sec = getProperty("time-pos").toUInt();
    BLURAY_CLIP_INFO clip_info = _get_clip_info();
    uint64_t pts = clip_info.in_time + sec * 45000;

    bd_user_input(bd, pts, BD_VK_POPUP);
    _wait_idle();
}