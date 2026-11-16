#pragma once

#include <QtGlobal>

namespace nc {

// 保存五次往返测量，采用 RTT 最低的有效样本，将客人时间映射到房主时间。
class ClockSynchronizer {
  public:
    static constexpr int RequiredSamples = 5;
    bool sample(qint64 guestSent, qint64 hostReceived, qint64 hostSent, qint64 guestReceived);
    void reset();
    bool isReady() const {
        return m_samples >= RequiredSamples;
    }
    int sampleCount() const {
        return m_samples;
    }
    double offsetMs() const {
        return m_offset;
    }
    qint64 bestRttMs() const {
        return m_bestRtt;
    }
    qint64 guestTarget(qint64 hostTargetMs) const;

  private:
    int m_samples = 0;     // 当前连接采集到的有效样本数。
    qint64 m_bestRtt = -1; // 负值表示尚未测得往返时间。
    double m_offset = 0.0; // hostTime - guestTime，允许半毫秒。
};

} // namespace nc
