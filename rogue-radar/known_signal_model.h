#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "known_device_types.h"

namespace rogue_radar {

struct KnownSignalIdentity {
    KnownRadio radio = KnownRadio::Ble;
    char address[18] = {};
    uint8_t addressType = 255;
};

inline KnownSignalIdentity knownSignalIdentity(KnownRadio radio,
                                               const KnownAddress &address) {
    KnownSignalIdentity identity;
    identity.radio = radio;
    strncpy(identity.address, address.address, sizeof(identity.address) - 1);
    identity.addressType = radio == KnownRadio::Ble ? address.addressType : 255;
    return identity;
}

inline bool sameKnownSignalIdentity(const KnownSignalIdentity &left,
                                    const KnownSignalIdentity &right) {
    return left.radio == right.radio && left.addressType == right.addressType &&
           strncmp(left.address, right.address, sizeof(left.address)) == 0;
}

// BLE advertisements commonly omit fields that were present in an earlier
// packet. Retain the latest nonempty descriptive metadata only while the exact
// radio/address/address-type identity is unchanged. Signal and time fields
// always come from the newest observation.
inline KnownAddress mergeKnownSignalObservation(KnownRadio radio,
                                                 const KnownAddress &stored,
                                                 const KnownAddress &latest) {
    KnownAddress merged = latest;
    if (!sameKnownSignalIdentity(knownSignalIdentity(radio, stored),
                                 knownSignalIdentity(radio, latest)))
        return merged;
    if (!merged.advertisedName[0] && stored.advertisedName[0])
        strncpy(merged.advertisedName, stored.advertisedName,
                sizeof(merged.advertisedName) - 1);
    bool preservedMetadata = false;
    if (!merged.manufacturerData[0] && stored.manufacturerData[0]) {
        merged.manufacturerId = stored.manufacturerId;
        strncpy(merged.manufacturerData, stored.manufacturerData,
                sizeof(merged.manufacturerData) - 1);
        preservedMetadata = true;
    }
    if (!merged.serviceUuids[0] && stored.serviceUuids[0]) {
        strncpy(merged.serviceUuids, stored.serviceUuids,
                sizeof(merged.serviceUuids) - 1);
        preservedMetadata = true;
    }
    if (preservedMetadata && stored.metadataTruncated)
        merged.metadataTruncated = true;
    return merged;
}

enum class KnownSignalTrend : uint8_t { Unknown, Rising, Steady, Falling };
enum class KnownSignalObserveResult : uint8_t { Updated, Added, Ignored, Full };

struct KnownSignalCandidate {
    static const int16_t MissingRssi = -128;
    KnownSignalIdentity identity{};
    KnownAddress observation{};
    int16_t smoothedRssi = -127;
    int16_t history[12] = {};
    uint8_t historyCount = 0;
    KnownSignalTrend trend = KnownSignalTrend::Unknown;
    uint32_t lastSeenMs = 0;
    bool stale = false;
};

class KnownSignalTable {
 public:
    static constexpr size_t Capacity = 30;
    static constexpr size_t HistoryBuckets = 12;
    static constexpr uint32_t BucketMs = 500;

    void reset(KnownRadio radio) {
        radio_ = radio;
        count_ = 0;
        frozen_ = false;
        lastRankMs_ = 0;
        hasRanked_ = false;
        for (size_t i = 0; i < Capacity; ++i) order_[i] = static_cast<uint8_t>(i);
    }

    KnownRadio radio() const { return radio_; }
    size_t count() const { return count_; }
    bool orderFrozen() const { return frozen_; }
    void setOrderFrozen(bool frozen) { frozen_ = frozen; }

