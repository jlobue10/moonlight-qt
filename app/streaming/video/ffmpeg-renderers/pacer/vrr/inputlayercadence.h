#pragma once

#include <algorithm>
#include <cstdint>

namespace Vrr13 {
// Detect substantial source-rate changes from adjacent RTP intervals, without
// waiting for the long cadence fit. Arrival spacing is deliberately not input.
class InputLayerCadence {
public:
    static bool sameLayer(uint64_t a, uint64_t b) {
        if (!a || !b) return false;
        const uint64_t low = std::min(a, b), high = std::max(a, b);
        return high - low <= low * 2 / 5; // symmetric 1.4x period range
    }
    uint64_t observe(uint64_t interval, uint64_t activePeriod, bool adjacent) {
        if (!adjacent || !interval || interval > 1000000 ||
                sameLayer(interval, activePeriod)) {
            reset();
            return 0;
        }
        const uint64_t mean = m_Count ? m_Total / m_Count : interval;
        const uint64_t difference = std::max(mean, interval) - std::min(mean, interval);
        if (difference > std::max<uint64_t>(500, mean / 10)) reset();
        m_Total += interval;
        if (++m_Count < 3) return 0;
        const uint64_t period = m_Total / m_Count;
        reset();
        return period;
    }
    // Policy 8: a layer boundary alone is not a rate-change observation.
    // Fast changes must depart from both the layer and the current fitted
    // cadence. Freeze that latter reference while qualifying the candidate.
    static bool sameOperatingRate(uint64_t a, uint64_t b, uint64_t minimumFpsGap = 0) {
        if (!a || !b) return false;
        // Round the observed source rate to Hz so RTP-to-us quantization
        // cannot turn an exact 60 -> 30 transition into 29.998 FPS.
        const uint64_t aHz = 1000000 / a + (1000000 % a >= (a + 1) / 2);
        const uint64_t bHz = 1000000 / b + (1000000 % b >= (b + 1) / 2);
        if (std::max(aHz, bHz) - std::min(aHz, bHz) < minimumFpsGap) return true;
        const uint64_t low = std::min(a, b), high = std::max(a, b);
        return high - low < (low + 1) / 2; // at least a 1.5x change to leave
    }
    uint64_t observeConfirmed(uint64_t interval, uint64_t layerPeriod,
                              uint64_t operatingPeriod, bool adjacent, uint64_t minimumFpsGap = 0) {
        if (!adjacent || !interval || interval > 1000000 ||
                sameOperatingRate(interval, layerPeriod, minimumFpsGap)) {
            m_Fast = {}; m_Settled = {};
            return 0;
        }
        if (sameOperatingRate(interval, m_Fast.count ? m_Fast.reference : operatingPeriod, minimumFpsGap))
            m_Fast = {};
        else {
            m_Fast.add(interval, operatingPeriod, 10);
            if (m_Fast.count >= 3 && !sameOperatingRate(m_Fast.mean(), m_Fast.reference, minimumFpsGap) &&
                    !sameOperatingRate(m_Fast.mean(), layerPeriod, minimumFpsGap)) {
                const auto result = m_Fast.mean();
                m_Fast = {}; m_Settled = {};
                return result;
            }
        }
        // A gradual rate change can eventually earn a new layer, but a
        // drifting fit or a few jittery tail intervals cannot. Require a
        // tightly clustered run and agreement with the settled source fit.
        m_Settled.add(interval, operatingPeriod, 20);
        const auto mean = m_Settled.mean();
        if (m_Settled.count >= 12 && m_Settled.total >= 200000 &&
                !sameOperatingRate(mean, layerPeriod, minimumFpsGap) &&
                std::max(mean, operatingPeriod) - std::min(mean, operatingPeriod) <= mean / 10) {
            m_Fast = {}; m_Settled = {};
            return mean;
        }
        return 0;
    }
    void reset() { m_Total = 0; m_Count = 0; m_Fast = {}; m_Settled = {}; }
private:
    struct Run {
        uint64_t total = 0, minimum = 0, maximum = 0, reference = 0;
        unsigned count = 0;
        uint64_t mean() const { return count ? total / count : 0; }
        void add(uint64_t interval, uint64_t operatingPeriod, uint64_t divisor) {
            if (count && (std::max(maximum, interval) - std::min(minimum, interval) >
                          std::max<uint64_t>(500, mean() / divisor) || count >= 256))
                *this = {};
            if (!count) { minimum = maximum = interval; reference = operatingPeriod; }
            minimum = std::min(minimum, interval); maximum = std::max(maximum, interval);
            total += interval; ++count;
        }
    };
    Run m_Fast, m_Settled;
    uint64_t m_Total = 0;
    unsigned m_Count = 0;
};
}
