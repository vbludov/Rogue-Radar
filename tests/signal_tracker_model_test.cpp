#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#include "../rogue-radar/signal_tracker_model.h"

using rogue_radar::SignalTrackerModel;
using rogue_radar::SignalTrend;

namespace {
int failures = 0;
int checks = 0;

void check(bool condition, const std::string &message)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void checkNear(float actual, float expected, float tolerance,
               const std::string &message)
{
    check(std::fabs(actual - expected) <= tolerance,
          message + " (actual=" + std::to_string(actual) +
              ", expected=" + std::to_string(expected) + ")");
}

void checkAbsent(const SignalTrackerModel::Bucket &bucket,
                 const std::string &message)
{
    check(!bucket.present() && bucket.raw == SignalTrackerModel::kAbsent &&
              bucket.smoothed == SignalTrackerModel::kAbsent,
          message);
}

void fillSteadyBaseline(SignalTrackerModel &model)
{
    model.addSample(-80, 0);
    for (uint32_t time = 500; time <= 2500; time += 500) {
        model.addSample(-80, time);
    }
}

void testThirtySecondWindow()
{
    SignalTrackerModel model(0);
    for (uint32_t i = 0; i < SignalTrackerModel::kBucketCount; ++i) {
        model.addSample(static_cast<int16_t>(-90 + i), i * 500U);
    }

    for (std::size_t i = 0; i < SignalTrackerModel::kBucketCount; ++i) {
        const SignalTrackerModel::Bucket bucket = model.bucket(i);
        check(bucket.present(), "all 60 half-second buckets are populated");
        check(bucket.raw == static_cast<int16_t>(-90 + i),
              "30-second history is chronological");
    }

    model.addSample(-30, 30000);
    check(model.bucket(0).raw == -89,
          "61st sample evicts exactly the oldest bucket");
    check(model.bucket(59).raw == -30,
          "new sample occupies newest bucket");
    checkAbsent(model.bucket(60), "out-of-range bucket is absent");
}

void testAbsentBucketsAndFreshCurrent()
{
    SignalTrackerModel model(0);
    model.addSample(-70, 0);
    model.advance(1500);

    check(model.bucket(56).raw == -70,
          "sample shifts three chronological positions after late advance");
    checkAbsent(model.bucket(57), "first missed interval is absent");
    checkAbsent(model.bucket(58), "second missed interval is absent");
    checkAbsent(model.bucket(59), "current missed interval is absent");

    int16_t raw = 0;
    float smoothed = 0.0f;
    check(model.current(1500, raw, smoothed),
          "recent reading remains current across absent buckets");
    check(raw == -70, "fresh current raw value is retained");
    checkNear(smoothed, -70.0f, 0.001f,
              "fresh current smoothed value is retained");
}

void testSmoothingAndTrendDeadband()
{
    SignalTrackerModel smooth(0);
    smooth.addSample(-80, 0);
    smooth.addSample(-60, 100);
    smooth.addSample(-40, 200);
    int16_t raw = 0;
    float filtered = 0.0f;
    check(smooth.current(200, raw, filtered), "smoothed sample is current");
    check(raw == -40, "current raw is latest sample");
    checkNear(filtered, -63.8f, 0.001f, "EWMA uses alpha 0.30");
    check(smooth.bucket(59).smoothed == -64,
          "negative smoothed RSSI rounds to nearest integer");

    SignalTrackerModel stronger(0);
    fillSteadyBaseline(stronger);
    stronger.addSample(-66, 3000);  // -80 -> -75.8, delta +4.2 dB.
    check(stronger.trend(3000) == SignalTrend::Stronger,
          "trend stronger above +3 dB deadband");

    SignalTrackerModel steady(0);
    fillSteadyBaseline(steady);
    steady.addSample(-70, 3000);  // -80 -> -77, exactly +3 dB.
    check(steady.trend(3000) == SignalTrend::Steady,
          "exact +3 dB remains steady");

    SignalTrackerModel weaker(0);
    fillSteadyBaseline(weaker);
    weaker.addSample(-94, 3000);  // -80 -> -84.2, delta -4.2 dB.
    check(weaker.trend(3000) == SignalTrend::Weaker,
          "trend weaker below -3 dB deadband");

    SignalTrackerModel tooYoung(0);
    tooYoung.addSample(-70, 0);
    check(tooYoung.trend(0) == SignalTrend::Unknown,
          "trend is unknown without a three-second reference");
}

void testStaleBoundaryAndReacquisition()
{
    SignalTrackerModel model(0);
    model.addSample(-80, 0);
    int16_t raw = 0;
    float smoothed = 0.0f;
    check(model.current(2999, raw, smoothed),
          "reading is fresh one millisecond before stale boundary");
    check(!model.current(3000, raw, smoothed),
          "reading is stale at exactly three seconds");
    check(model.lost(3000), "lost flips at exact three-second boundary");
    check(model.strengthPercent(3000) == 0,
          "stale reading reports zero strength");
    check(model.trend(3000) == SignalTrend::Unknown,
          "stale reading has unknown trend");

    model.addSample(-40, 3000);
    check(model.current(3000, raw, smoothed),
          "reacquired reading is immediately current");
    check(raw == -40, "reacquisition updates raw RSSI");
    checkNear(smoothed, -40.0f, 0.001f,
              "reacquisition resets rather than continues EWMA");
    check(model.trend(3000) == SignalTrend::Unknown,
          "reacquisition does not trend across a lost interval");

    model.addSample(-40, 5999);
    check(model.trend(5999) == SignalTrend::Unknown,
          "trend stays unknown through 2999 ms of reacquisition");
    model.addSample(-20, 6000);  // EWMA -34, +6 dB from reacquired -40.
    check(model.trend(6000) == SignalTrend::Stronger,
          "trend becomes available at exactly 3000 ms of reacquisition");
}

void testTimestampWraparound()
{
    const uint32_t start = std::numeric_limits<uint32_t>::max() - 249U;
    SignalTrackerModel model(start);
    model.addSample(-80, start);
    const uint32_t after750 = start + 750U;
    model.advance(after750);
    check(model.bucket(58).raw == -80,
          "bucket rotation survives uint32 timestamp wrap");
    checkAbsent(model.bucket(59),
                "wrapped advance creates the elapsed absent bucket");

    model.addSample(-60, after750);
    int16_t raw = 0;
    float smoothed = 0.0f;
    check(model.current(after750, raw, smoothed),
          "post-wrap sample is current");
    checkNear(smoothed, -74.0f, 0.001f,
              "EWMA continues across timestamp wrap");
    check(!model.lost(after750 + 2999U),
          "post-wrap sample remains fresh before threshold");
    check(model.lost(after750 + 3000U),
          "post-wrap stale threshold uses unsigned elapsed time");

    const uint32_t acquisitionStart =
        std::numeric_limits<uint32_t>::max() - 999U;
    SignalTrackerModel trendAcrossWrap(acquisitionStart);
    trendAcrossWrap.addSample(-80, acquisitionStart);
    trendAcrossWrap.addSample(-80, acquisitionStart + 2999U);
    check(trendAcrossWrap.trend(acquisitionStart + 2999U) ==
              SignalTrend::Unknown,
          "acquisition gate remains closed for 2999 ms across wrap");
    trendAcrossWrap.addSample(-60, acquisitionStart + 3000U);
    check(trendAcrossWrap.trend(acquisitionStart + 3000U) ==
              SignalTrend::Stronger,
          "acquisition gate opens at 3000 ms across wrap");
}

void testDelayedAdvanceAndStrengthClamp()
{
    SignalTrackerModel model(100);
    model.addSample(-75, 100);
    const uint32_t delayed = 100U + 65U * SignalTrackerModel::kBucketMs;
    model.advance(delayed);
    for (std::size_t i = 0; i < SignalTrackerModel::kBucketCount; ++i) {
        checkAbsent(model.bucket(i),
                    "advance beyond window clears every history bucket");
    }
    check(model.lost(delayed), "delayed model is stale");

    model.addSample(-50, delayed);
    check(model.bucket(59).raw == -50,
          "sample after delayed advance occupies newest bucket");
    int16_t raw = 0;
    float smoothed = 0.0f;
    check(model.current(delayed, raw, smoothed),
          "sample reacquires after delayed advance");
    checkNear(smoothed, -50.0f, 0.001f,
              "delayed reacquisition resets EWMA");

    SignalTrackerModel strength(0);
    strength.addSample(-120, 0);
    check(strength.strengthPercent(0) == 0,
          "strength clamps below -100 dBm to zero");
    strength.reset(0);
    strength.addSample(-10, 0);
    check(strength.strengthPercent(0) == 100,
          "strength clamps above -30 dBm to 100");
    strength.reset(0);
    strength.addSample(-65, 0);
    check(strength.strengthPercent(0) == 50,
          "strength maps midpoint to 50 percent");
}
}  // namespace

int main()
{
    testThirtySecondWindow();
    testAbsentBucketsAndFreshCurrent();
    testSmoothingAndTrendDeadband();
    testStaleBoundaryAndReacquisition();
    testTimestampWraparound();
    testDelayedAdvanceAndStrengthClamp();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: " << checks << " SignalTrackerModel checks\n";
    return EXIT_SUCCESS;
}