    KnownSignalObserveResult observe(KnownRadio radio,
                                     const KnownAddress &observation,
                                     uint32_t nowMs) {
        if (radio != radio_ || observation.address[0] == '\0')
            return KnownSignalObserveResult::Ignored;
        const KnownSignalIdentity identity = knownSignalIdentity(radio, observation);
        int index = findIndex(identity);
        KnownSignalObserveResult result = KnownSignalObserveResult::Updated;
        if (index < 0) {
            if (count_ >= Capacity) {
                if (frozen_) return KnownSignalObserveResult::Full;
                index = replacementIndex(observation.lastRssi, nowMs);
                if (index < 0) return KnownSignalObserveResult::Full;
            } else {
                index = static_cast<int>(count_++);
                order_[count_ - 1] = static_cast<uint8_t>(index);
            }
            candidates_[index] = KnownSignalCandidate{};
            buckets_[index] = BucketState{};
            candidates_[index].identity = identity;
            for (size_t i = 0; i < HistoryBuckets; ++i)
                candidates_[index].history[i] = KnownSignalCandidate::MissingRssi;
            result = KnownSignalObserveResult::Added;
        }
        updateCandidate(index, observation, nowMs);
        return result;
    }

    const KnownSignalCandidate *candidateAtRank(size_t rank) const {
        if (rank >= count_) return nullptr;
        return &candidates_[order_[rank]];
    }

    const KnownSignalCandidate *find(const KnownSignalIdentity &identity) const {
        const int index = findIndex(identity);
        return index < 0 ? nullptr : &candidates_[index];
    }

    bool refreshRanking(uint32_t nowMs, uint32_t intervalMs = 1000) {
        if (frozen_ || (hasRanked_ && nowMs - lastRankMs_ < intervalMs))
            return false;
        lastRankMs_ = nowMs;
        hasRanked_ = true;
        for (size_t i = 1; i < count_; ++i) {
            const uint8_t value = order_[i];
            size_t position = i;
            while (position > 0 && ranksBefore(value, order_[position - 1])) {
                order_[position] = order_[position - 1];
                --position;
            }
            order_[position] = value;
        }
        return true;
    }

    size_t expireStale(uint32_t nowMs, uint32_t staleMs = 3000) {
        size_t staleCount = 0;
        for (size_t i = 0; i < count_; ++i) {
            candidates_[i].stale = nowMs - candidates_[i].lastSeenMs >= staleMs;
            if (candidates_[i].stale) ++staleCount;
        }
        if (frozen_ || staleCount == 0) return 0;
        size_t removed = 0;
        for (size_t i = 0; i < count_;) {
            if (!candidates_[i].stale) { ++i; continue; }
            for (size_t j = i + 1; j < count_; ++j) {
                candidates_[j - 1] = candidates_[j];
                buckets_[j - 1] = buckets_[j];
            }
            --count_;
            ++removed;
        }
        rebuildDefaultOrder();
        hasRanked_ = false;
        return removed;
    }

 private:
    struct BucketState {
        uint32_t startedMs = 0;
        int32_t sum = 0;
        uint16_t samples = 0;
        bool initialized = false;
    } buckets_[Capacity]{};

    int findIndex(const KnownSignalIdentity &identity) const {
        for (size_t i = 0; i < count_; ++i)
            if (sameKnownSignalIdentity(candidates_[i].identity, identity))
                return static_cast<int>(i);
        return -1;
    }

    int replacementIndex(int16_t incomingRssi, uint32_t nowMs) const {
        int stale = -1;
        for (size_t i = 0; i < count_; ++i) {
            if (!candidates_[i].stale) continue;
            if (stale < 0 || nowMs - candidates_[i].lastSeenMs >
                                 nowMs - candidates_[stale].lastSeenMs)
                stale = static_cast<int>(i);
        }
        if (stale >= 0) return stale;
        if (count_ == 0) return -1;
        size_t weakest = 0;
        for (size_t i = 1; i < count_; ++i) {
            if (candidates_[i].smoothedRssi < candidates_[weakest].smoothedRssi ||
                (candidates_[i].smoothedRssi == candidates_[weakest].smoothedRssi &&
                 nowMs - candidates_[i].lastSeenMs >
                     nowMs - candidates_[weakest].lastSeenMs))
                weakest = i;
        }
        return incomingRssi > candidates_[weakest].smoothedRssi
            ? static_cast<int>(weakest) : -1;
    }

    void rebuildDefaultOrder() {
        for (size_t i = 0; i < count_; ++i) order_[i] = static_cast<uint8_t>(i);
    }

