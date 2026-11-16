#pragma once

#include <QElapsedTimer>

namespace nc {

// 音频实际进入播放态后建立锚点；QTimer 只刷新画面，不累计音乐时间。
class SongClock {
  public:
    void start(qint64 musicPositionMs = 0) {
        anchorMs_ = musicPositionMs;
        timer_.start();
        running_ = true;
    }
    qint64 nowMs() const {
        return anchorMs_ + (running_ ? timer_.elapsed() : 0);
    }
    void pause() {
        anchorMs_ = nowMs();
        running_ = false;
    }
    void reset() {
        anchorMs_ = 0;
        running_ = false;
    }
    bool running() const {
        return running_;
    }

  private:
    QElapsedTimer timer_;
    qint64 anchorMs_ = 0;
    bool running_ = false;
};

} // namespace nc
