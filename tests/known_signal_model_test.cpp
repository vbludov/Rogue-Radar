#include <cstdlib>
#include <cstring>
#include <iostream>

#include "../rogue-radar/known_signal_model.h"

using namespace rogue_radar;

namespace {
int checks;
int failures;

void check(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

KnownAddress address(const char *mac, int rssi, uint8_t type = 255,
                     const char *name = "", uint32_t stamp = 0) {
    KnownAddress value;
    std::strncpy(value.address, mac, sizeof(value.address) - 1);
    std::strncpy(value.advertisedName, name, sizeof(value.advertisedName) - 1);
    value.addressType = type;
    value.lastRssi = static_cast<int8_t>(rssi);
    value.lastSeenUptimeMs = stamp;
    return value;
}

void testIdentityHistoryRankingAndExpiry() {
    KnownSignalTable table;
    table.reset(KnownRadio::Ble);
    KnownAddress publicAddress = address("AA:BB:CC:DD:EE:01", -70, 0, "Shared");
    KnownAddress randomAddress = address("AA:BB:CC:DD:EE:01", -50, 1, "Shared");
    check(table.observe(KnownRadio::Ble, publicAddress, 100) ==
              KnownSignalObserveResult::Added,
          "first BLE identity added");
    check(table.observe(KnownRadio::Ble, randomAddress, 100) ==
              KnownSignalObserveResult::Added && table.count() == 2,
          "BLE address type remains part of identity");
    check(table.observe(KnownRadio::Wifi, publicAddress, 100) ==
              KnownSignalObserveResult::Ignored,
          "table filters the inactive radio view");

    table.refreshRanking(1000, 0);
    check(table.candidateAtRank(0)->identity.addressType == 1,
          "stronger smoothed signal ranks first");
    check(!table.refreshRanking(1100, 1000),
          "ranking refresh respects its periodic interval");

    KnownSignalIdentity selected = table.candidateAtRank(0)->identity;
    table.setOrderFrozen(true);
    for (uint32_t now = 600; now <= 2600; now += 500) {
        publicAddress.lastRssi = -20;
        table.observe(KnownRadio::Ble, publicAddress, now);
    }
    check(!table.refreshRanking(3000, 0), "ranking does not move while frozen");
    check(sameKnownSignalIdentity(table.candidateAtRank(0)->identity, selected),
          "frozen navigation preserves selected identity");
    check(table.expireStale(7000, 3000) == 0 && table.count() == 2,
          "stale candidates remain addressable while navigation is frozen");
    check(table.find(selected) != nullptr && table.find(selected)->stale,
          "selected stale identity is marked without removal");
    table.setOrderFrozen(false);
    check(table.expireStale(7000, 3000) == 2 && table.count() == 0,
          "unfrozen expiry removes stale identities");

    table.reset(KnownRadio::Wifi);
    KnownAddress wifiA = address("02:00:00:00:00:01", -70, 0);
    KnownAddress wifiB = address("02:00:00:00:00:02", -60, 1);
    table.observe(KnownRadio::Wifi, wifiA, 0);
    table.observe(KnownRadio::Wifi, wifiB, 0);
    check(table.count() == 2 && table.candidateAtRank(0)->identity.addressType == 255,
          "WiFi identity normalizes irrelevant BLE address type");

    table.reset(KnownRadio::Ble);
    KnownAddress moving = address("02:00:00:00:00:03", -80, 1);
    table.observe(KnownRadio::Ble, moving, 0);
    moving.lastRssi = -65; table.observe(KnownRadio::Ble, moving, 500);
    moving.lastRssi = -50; table.observe(KnownRadio::Ble, moving, 1000);
    const KnownSignalCandidate *candidate = table.candidateAtRank(0);
    check(candidate->historyCount == 3, "history spans fixed 500 ms buckets");
    check(candidate->history[0] == -50 && candidate->history[1] == -65,
          "history exposes newest smoothed bucket first");
    check(candidate->trend == KnownSignalTrend::Rising,
          "recent bucket deadband reports rising signal");
}

void fillPhase(KnownLearningSession &session, KnownRadio radio,
               KnownAddress target, KnownAddress neighbor, uint32_t start,
               int targetRssi, int neighborRssi, unsigned targetSamples = 8,
               unsigned neighborSamples = 8) {
    for (unsigned i = 0; i < targetSamples; ++i) {
        target.lastRssi = static_cast<int8_t>(targetRssi + static_cast<int>(i % 2));
        target.lastSeenUptimeMs = start + i + 1;
        session.observe(radio, target, start + i * 900);
    }
    for (unsigned i = 0; i < neighborSamples; ++i) {
        neighbor.lastRssi = static_cast<int8_t>(neighborRssi + static_cast<int>(i % 2));
        neighbor.lastSeenUptimeMs = start + 100 + i;
        session.observe(radio, neighbor, start + i * 900);
    }
    session.advance(start + KnownLearningSession::MinimumPhaseMs);
}

void testGuidedEvidenceAndFalsePositives() {
    KnownAddress target = address("10:00:00:00:00:01", -45, 1, "Same Name");
    KnownAddress neighbor = address("10:00:00:00:00:02", -35, 1, "Same Name");
    target.manufacturerId = neighbor.manufacturerId = 0x004c;
    std::strcpy(target.serviceUuids, "180D");
    std::strcpy(neighbor.serviceUuids, "180D");

    KnownLearningSession session;
    session.begin(KnownRadio::Ble, target, 0);
    check(!session.currentPhaseReady(9999),
          "guided phase cannot finish before ten seconds");
    fillPhase(session, KnownRadio::Ble, target, neighbor, 0, -45, -35);
    fillPhase(session, KnownRadio::Ble, target, neighbor, 10000, -72, -35);
    fillPhase(session, KnownRadio::Ble, target, neighbor, 20000, -46, -35);
    LearningMetrics metrics = session.evaluate(30000);
    check(metrics.outcome == LearningOutcome::ConsistentResponse,
          "drop and recovery distinguish target from stationary strong neighbor");
    check(metrics.firstDropDb >= 20 && metrics.secondDropDb >= 20,
          "UI metrics expose both target response changes");
    check(metrics.responsiveNeighbors == 0 && metrics.ambiguousNeighbors == 0,
          "absolute neighbor strength does not create a false match");

    PossibleKnownMatch matches[8]{};
    check(session.possibleMatches(matches, 8) == 1 &&
              std::strcmp(matches[0].identity.address, neighbor.address) == 0,
          "identical name/metadata is only a possible-match suggestion");
    check(session.selectedIdentity().addressType == 1,
          "selected identity remains explicit after consistent evidence");
    LearningPhaseEvidence evidence[3]{};
    bool excluded = true;
    check(session.candidateEvidence(knownSignalIdentity(KnownRadio::Ble, neighbor),
                                    evidence, 30000, &excluded) && !excluded &&
              evidence[0].samples == 8,
          "per-candidate phase evidence is available to the UI");
}

void testAmbiguityExclusionAndWeakEvidence() {
    KnownAddress target = address("20:00:00:00:00:01", -45, 1, "Tag");
    KnownAddress movingNeighbor = address("20:00:00:00:00:02", -48, 1, "Tag");
    KnownLearningSession session;
    session.begin(KnownRadio::Ble, target, 0);
    fillPhase(session, KnownRadio::Ble, target, movingNeighbor, 0, -45, -48);
    fillPhase(session, KnownRadio::Ble, target, movingNeighbor, 10000, -70, -72);
    fillPhase(session, KnownRadio::Ble, target, movingNeighbor, 20000, -46, -49);
    LearningMetrics metrics = session.evaluate(30000);
    check(metrics.outcome == LearningOutcome::AmbiguousResponse &&
              metrics.ambiguousNeighbors == 1,
          "neighbor with matching response keeps learning ambiguous");
    check(session.exclude(knownSignalIdentity(KnownRadio::Ble, movingNeighbor)),
          "user can exclude a neighboring identity for this session");
    check(session.evaluate(30000).outcome == LearningOutcome::ConsistentResponse,
          "explicit exclusion removes neighbor from response ambiguity");

    KnownLearningSession staticTarget;
    staticTarget.begin(KnownRadio::Ble, target, 0);
    fillPhase(staticTarget, KnownRadio::Ble, target, movingNeighbor, 0, -45, -35);
    fillPhase(staticTarget, KnownRadio::Ble, target, movingNeighbor, 10000, -47, -35);
    fillPhase(staticTarget, KnownRadio::Ble, target, movingNeighbor, 20000, -46, -35);
    check(staticTarget.evaluate(30000).outcome == LearningOutcome::WeakResponse,
          "near-only/static strength never becomes identity certainty");
}

void testInsufficientMissingAndRotatingAddresses() {
    KnownAddress target = address("30:00:00:00:00:01", -45, 1, "Rotator");
    KnownAddress neighbor = address("30:00:00:00:00:02", -40, 1, "Neighbor");
    KnownLearningSession fewSamples;
    fewSamples.begin(KnownRadio::Ble, target, 0);
    fillPhase(fewSamples, KnownRadio::Ble, target, neighbor, 0, -45, -40, 7, 8);
    fillPhase(fewSamples, KnownRadio::Ble, target, neighbor, 10000, -70, -40);
    fillPhase(fewSamples, KnownRadio::Ble, target, neighbor, 20000, -45, -40);
    LearningMetrics few = fewSamples.evaluate(30000);
    check(few.outcome == LearningOutcome::InsufficientEvidence &&
              (few.missingPhaseMask & 0x01),
          "BLE phase requires at least eight distinct observations");

    KnownLearningSession missing;
    missing.begin(KnownRadio::Ble, target, 0);
    fillPhase(missing, KnownRadio::Ble, target, neighbor, 0, -45, -40);
    KnownAddress rotated = target;
    std::strcpy(rotated.address, "30:00:00:00:00:99");
    fillPhase(missing, KnownRadio::Ble, rotated, neighbor, 10000, -70, -40);
    fillPhase(missing, KnownRadio::Ble, rotated, neighbor, 20000, -45, -40);
    LearningMetrics changed = missing.evaluate(30000);
    check(changed.outcome == LearningOutcome::InsufficientEvidence &&
              (changed.missingPhaseMask & 0x06) == 0x06,
          "changing BLE address is not automatically merged with selected target");
    PossibleKnownMatch suggestions[8]{};
    check(missing.possibleMatches(suggestions, 8) == 1 &&
              std::strcmp(suggestions[0].identity.address, rotated.address) == 0,
          "rotated similar metadata remains a suggestion requiring confirmation");

    KnownLearningSession incomplete;
    incomplete.begin(KnownRadio::Ble, target, 0);
    KnownAddress missingRssi = target;
    missingRssi.lastRssi = -127;
    check(!incomplete.observe(KnownRadio::Ble, missingRssi, 0),
          "missing RSSI is not counted as learning evidence");
    for (unsigned i = 0; i < 8; ++i) {
        target.lastSeenUptimeMs = i + 1;
        incomplete.observe(KnownRadio::Ble, target, i * 1000);
    }
    check(incomplete.evaluate(10000).outcome == LearningOutcome::InProgress,
          "near capture alone cannot produce a final outcome");
}

void testWifiThresholdAndDuplicateSamples() {
    KnownAddress target = address("40:00:00:00:00:01", -45, 255, "AP");
    KnownLearningSession session;
    session.begin(KnownRadio::Wifi, target, 0);
    for (int phase = 0; phase < 3; ++phase) {
        const uint32_t start = phase * 10000U;
        const int rssi = phase == 1 ? -70 : -45;
        for (unsigned i = 0; i < 3; ++i) {
            target.lastRssi = static_cast<int8_t>(rssi);
            target.lastSeenUptimeMs = start + i + 1;
            check(session.observe(KnownRadio::Wifi, target, start + i * 2000),
                  "fresh WiFi sweep accepted");
            check(!session.observe(KnownRadio::Wifi, target, start + i * 2000 + 1),
                  "cached observation timestamp is not double-counted");
        }
        session.advance(start + 10000);
    }
    check(session.evaluate(30000).outcome == LearningOutcome::ConsistentResponse,
          "three distinct WiFi sweeps per ten-second phase are sufficient");
}

void testIntermittentMetadataPreservation() {
    KnownAddress rich = address("45:00:00:00:00:01", -48, 1, "Band 7", 1);
    rich.manufacturerId = 0x1234;
    std::strcpy(rich.manufacturerData, "1234AABB");
    std::strcpy(rich.serviceUuids, "180D,180F");
    rich.metadataTruncated = true;

    KnownSignalTable table;
    table.reset(KnownRadio::Ble);
    table.observe(KnownRadio::Ble, rich, 100);
    KnownAddress sparse = address(rich.address, -55, 1, "", 2);
    table.observe(KnownRadio::Ble, sparse, 200);
    const KnownSignalCandidate *candidate = table.find(
        knownSignalIdentity(KnownRadio::Ble, rich));
    check(candidate && std::strcmp(candidate->observation.advertisedName, "Band 7") == 0 &&
              candidate->observation.manufacturerId == 0x1234 &&
              std::strcmp(candidate->observation.manufacturerData, "1234AABB") == 0 &&
              std::strcmp(candidate->observation.serviceUuids, "180D,180F") == 0 &&
              candidate->observation.metadataTruncated &&
              candidate->observation.lastRssi == -55,
          "same exact identity retains nonempty metadata while signal fields update");

    KnownAddress otherType = address(rich.address, -60, 0, "", 3);
    table.observe(KnownRadio::Ble, otherType, 300);
    const KnownSignalCandidate *other = table.find(
        knownSignalIdentity(KnownRadio::Ble, otherType));
    check(other && !other->observation.advertisedName[0] &&
              !other->observation.manufacturerData[0] &&
              !other->observation.serviceUuids[0],
          "metadata never crosses BLE address-type identity boundaries");

    KnownAddress refreshed = address(rich.address, -44, 1, "Band New", 4);
    refreshed.manufacturerId = 0x5678;
    std::strcpy(refreshed.manufacturerData, "5678CCDD");
    std::strcpy(refreshed.serviceUuids, "FE95");
    table.observe(KnownRadio::Ble, refreshed, 400);
    candidate = table.find(knownSignalIdentity(KnownRadio::Ble, rich));
    check(candidate && std::strcmp(candidate->observation.advertisedName, "Band New") == 0 &&
              candidate->observation.manufacturerId == 0x5678 &&
              std::strcmp(candidate->observation.manufacturerData, "5678CCDD") == 0 &&
              std::strcmp(candidate->observation.serviceUuids, "FE95") == 0 &&
              !candidate->observation.metadataTruncated,
          "new nonempty metadata replaces retained values and truncation state");

    KnownAddress neighbor = rich;
    std::strcpy(neighbor.address, "45:00:00:00:00:02");
    neighbor.lastSeenUptimeMs = 1;
    KnownLearningSession learning;
    learning.begin(KnownRadio::Ble, rich, 0);
    learning.observe(KnownRadio::Ble, neighbor, 100);
    KnownAddress sparseSelected = sparse;
    sparseSelected.lastSeenUptimeMs = 5;
    KnownAddress sparseNeighbor = address(neighbor.address, -57, 1, "", 6);
    learning.observe(KnownRadio::Ble, sparseSelected, 200);
    learning.observe(KnownRadio::Ble, sparseNeighbor, 300);
    PossibleKnownMatch matches[KnownLearningSession::SuggestionCapacity]{};
    check(learning.possibleMatches(matches,
                                   KnownLearningSession::SuggestionCapacity) == 1 &&
              matches[0].similarityScore == 8,
          "learning records retain intermittent metadata used for suggestions");
}

void testRestartCurrentLearningPhase() {
    KnownAddress target = address("46:00:00:00:00:01", -45, 1, "Target", 1);
    KnownAddress neighbor = address("46:00:00:00:00:02", -50, 1, "Neighbor", 2);
    KnownLearningSession session;
    session.begin(KnownRadio::Ble, target, 100);
    for (uint8_t i = 0; i < 8; ++i) {
        target.lastSeenUptimeMs = 10 + i;
        neighbor.lastSeenUptimeMs = 30 + i;
        session.observe(KnownRadio::Ble, target, 100 + i * 800);
        session.observe(KnownRadio::Ble, neighbor, 100 + i * 800);
    }
    check(session.exclude(knownSignalIdentity(KnownRadio::Ble, neighbor)),
          "neighbor exclusion is set before phase restart");
    check(session.advance(10100), "first phase completes before restart test");

    target.lastRssi = -70;
    target.lastSeenUptimeMs = 90;
    session.observe(KnownRadio::Ble, target, 10200);
    LearningPhaseEvidence before[3]{};
    bool excluded = false;
    check(session.candidateEvidence(knownSignalIdentity(KnownRadio::Ble, target),
                                    before, 10300) &&
              before[0].samples == 8 && before[1].samples == 1,
          "current and completed phase evidence exists before restart");

    check(session.restartCurrentPhase(20000),
          "active learning phase can be restarted explicitly");
    LearningPhaseEvidence after[3]{};
    check(session.candidateEvidence(knownSignalIdentity(KnownRadio::Ble, target),
                                    after, 20000) &&
              after[0].samples == 8 && after[0].durationMs == 10000 &&
              after[1].samples == 0 && after[1].durationMs == 0,
          "restart clears only current phase evidence and resets its clock");
    LearningPhaseEvidence neighborEvidence[3]{};
    check(session.candidateEvidence(knownSignalIdentity(KnownRadio::Ble, neighbor),
                                    neighborEvidence, 20000, &excluded) && excluded &&
              neighborEvidence[0].samples == 8 && neighborEvidence[1].samples == 0,
          "restart preserves identities, exclusions, and completed evidence");
    check(session.observe(KnownRadio::Ble, target, 20001),
          "restart clears timestamp dedupe state for the current phase");

    session.advance(30000);
    session.advance(40000);
    check(session.phase() == LearningPhase::Complete &&
              !session.restartCurrentPhase(50000),
          "completed learning session cannot restart a nonexistent phase");
}

void testCapacityBound() {
    KnownSignalTable table;
    table.reset(KnownRadio::Ble);
    char mac[18];
    for (size_t i = 0; i < KnownSignalTable::Capacity; ++i) {
        std::snprintf(mac, sizeof(mac), "50:00:00:00:00:%02X", (unsigned)i);
        check(table.observe(KnownRadio::Ble, address(mac, -60, 1),
                            static_cast<uint32_t>(i)) == KnownSignalObserveResult::Added,
              "bounded candidate slot added");
    }
    table.setOrderFrozen(true);
    check(table.observe(KnownRadio::Ble, address("50:00:00:00:00:FE", -20, 1), 100) ==
              KnownSignalObserveResult::Full && table.count() == 30,
          "frozen full table drops new identity without changing selection");

    KnownSignalTable rankedAdmission;
    rankedAdmission.reset(KnownRadio::Wifi);
    for (size_t i = 0; i < KnownSignalTable::Capacity; ++i) {
        std::snprintf(mac, sizeof(mac), "52:00:00:00:00:%02X", (unsigned)i);
        rankedAdmission.observe(KnownRadio::Wifi,
                                address(mac, -30 - static_cast<int>(i), 255), 100);
    }
    check(rankedAdmission.observe(KnownRadio::Wifi,
                                  address("52:00:00:00:00:FE", -70, 255), 100) ==
              KnownSignalObserveResult::Full &&
              rankedAdmission.find(knownSignalIdentity(
                  KnownRadio::Wifi, address("52:00:00:00:00:00", -30, 255))) != nullptr,
          "weak WiFi tail cannot evict a stronger candidate from a full table");
    KnownAddress newStrong = address("52:00:00:00:00:FD", -20, 255);
    check(rankedAdmission.observe(KnownRadio::Wifi, newStrong, 101) ==
              KnownSignalObserveResult::Added &&
              rankedAdmission.find(knownSignalIdentity(KnownRadio::Wifi, newStrong)) != nullptr &&
              rankedAdmission.find(knownSignalIdentity(
                  KnownRadio::Wifi, address("52:00:00:00:00:1D", -59, 255))) == nullptr,
          "strong new WiFi candidate replaces the weakest retained identity");

    KnownAddress selected = address("51:00:00:00:00:00", -40, 1, "Shared");
    KnownLearningSession learning;
    learning.begin(KnownRadio::Ble, selected, 0);
    for (unsigned i = 1; i < KnownLearningSession::Capacity; ++i) {
        std::snprintf(mac, sizeof(mac), "51:00:00:00:00:%02X", i);
        KnownAddress candidate = address(mac, -60, 1, "Shared", i);
        check(learning.observe(KnownRadio::Ble, candidate, i * 500),
              "bounded learning candidate added");
    }
    check(!learning.observe(KnownRadio::Ble,
                            address("51:00:00:00:00:FE", -20, 1, "Shared", 99),
                            20000) && learning.candidateCount() == 30,
          "learning table rejects candidate beyond fixed capacity");
    PossibleKnownMatch suggestions[KnownLearningSession::SuggestionCapacity]{};
    check(learning.possibleMatches(suggestions,
                                   KnownLearningSession::SuggestionCapacity) == 8,
          "possible-match list remains session bounded");
}
}  // namespace

int main() {
    testIdentityHistoryRankingAndExpiry();
    testGuidedEvidenceAndFalsePositives();
    testAmbiguityExclusionAndWeakEvidence();
    testInsufficientMissingAndRotatingAddresses();
    testWifiThresholdAndDuplicateSamples();
    testIntermittentMetadataPreservation();
    testRestartCurrentLearningPhase();
    testCapacityBound();
    check(sizeof(KnownSignalTable) < 14000, "nearby table remains bounded for external PSRAM allocation");
    check(sizeof(KnownLearningSession) < 16000, "learning session remains bounded for external PSRAM allocation");
    std::cout << "KnownSignalTable bytes=" << sizeof(KnownSignalTable)
              << " KnownLearningSession bytes=" << sizeof(KnownLearningSession) << '\n';
    if (failures) {
        std::cerr << failures << " of " << checks << " known-signal checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: " << checks << " known-signal model checks\n";
    return EXIT_SUCCESS;
}