    bool ranksBefore(uint8_t left, uint8_t right) const {
        const KnownSignalCandidate &a = candidates_[left];
        const KnownSignalCandidate &b = candidates_[right];
        if (a.stale != b.stale) return !a.stale;
        if (a.smoothedRssi != b.smoothedRssi) return a.smoothedRssi > b.smoothedRssi;
        return strncmp(a.identity.address, b.identity.address,
                       sizeof(a.identity.address)) < 0;
    }

    void updateCandidate(int index, const KnownAddress &observation, uint32_t nowMs) {
        KnownSignalCandidate &candidate = candidates_[index];
        BucketState &bucket = buckets_[index];
        candidate.observation = mergeKnownSignalObservation(
            candidate.identity.radio, candidate.observation, observation);
        candidate.observation.lastSeenUptimeMs = nowMs;
        candidate.lastSeenMs = nowMs;
        candidate.stale = false;
        const int16_t rssi = observation.lastRssi;
        if (!bucket.initialized) {
            bucket.initialized = true;
            bucket.startedMs = nowMs;
            candidate.historyCount = 1;
        } else {
            const uint32_t steps = (nowMs - bucket.startedMs) / BucketMs;
            if (steps > 0) {
                const size_t shift = steps >= HistoryBuckets ? HistoryBuckets : steps;
                for (size_t i = HistoryBuckets; i-- > shift;)
                    candidate.history[i] = candidate.history[i - shift];
                for (size_t i = 0; i < shift; ++i)
                    candidate.history[i] = KnownSignalCandidate::MissingRssi;
                candidate.historyCount = static_cast<uint8_t>(
                    candidate.historyCount + shift > HistoryBuckets
                        ? HistoryBuckets : candidate.historyCount + shift);
                bucket.startedMs += steps * BucketMs;
                bucket.sum = 0;
                bucket.samples = 0;
            }
        }
        bucket.sum += rssi;
        ++bucket.samples;
        candidate.history[0] = static_cast<int16_t>(bucket.sum / bucket.samples);
        candidate.smoothedRssi = candidate.smoothedRssi == -127
            ? rssi : static_cast<int16_t>((candidate.smoothedRssi * 3 + rssi * 2) / 5);
        candidate.trend = trendFor(candidate);
    }

    static KnownSignalTrend trendFor(const KnownSignalCandidate &candidate) {
        int first = -1, second = -1;
        for (size_t i = 0; i < candidate.historyCount; ++i) {
            if (candidate.history[i] == KnownSignalCandidate::MissingRssi) continue;
            if (first < 0) first = static_cast<int>(i);
            else { second = static_cast<int>(i); break; }
        }
        if (second < 0) return KnownSignalTrend::Unknown;
        const int delta = candidate.history[first] - candidate.history[second];
        return delta > 3 ? KnownSignalTrend::Rising :
               delta < -3 ? KnownSignalTrend::Falling : KnownSignalTrend::Steady;
    }

    KnownRadio radio_ = KnownRadio::Ble;
    KnownSignalCandidate candidates_[Capacity]{};
    uint8_t order_[Capacity]{};
    size_t count_ = 0;
    bool frozen_ = false;
    uint32_t lastRankMs_ = 0;
    bool hasRanked_ = false;
};

enum class LearningPhase : uint8_t { NearFirst, Away, NearSecond, Complete };
enum class LearningOutcome : uint8_t {
    InProgress,
    InsufficientEvidence,
    WeakResponse,
    InconsistentResponse,
    AmbiguousResponse,
    ConsistentResponse
};

// Outcomes describe only how the observed RSSI changed during the guided
// near/away/near exercise. Even ConsistentResponse is not an identity or
// distance confirmation; saving/relinking remains an explicit user action.

struct LearningPhaseEvidence {
    uint16_t samples = 0;
    uint32_t durationMs = 0;
    int16_t meanRssi = -127;
};

struct LearningMetrics {
    LearningOutcome outcome = LearningOutcome::InProgress;
    LearningPhaseEvidence selected[3]{};
    int16_t firstDropDb = 0;
    int16_t secondDropDb = 0;
    int16_t nearDifferenceDb = 0;
    uint8_t responsiveNeighbors = 0;
    uint8_t ambiguousNeighbors = 0;
    uint8_t missingPhaseMask = 0;
};

struct PossibleKnownMatch {
    KnownSignalIdentity identity{};
    uint8_t similarityScore = 0;
};

class KnownLearningSession {
 public:
    static constexpr size_t Capacity = 30;
    static constexpr size_t SuggestionCapacity = 8;
    static constexpr uint32_t MinimumPhaseMs = 10000;

