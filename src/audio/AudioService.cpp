#include "AudioService.h"

#include <QUrl>
#include <algorithm>

namespace nc {

AudioService::AudioService(QObject *parent) : QObject(parent) {
    player_.setAudioOutput(&output_);
    connect(
        &player_, &QMediaPlayer::mediaStatusChanged, this,
        [this](QMediaPlayer::MediaStatus status) {
            if ((status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) &&
                !ready_) {
                ready_ = true;
                emit loaded();
            } else if (status == QMediaPlayer::EndOfMedia && active_) {
                active_ = false;
                emit ended();
            } else if (status == QMediaPlayer::StalledMedia && active_) {
                emit bufferingInterrupted();
            }
        });
    connect(&player_, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (state == QMediaPlayer::PlayingState && active_)
                    emit started(player_.position());
            });
    connect(&player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &message) {
                if (error != QMediaPlayer::NoError)
                    emit failed(message);
            });
}

void AudioService::load(const QString &path) {
    stop();
    ready_ = false;
    // 同一首歌曲重开也必须触发新的预加载完成通知。
    player_.setSource(QUrl());
    player_.setSource(QUrl::fromLocalFile(path));
    player_.setPosition(0);
}

void AudioService::play() {
    active_ = true;
    player_.play();
}
void AudioService::pause() {
    active_ = false;
    player_.pause();
}
void AudioService::stop() {
    active_ = false;
    player_.stop();
    player_.setPosition(0);
}
void AudioService::setVolume(int volume) {
    output_.setVolume(std::clamp(volume, 0, 100) / 100.0f);
}

} // namespace nc
