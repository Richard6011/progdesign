#pragma once

#include "core/GameEngine.h"
#include <QWidget>

namespace nc {

class GameWidget : public QWidget {
    Q_OBJECT
  public:
    explicit GameWidget(QWidget *parent = nullptr);
    void setEngine(const GameEngine *engine) {
        engine_ = engine;
    }
    void setMusicTime(qint64 time) {
        musicTime_ = time;
        update();
    }
    void setOverlay(const QString &text) {
        overlay_ = text;
        update();
    }
    void showJudgement(Judgement judgement);

  signals:
    void lanePressed(int lane);
    void laneReleased(int lane);
    void escapePressed();

  protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

  private:
    static int laneForKey(int key);
    const GameEngine *engine_ = nullptr;
    qint64 musicTime_ = 0;
    QString overlay_;
    QString judgement_;
    QColor judgementColor_;
    qint64 feedbackUntil_ = 0;
};

} // namespace nc