    void begin(KnownRadio radio, const KnownAddress &selected, uint32_t nowMs) {
        count_ = 0;
        phase_ = LearningPhase::NearFirst;
        phaseStartedMs_ = nowMs;
        memset(phaseDurationMs_, 0, sizeof(phaseDurationMs_));
        selected_ = knownSignalIdentity(radio, selected);
        const int index = addRecord(radio, selected);
        if (index >= 0) records_[index].selected = true;
    }

    LearningPhase phase() const { return phase_; }
    const KnownSignalIdentity &selectedIdentity() const { return selected_; }
    size_t candidateCount() const { return count_; }
    static uint16_t minimumSamples(KnownRadio radio) {
        return radio == KnownRadio::Ble ? 8 : 3;
    }

    LearningPhaseEvidence selectedPhaseEvidence(uint32_t nowMs) const {
        LearningPhaseEvidence evidence;
        const int index = findRecord(selected_);
        if (phase_ == LearningPhase::Complete || index < 0) return evidence;
        const uint8_t phase = static_cast<uint8_t>(phase_);
        evidence.samples = records_[index].phases[phase].samples;
        evidence.durationMs = nowMs - phaseStartedMs_;
        evidence.meanRssi = mean(records_[index].phases[phase]);
        return evidence;
    }

    bool currentPhaseReady(uint32_t nowMs) const {
        if (phase_ == LearningPhase::Complete) return false;
        const LearningPhaseEvidence evidence = selectedPhaseEvidence(nowMs);
        return evidence.durationMs >= MinimumPhaseMs &&
               evidence.samples >= minimumSamples(selected_.radio);
    }

    bool candidateEvidence(const KnownSignalIdentity &identity,
                           LearningPhaseEvidence output[3],
                           uint32_t nowMs,
                           bool *excluded = nullptr) const {
        if (!output) return false;
        const int index = findRecord(identity);
        if (index < 0) return false;
        for (uint8_t phase = 0; phase < 3; ++phase) {
            output[phase].samples = records_[index].phases[phase].samples;
            output[phase].durationMs = phaseDurationMs_[phase];
            if (phase_ != LearningPhase::Complete &&
                phase == static_cast<uint8_t>(phase_))
                output[phase].durationMs = nowMs - phaseStartedMs_;
            output[phase].meanRssi = mean(records_[index].phases[phase]);
        }
        if (excluded) *excluded = records_[index].excluded;
        return true;
    }

    bool advance(uint32_t nowMs) {
        if (phase_ == LearningPhase::Complete) return false;
        const uint8_t index = static_cast<uint8_t>(phase_);
        phaseDurationMs_[index] = nowMs - phaseStartedMs_;
        phase_ = static_cast<LearningPhase>(index + 1);
        phaseStartedMs_ = nowMs;
        return true;
    }

    bool setPhase(LearningPhase phase, uint32_t nowMs) {
        if (phase == phase_) return true;
        if (phase_ == LearningPhase::Complete ||
            static_cast<uint8_t>(phase) != static_cast<uint8_t>(phase_) + 1)
            return false;
        return advance(nowMs);
    }

    bool restartCurrentPhase(uint32_t nowMs) {
        if (phase_ == LearningPhase::Complete) return false;
        const uint8_t phaseIndex = static_cast<uint8_t>(phase_);
        for (size_t i = 0; i < count_; ++i)
            records_[i].phases[phaseIndex] = PhaseAccumulator{};
        phaseDurationMs_[phaseIndex] = 0;
        phaseStartedMs_ = nowMs;
        return true;
    }

