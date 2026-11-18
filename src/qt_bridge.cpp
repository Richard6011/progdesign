/* Qt 的薄 C ABI：只转发窗口、绘图、输入。游戏规则、页面和网络均在 .c 中。
 * 保留原 gfx_* 接口，C 代码无需理解 Qt 对象、信号槽或事件循环。 */
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

// Qt 键值与 Windows VK 不同；原 C 层使用 VK，普通按键优先取系统键码。
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

// 输入统一排队交给 C 主循环；UTF-32 保证一个中文字符对应一个 UI_CHAR。
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

    // 使用固定逻辑尺寸沿用已有 C 页面布局；QImage 用作离屏画布。
    Canvas(int w, int h) : frame(w, h, QImage::Format_RGB32) {
        frame.fill(Qt::black);
        setFixedSize(w, h);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        // 默认接收直接击键；只有 C 层确认正在编辑文本框时才开启输入法。
    }

  protected:
    // 只在 paintEvent 中绘制窗口，避免把 QWidget 当成随时可写的画布。
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.drawImage(0, 0, frame);
    }

    // 将 Qt 事件翻译为原有 C 结构；关闭先交给 C 清理音频和联机状态。
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
            // Qt 自动重复包含伪 KeyRelease，不能让长按音符因此被提前松开。
            if (event->type() == QEvent::KeyRelease && key->isAutoRepeat())
                return true;
            e.type = event->type() == QEvent::KeyPress ? UI_KEYDOWN : UI_KEYUP;
            e.key = virtual_key(key);
            e.repeat = key->isAutoRepeat();
            events.push_back(e);
            if (e.type == UI_KEYDOWN && testAttribute(Qt::WA_InputMethodEnabled))
                queue_text(key->text());
            return true; // 自己处理 Tab，防止 QWidget 将其转成焦点导航。
        }
        case QEvent::InputMethod:
            // 切页后迟到的输入法提交不能再当成游戏输入。
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
} // namespace

// QApplication 参数必须活到销毁；不让 Qt 消费 C 主程序的 --data-dir 等参数。
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

// 先销毁 QWidget 再销毁 QApplication，遵守 Qt 的资源生命周期。
extern "C" void gfx_close(void) {
    window.reset();
    events.clear();
    application.reset();
}

// 非阻塞泵送事件，保留 C 主循环，不需要引入信号槽或第二个线程。
extern "C" int gfx_event(UiEvent *e) {
    if (events.empty())
        QApplication::processEvents();
    if (events.empty())
        return 0;
    *e = events.front();
    events.pop_front();
    return 1;
}

// 同一 Canvas 绘制所有页面，焦点对象不会随切页改变，必须显式通知 Qt 更新 IME 状态。
extern "C" void gfx_text_input(int enabled) {
    const bool active = enabled != 0;
    if (window->testAttribute(Qt::WA_InputMethodEnabled) == active)
        return;
    // 先取消未完成的拼音/全角组词，避免进入演奏后输入法继续截走 DFJK。
    if (!active)
        QGuiApplication::inputMethod()->reset();
    window->setAttribute(Qt::WA_InputMethodEnabled, active);
    QGuiApplication::inputMethod()->update(Qt::ImEnabled);
}

// 各绘图函数只改离屏图像；C 层每帧调用一次 gfx_present 提交完整画面。
extern "C" void gfx_clear(uint32_t color) { window->frame.fill(QColor::fromRgb(color)); }
extern "C" void gfx_rect(int x, int y, int w, int h, uint32_t color, int radius) {
    QPainter p(&window->frame);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor::fromRgb(color));
    // 原接口传的是圆角椭圆直径，Qt 接收半径，因此除以 2 保持外观。
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
    // 原接口 y 是文字顶部，Qt drawText 的 y 是基线，需要加字体 ascent。
    p.drawText(x, y + QFontMetrics(font).ascent(), QString::fromUtf8(text));
}
// 居中由 Qt 根据字体计算，按钮命中矩形与显示矩形共用同一组坐标。
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
extern "C" void gfx_capture(const char *path) { window->frame.save(QString::fromUtf8(path)); }
extern "C" void gfx_error(const char *text) {
    QMessageBox::critical(window.get(), QString::fromUtf8("音符碰撞"), QString::fromUtf8(text));
}
