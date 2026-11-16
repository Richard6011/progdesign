#include "GameWidget.h"
#include "core/ScoreService.h"

#include <QDateTime>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QPainter>
#include <algorithm>
#include <map>

namespace nc {

GameWidget::GameWidget(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(520, 450);
}

int GameWidget::laneForKey(int key) {
    switch (key) {
    case Qt::Key_D:
        return 0;
    case Qt::Key_F:
        return 1;
    case Qt::Key_J:
        return 2;
    case Qt::Key_K:
        return 3;
    default:
        return -1;
    }
}

void GameWidget::keyPressEvent(QKeyEvent *event) {
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }
    const int lane = laneForKey(event->key());
    if (lane >= 0) {
        emit lanePressed(lane);
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        emit escapePressed();
        event->accept();
    } else
        QWidget::keyPressEvent(event);
}

void GameWidget::keyReleaseEvent(QKeyEvent *event) {
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }
    const int lane = laneForKey(event->key());
    if (lane >= 0) {
        emit laneReleased(lane);
        event->accept();
    } else
        QWidget::keyReleaseEvent(event);
}

void GameWidget::showJudgement(Judgement judgement) {
    const QStringList labels{"PERFECT", "GREAT", "GOOD", "MISS"};
    const QColor colors[]{QColor("#78f5d2"), QColor("#9eafff"), QColor("#ffce79"),
                          QColor("#ff7297")};
    judgement_ = labels[static_cast<int>(judgement)];
    judgementColor_ = colors[static_cast<int>(judgement)];
    feedbackUntil_ = QDateTime::currentMSecsSinceEpoch() + 450;
    update();
}

void GameWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#0b1020"));
    const qreal fieldWidth = std::min(width() - 80.0, 560.0);
    const qreal left = (width() - fieldWidth) / 2;
    const qreal laneWidth = fieldWidth / 4;
    const qreal judgeY = height() - 78;
    const qreal spawnY = 32;
    const QColor colors[]{QColor("#7c94ff"), QColor("#79e2db"), QColor("#f4ba79"),
                          QColor("#db92f2")};
    for (int lane = 0; lane < 4; ++lane) {
        const QRectF laneRect(left + lane * laneWidth, 0, laneWidth, height());
        painter.fillRect(laneRect, QColor(lane % 2 == 0 ? "#141d32" : "#10182b"));
        if (engine_ && engine_->laneDown()[lane]) {
            QColor glow = colors[lane];
            glow.setAlpha(45);
            painter.fillRect(laneRect, glow);
        }
        painter.setPen(QColor("#29344e"));
        painter.drawLine(QPointF(laneRect.left(), 0), QPointF(laneRect.left(), height()));
        painter.setPen(colors[lane]);
        QFont font = painter.font();
        font.setPixelSize(24);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRectF(laneRect.left(), judgeY + 20, laneWidth, 42), Qt::AlignCenter,
                         QStringList{"D", "F", "J", "K"}[lane]);
    }
    if (engine_ && !engine_->chart().notes.empty()) {
        const auto &chart = engine_->chart();
        const auto &runtime = engine_->runtime();
        const auto y = [&](qint64 time) {
            return spawnY +
                   (1.0 - double(time - musicTime_) / chart.approachMs) * (judgeY - spawnY);
        };
        std::map<int, QPointF> chainPoints;
        for (std::size_t i = 0; i < chart.notes.size(); ++i) {
            const auto &note = chart.notes[i];
            if (runtime[i].finalized || y(note.endTimeMs) > judgeY + 80 || y(note.hitTimeMs) < -30)
                continue;
            const qreal x = left + note.lane * laneWidth + 10;
            qreal headY = y(note.hitTimeMs);
            if (note.type == NoteType::Hold) {
                if (runtime[i].state == NoteState::Holding)
                    headY = judgeY;
                QColor body = colors[note.lane];
                body.setAlpha(85);
                painter.fillRect(QRectF(x + 8, y(note.endTimeMs), laneWidth - 36,
                                        std::max(0.0, headY - y(note.endTimeMs))),
                                 body);
            }
            if (note.type == NoteType::ChainStep) {
                QPointF point(x + (laneWidth - 20) / 2, headY);
                if (chainPoints.count(note.chainId)) {
                    painter.setPen(QPen(QColor("#f3e5a3"), 2));
                    painter.drawLine(chainPoints[note.chainId], point);
                }
                chainPoints[note.chainId] = point;
            }
            painter.setPen(note.chordId >= 0 ? QPen(Qt::white, 2) : QPen(Qt::NoPen));
            painter.setBrush(colors[note.lane]);
            painter.drawRoundedRect(QRectF(x, headY - 7, laneWidth - 20, 14), 5, 5);
            if (note.type == NoteType::ChainStep) {
                painter.setPen(QColor("#182034"));
                painter.drawText(QRectF(x, headY - 9, laneWidth - 20, 18), Qt::AlignCenter, "◆");
            }
        }
    }
    painter.setPen(QPen(QColor("#eff5ff"), 3));
    painter.drawLine(QPointF(left, judgeY), QPointF(left + fieldWidth, judgeY));
    if (QDateTime::currentMSecsSinceEpoch() < feedbackUntil_) {
        QFont font = painter.font();
        font.setPixelSize(25);
        painter.setFont(font);
        painter.setPen(judgementColor_);
        painter.drawText(QRectF(0, height() * .48, width(), 45), Qt::AlignCenter, judgement_);
    }
    if (!overlay_.isEmpty()) {
        painter.fillRect(rect(), QColor(5, 9, 20, 165));
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPixelSize(25);
        painter.setFont(font);
        painter.drawText(rect().adjusted(30, 20, -30, -20), Qt::AlignCenter | Qt::TextWordWrap,
                         overlay_);
    }
}

} // namespace nc