    bool observe(KnownRadio radio, const KnownAddress &observation, uint32_t nowMs) {
        if (phase_ == LearningPhase::Complete || observation.address[0] == '\0' ||
            observation.lastRssi <= -127) return false;
        int index = findRecord(knownSignalIdentity(radio, observation));
        if (index < 0) index = addRecord(radio, observation);
        if (index < 0) return false;
        Record &record = records_[index];
        record.observation = mergeKnownSignalObservation(
            record.identity.radio, record.observation, observation);
        const uint8_t phaseIndex = static_cast<uint8_t>(phase_);
        PhaseAccumulator &accumulator = record.phases[phaseIndex];
        const uint32_t stamp = observation.lastSeenUptimeMs;
        if (accumulator.hasSample) {
            if (stamp != 0 && stamp == accumulator.lastObservationStamp) return false;
            if (stamp == 0 && nowMs - accumulator.lastAcceptedMs < 500) return false;
        }
        accumulator.hasSample = true;
        accumulator.lastObservationStamp = stamp;
        accumulator.lastAcceptedMs = nowMs;
        accumulator.sum += observation.lastRssi;
        ++accumulator.samples;
        return true;
    }

    bool exclude(const KnownSignalIdentity &identity, bool excluded = true) {
        int index = findRecord(identity);
        if (index < 0) {
            KnownAddress address{};
            strncpy(address.address, identity.address, sizeof(address.address) - 1);
            address.addressType = identity.addressType;
            index = addRecord(identity.radio, address);
        }
        if (index < 0 || records_[index].selected) return false;
        records_[index].excluded = excluded;
        return true;
    }

    LearningMetrics evaluate(uint32_t nowMs) const {
        LearningMetrics metrics;
        if (phase_ != LearningPhase::Complete) {
            metrics.outcome = LearningOutcome::InProgress;
            fillSelectedMetrics(metrics, nowMs);
            return metrics;
        }
        fillSelectedMetrics(metrics, nowMs);
        const int selectedIndex = findRecord(selected_);
        if (selectedIndex < 0) {
            metrics.outcome = LearningOutcome::InsufficientEvidence;
            metrics.missingPhaseMask = 0x07;
            return metrics;
        }
        const Record &selected = records_[selectedIndex];
        for (uint8_t phase = 0; phase < 3; ++phase) {
            if (!phaseSufficient(selected, phase)) metrics.missingPhaseMask |= 1U << phase;
        }
        if (metrics.missingPhaseMask) {
            metrics.outcome = LearningOutcome::InsufficientEvidence;
            return metrics;
        }
        metrics.firstDropDb = metrics.selected[0].meanRssi - metrics.selected[1].meanRssi;
        metrics.secondDropDb = metrics.selected[2].meanRssi - metrics.selected[1].meanRssi;
        metrics.nearDifferenceDb = absolute(metrics.selected[0].meanRssi -
                                             metrics.selected[2].meanRssi);
        if (metrics.firstDropDb < 6 || metrics.secondDropDb < 6) {
            metrics.outcome = LearningOutcome::WeakResponse;
            return metrics;
        }
        if (metrics.nearDifferenceDb > 8) {
            metrics.outcome = LearningOutcome::InconsistentResponse;
            return metrics;
        }
        const int16_t selectedResponse = metrics.firstDropDb < metrics.secondDropDb
            ? metrics.firstDropDb : metrics.secondDropDb;
        for (size_t i = 0; i < count_; ++i) {
            const Record &record = records_[i];
            if (record.selected || record.excluded || !allPhasesSufficient(record)) continue;
            const int16_t first = mean(record.phases[0]) - mean(record.phases[1]);
            const int16_t second = mean(record.phases[2]) - mean(record.phases[1]);
            const int16_t nearDifference = absolute(mean(record.phases[0]) -
                                                     mean(record.phases[2]));
            if (first < 6 || second < 6 || nearDifference > 8) continue;
            ++metrics.responsiveNeighbors;
            const int16_t response = first < second ? first : second;
            if (response + 3 >= selectedResponse) ++metrics.ambiguousNeighbors;
        }
        metrics.outcome = metrics.ambiguousNeighbors
            ? LearningOutcome::AmbiguousResponse : LearningOutcome::ConsistentResponse;
        return metrics;
    }

