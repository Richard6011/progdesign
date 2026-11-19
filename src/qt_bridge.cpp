#include "graphics.h"
#include <QApplication>
#include <QCloseEvent>
#include <QFontMetrics>
#include <QImage>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QThread>
#include <QWidget>
#include <deque>
#include <memory>

namespace {
std::unique_ptr<QApplication> application;
std::deque<UiEvent> events;
int virtual_key(const QKeyEvent *e) {
    if (e->nativeVirtualKey())
        return static_cast<int>(e->nativeVirtualKey());
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return 13;
    case Qt::Key_Escape:
        return 27;
    case Qt::Key_Backspace:
        return 8;
    case Qt::Key_Tab:
        return 9;
    default:
        return e->key();
    }
}

void queue_text(const QString &text) {
    for (auto character : text.toUcs4()) {
        UiEvent e = {};
        e.type = UI_CHAR;
        e.character = character;
        events.push_back(e);
    }
}
class Canvas final : public QWidget {
  public:
    QImage frame;

    Canvas(int w, int h) : frame(w, h, QImage::Format_RGB32) {
        frame.fill(Qt::black);
        setFixedSize(w, h);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.drawImage(0, 0, frame);
    }

    bool event(QEvent *event) override {
        UiEvent e = {};
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (event->type() == QEvent::MouseButtonPress && mouse->button() != Qt::LeftButton)
                break;
            e.type = event->type() == QEvent::MouseMove ? UI_MOVE : UI_CLICK;
            e.x = qRound(mouse->position().x());
            e.y = qRound(mouse->position().y());
            break;
        }
        case QEvent::KeyPress:
        case QEvent::KeyRelease: {
            auto *key = static_cast<QKeyEvent *>(event);
            if (event->type() == QEvent::KeyRelease && key->isAutoRepeat())
                return true;
            e.type = event->type() == QEvent::KeyPress ? UI_KEYDOWN : UI_KEYUP;
            e.key = virtual_key(key);
            e.repeat = key->isAutoRepeat();
            events.push_back(e);
            if (e.type == UI_KEYDOWN && testAttribute(Qt::WA_InputMethodEnabled))
                queue_text(key->text());
            return true;
        }
        case QEvent::InputMethod:
            if (testAttribute(Qt::WA_InputMethodEnabled))
                queue_text(static_cast<QInputMethodEvent *>(event)->commitString());
            return true;
        case QEvent::FocusOut:
            e.type = UI_FOCUS_LOST;
            break;
        case QEvent::Close:
            e.type = UI_CLOSE;
            events.push_back(e);
            static_cast<QCloseEvent *>(event)->ignore();
            return true;
        default:
            break;
        }
        if (e.type)
            events.push_back(e);
        return QWidget::event(event);
    }
};
std::unique_ptr<Canvas> window;
}

extern "C" int gfx_open(int w, int h, const char *title) {
    static int argc = 1;
    static char name[] = "NoteCollision";
    static char *argv[] = {name, nullptr};
    application = std::make_unique<QApplication>(argc, argv);
    application->setQuitOnLastWindowClosed(false);
    window = std::make_unique<Canvas>(w, h);
    window->setWindowTitle(QString::fromUtf8(title));
    window->show();
    window->setFocus();
    return 1;
}

extern "C" void gfx_close(void) {
    window.reset();
    events.clear();
    application.reset();
}

extern "C" int gfx_event(UiEvent *e) {
    if (events.empty())
        QApplication::processEvents();
    if (events.empty())
        return 0;
    *e = events.front();
    events.pop_front();
    return 1;
}

extern "C" void gfx_text_input(int enabled) {
    const bool active = enabled != 0;
    if (window->testAttribute(Qt::WA_InputMethodEnabled) == active)
        return;
    if (!active)
        QGuiApplication::inputMethod()->reset();
    window->setAttribute(Qt::WA_InputMethodEnabled, active);
    QGuiApplication::inputMethod()->update(Qt::ImEnabled);
}

extern "C" void gfx_clear(uint32_t color) { window->frame.fill(QColor::fromRgb(color)); }
extern "C" void gfx_rect(int x, int y, int w, int h, uint32_t color, int radius) {
    QPainter p(&window->frame);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor::fromRgb(color));
    p.drawRoundedRect(QRectF(x, y, w, h), radius / 2.0, radius / 2.0);
}
extern "C" void gfx_line(int x, int y, int x2, int y2, uint32_t color) {
    QPainter p(&window->frame);
    p.setPen(QColor::fromRgb(color));
    p.drawLine(x, y, x2, y2);
}
extern "C" void gfx_text(int x, int y, int size, uint32_t color, const char *text) {
    QPainter p(&window->frame);
    QFont font("Microsoft YaHei UI");
    font.setPixelSize(size);
    p.setFont(font);
    p.setPen(QColor::fromRgb(color));
    p.drawText(x, y + QFontMetrics(font).ascent(), QString::fromUtf8(text));
}
extern "C" void gfx_text_center(int x, int y, int width, int height, int size, uint32_t color,
                                const char *text) {
    QPainter p(&window->frame);
    QFont font("Microsoft YaHei UI");
    font.setPixelSize(size);
    p.setFont(font);
    p.setPen(QColor::fromRgb(color));
    p.drawText(QRect(x, y, width, height), Qt::AlignCenter, QString::fromUtf8(text));
}
extern "C" void gfx_present(void) { window->repaint(); }
extern "C" void gfx_sleep(int ms) { QThread::msleep(static_cast<unsigned long>(ms)); }
extern "C" int gfx_alive(void) { return window && window->isVisible(); }
extern "C" void gfx_error(const char *text) {
    QMessageBox::critical(window.get(), QString::fromUtf8("音符碰撞"), QString::fromUtf8(text));
}
