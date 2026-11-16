#include "ClockSynchronizer.h"

#include <cmath>

namespace nc {

bool ClockSynchronizer::sample(qint64 t1, qint64 t2, qint64 t3, qint64 t4) {
    if (t1 < 0 || t2 < 0 || t3 < t2 || t4 < t1)
        return false;
    const qint64 rtt = (t4 - t1) - (t3 - t2);
    if (rtt < 0 || rtt > 5000)
        return false;
    const double offset = ((static_cast<double>(t2) - t1) + (static_cast<double>(t3) - t4)) / 2.0;
    if (m_bestRtt < 0 || rtt < m_bestRtt) {
        m_bestRtt = rtt;
        m_offset = offset;
    }
    ++m_samples;
    return true;
}

void ClockSynchronizer::reset() {
    m_samples = 0;
    m_bestRtt = -1;
    m_offset = 0;
}

qint64 ClockSynchronizer::guestTarget(qint64 hostTargetMs) const {
    return static_cast<qint64>(std::llround(static_cast<double>(hostTargetMs) - m_offset));
}

} // namespace nc