    size_t possibleMatches(PossibleKnownMatch *output, size_t capacity) const {
        if (!output || capacity == 0) return 0;
        const int selectedIndex = findRecord(selected_);
        if (selectedIndex < 0) return 0;
        const KnownAddress &selected = records_[selectedIndex].observation;
        size_t outputCount = 0;
        for (size_t i = 0; i < count_; ++i) {
            const Record &record = records_[i];
            if (record.selected || record.excluded) continue;
            const uint8_t score = similarity(selected, record.observation);
            if (score == 0) continue;
            PossibleKnownMatch match;
            match.identity = record.identity;
            match.similarityScore = score;
            size_t position = outputCount < capacity ? outputCount++ : capacity;
            if (position == capacity && score <= output[capacity - 1].similarityScore) continue;
            if (position == capacity) position = capacity - 1;
            while (position > 0 && score > output[position - 1].similarityScore) {
                if (position < capacity) output[position] = output[position - 1];
                --position;
            }
            output[position] = match;
        }
        return outputCount;
    }

 private:
    struct PhaseAccumulator {
        int32_t sum = 0;
        uint16_t samples = 0;
        uint32_t lastAcceptedMs = 0;
        uint32_t lastObservationStamp = 0;
        bool hasSample = false;
    };
    struct Record {
        KnownSignalIdentity identity{};
        KnownAddress observation{};
        PhaseAccumulator phases[3]{};
        bool selected = false;
        bool excluded = false;
    } records_[Capacity]{};

    int addRecord(KnownRadio radio, const KnownAddress &observation) {
        if (count_ >= Capacity) return -1;
        const size_t index = count_++;
        records_[index] = Record{};
        records_[index].identity = knownSignalIdentity(radio, observation);
        records_[index].observation = observation;
        return static_cast<int>(index);
    }
    int findRecord(const KnownSignalIdentity &identity) const {
        for (size_t i = 0; i < count_; ++i)
            if (sameKnownSignalIdentity(records_[i].identity, identity))
                return static_cast<int>(i);
        return -1;
    }
    static int16_t mean(const PhaseAccumulator &phase) {
        return phase.samples ? static_cast<int16_t>(phase.sum / phase.samples) : -127;
    }
    static int16_t absolute(int16_t value) { return value < 0 ? -value : value; }
    uint16_t requiredSamples(const Record &record) const {
        return minimumSamples(record.identity.radio);
    }
    bool phaseSufficient(const Record &record, uint8_t phase) const {
        return phaseDurationMs_[phase] >= MinimumPhaseMs &&
               record.phases[phase].samples >= requiredSamples(record);
    }
    bool allPhasesSufficient(const Record &record) const {
        return phaseSufficient(record, 0) && phaseSufficient(record, 1) &&
               phaseSufficient(record, 2);
    }
    void fillSelectedMetrics(LearningMetrics &metrics, uint32_t nowMs) const {
        const int selectedIndex = findRecord(selected_);
        for (uint8_t phase = 0; phase < 3; ++phase) {
            metrics.selected[phase].durationMs = phaseDurationMs_[phase];
            if (phase_ != LearningPhase::Complete &&
                phase == static_cast<uint8_t>(phase_))
                metrics.selected[phase].durationMs = nowMs - phaseStartedMs_;
            if (selectedIndex >= 0) {
                metrics.selected[phase].samples = records_[selectedIndex].phases[phase].samples;
                metrics.selected[phase].meanRssi = mean(records_[selectedIndex].phases[phase]);
            }
        }
    }
    static uint8_t similarity(const KnownAddress &left, const KnownAddress &right) {
        uint8_t score = 0;
        if (left.advertisedName[0] && right.advertisedName[0] &&
            strcmp(left.advertisedName, right.advertisedName) == 0) score += 2;
        if (left.manufacturerId && left.manufacturerId == right.manufacturerId) score += 1;
        if (left.manufacturerData[0] && right.manufacturerData[0] &&
            strcmp(left.manufacturerData, right.manufacturerData) == 0) score += 3;
        if (left.serviceUuids[0] && right.serviceUuids[0] &&
            strcmp(left.serviceUuids, right.serviceUuids) == 0) score += 2;
        return score;
    }

    KnownSignalIdentity selected_{};
    size_t count_ = 0;
    LearningPhase phase_ = LearningPhase::Complete;
    uint32_t phaseStartedMs_ = 0;
    uint32_t phaseDurationMs_[3]{};
};

}  // namespace rogue_radar
