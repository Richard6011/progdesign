#pragma once

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>

namespace nc {

class AudioService : public QObject {
    Q_OBJECT
  public:
    explicit AudioService(QObject *parent = nullptr);
    void load(const QString &path);
    void play();
    void pause();
    void stop();
    void setVolume(int volume);
    bool ready() const {
        return ready_;
    }
    qint64 positionMs() const {
        return player_.position();
    }
    qint64 durationMs() const {
        return player_.duration();
    }

  signals:
    void loaded();
    void started(qint64 positionMs);
    void ended();
    void failed(const QString &reason);
    void bufferingInterrupted();

  private:
    QMediaPlayer player_;
    QAudioOutput output_;
    bool ready_ = false;
    bool active_ = false;
};

} // namespace nc
