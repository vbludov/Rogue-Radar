#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>

namespace rogue_radar {

enum class SignalTrend : uint8_t {
    Unknown = 0,
    Steady,
    Stronger,
    Weaker,
};

class SignalTrackerModel {
public:
    enum : uint32_t {
        kBucketMs = 500,
        kLostAfterMs = 3000,
    };
    enum : std::size_t {
        kBucketCount = 60,
    };
    enum : int16_t {
        kAbsent = INT16_MIN,
        kRangeMinDbm = -100,
        kRangeMaxDbm = -30,
        kTrendDeadbandDb = 3,
    };

    struct Bucket {
        int16_t raw;
        int16_t smoothed;

        bool present() const
        {
            return raw != kAbsent && smoothed != kAbsent;
        }
    };

    explicit SignalTrackerModel(uint32_t nowMs = 0)
    {
        reset(nowMs);
    }

    void reset(uint32_t nowMs = 0)
    {
        for (std::size_t i = 0; i < kBucketCount; ++i) {
            rawHistory_[i] = kAbsent;
            smoothedHistory_[i] = kAbsent;
        }
        newestBucket_ = 0;
        bucketStartMs_ = nowMs;
        lastSampleMs_ = nowMs;
        acquisitionStartMs_ = nowMs;
        currentRaw_ = kAbsent;
        currentSmoothed_ = 0.0f;
        hasSample_ = false;
    }

    // Advance the time window without adding a reading. Each elapsed 500 ms
    // interval becomes an explicit absent bucket, including when a loop is late.
    void advance(uint32_t nowMs)
    {
        const uint32_t elapsed = nowMs - bucketStartMs_;
        const uint32_t steps = elapsed / kBucketMs;
        if (steps == 0) {
            return;
        }

        bucketStartMs_ += steps * kBucketMs;
        if (steps >= kBucketCount) {
            for (std::size_t i = 0; i < kBucketCount; ++i) {
                rawHistory_[i] = kAbsent;
                smoothedHistory_[i] = kAbsent;
            }
            newestBucket_ =
                (newestBucket_ + (steps % kBucketCount)) % kBucketCount;
            return;
        }

        for (uint32_t i = 0; i < steps; ++i) {
            newestBucket_ = (newestBucket_ + 1U) % kBucketCount;
            rawHistory_[newestBucket_] = kAbsent;
            smoothedHistory_[newestBucket_] = kAbsent;
        }
    }

    void addSample(int16_t rssi, uint32_t nowMs)
    {
        const bool reacquiring = !hasSample_ || lost(nowMs);
        advance(nowMs);

        currentRaw_ = rssi;
        currentSmoothed_ = reacquiring
            ? static_cast<float>(rssi)
            : (ewmaAlpha() * static_cast<float>(rssi) +
               (1.0f - ewmaAlpha()) * currentSmoothed_);
        if (reacquiring) {
            acquisitionStartMs_ = nowMs;
        }
        lastSampleMs_ = nowMs;
        hasSample_ = true;

        rawHistory_[newestBucket_] = currentRaw_;
        smoothedHistory_[newestBucket_] = roundedRssi(currentSmoothed_);
    }

    // Freshness is independent of bucket rotation: a recent sample remains the
    // current reading after advance() moves into a later bucket.
    bool current(uint32_t nowMs, int16_t &raw, float &smoothed) const
    {
        if (lost(nowMs)) {
            return false;
        }
        raw = currentRaw_;
        smoothed = currentSmoothed_;
        return true;
    }

    bool lost(uint32_t nowMs) const
    {
        return !hasSample_ || (nowMs - lastSampleMs_) >= kLostAfterMs;
    }

    SignalTrend trend(uint32_t nowMs) const
    {
        if (lost(nowMs)) {
            return SignalTrend::Unknown;
        }
        if ((nowMs - acquisitionStartMs_) < kLostAfterMs) {
            return SignalTrend::Unknown;
        }

        constexpr std::size_t kTrendBuckets = 3000U / kBucketMs;
        const std::size_t priorIndex =
            (newestBucket_ + kBucketCount - kTrendBuckets) % kBucketCount;
        const int16_t prior = smoothedHistory_[priorIndex];
        if (prior == kAbsent) {
            return SignalTrend::Unknown;
        }

        const float delta = currentSmoothed_ - static_cast<float>(prior);
        if (delta > kTrendDeadbandDb) {
            return SignalTrend::Stronger;
        }
        if (delta < -kTrendDeadbandDb) {
            return SignalTrend::Weaker;
        }
        return SignalTrend::Steady;
    }

    uint8_t strengthPercent(uint32_t nowMs) const
    {
        if (lost(nowMs)) {
            return 0;
        }
        if (currentSmoothed_ <= kRangeMinDbm) {
            return 0;
        }
        if (currentSmoothed_ >= kRangeMaxDbm) {
            return 100;
        }

        const float scaled =
            (currentSmoothed_ - kRangeMinDbm) * 100.0f /
            (kRangeMaxDbm - kRangeMinDbm);
        return static_cast<uint8_t>(scaled + 0.5f);
    }

    // Returns a chronological bucket: index 0 is oldest, 59 is current.
    // Out-of-range indexes return an absent bucket.
    Bucket bucket(std::size_t indexOldestFirst) const
    {
        if (indexOldestFirst >= kBucketCount) {
            return absentBucket();
        }
        const std::size_t index =
            (newestBucket_ + 1U + indexOldestFirst) % kBucketCount;
        return Bucket{rawHistory_[index], smoothedHistory_[index]};
    }

private:
    int16_t rawHistory_[kBucketCount];
    int16_t smoothedHistory_[kBucketCount];
    std::size_t newestBucket_ = 0;
    uint32_t bucketStartMs_ = 0;
    uint32_t lastSampleMs_ = 0;
    uint32_t acquisitionStartMs_ = 0;
    int16_t currentRaw_ = kAbsent;
    float currentSmoothed_ = 0.0f;
    bool hasSample_ = false;

    static constexpr float ewmaAlpha() { return 0.30f; }

    static int16_t roundedRssi(float value)
    {
        return static_cast<int16_t>(value >= 0.0f ? value + 0.5f
                                                  : value - 0.5f);
    }

    static Bucket absentBucket()
    {
        return Bucket{kAbsent, kAbsent};
    }
};

}  // namespace rogue_radar
