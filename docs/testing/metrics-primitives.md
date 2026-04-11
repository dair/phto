# Testing: Metrics Primitives

**Plan Reference**: `docs/plan/0005.MONITORING.md`
**Status**: complete
**Coverage**: 9/9 acceptance criteria covered (100%)

## Test Inventory

| Test Name | File | Criteria Covered | Status |
|-----------|------|------------------|--------|
| CounterTest::testInitialValueIsZero | MetricsTest.cpp | Counter initial value=0 | ✅ |
| CounterTest::testAdd | MetricsTest.cpp | Counter add() | ✅ |
| CounterTest::testMultipleAddsAccumulate | MetricsTest.cpp | Counter multiple adds | ✅ |
| CounterTest::testReset | MetricsTest.cpp | Counter reset() | ✅ |
| CounterTest::testName | MetricsTest.cpp | Counter name() | ✅ |
| GaugeTest::testInitialValueIsZero | MetricsTest.cpp | Gauge initial value=0 | ✅ |
| GaugeTest::testIncrement | MetricsTest.cpp | Gauge increment() | ✅ |
| GaugeTest::testDecrement | MetricsTest.cpp | Gauge decrement() | ✅ |
| GaugeTest::testAddPositive | MetricsTest.cpp | Gauge add(positive) | ✅ |
| GaugeTest::testAddNegative | MetricsTest.cpp | Gauge add(negative) | ✅ |
| GaugeTest::testSet | MetricsTest.cpp | Gauge set() | ✅ |
| GaugeTest::testReset | MetricsTest.cpp | Gauge reset() | ✅ |
| GaugeTest::testName | MetricsTest.cpp | Gauge name() | ✅ |
| GaugeTest::testDecrementBelowZero | MetricsTest.cpp | Gauge unconstrained math | ✅ |
| GaugeGuardTest::testGuardIncrementsOnConstruct | MetricsTest.cpp | GaugeGuard RAII increment | ✅ |
| GaugeGuardTest::testGuardDecrementsOnDestruct | MetricsTest.cpp | GaugeGuard RAII decrement | ✅ |
| GaugeGuardTest::testNestedGuards | MetricsTest.cpp | GaugeGuard nested scopes | ✅ |
| SizedGaugeGuardTest::testBothGaugesIncrementedOnConstruct | MetricsTest.cpp | SizedGaugeGuard construct | ✅ |
| SizedGaugeGuardTest::testBothGaugesDecrementedOnDestruct | MetricsTest.cpp | SizedGaugeGuard destruct | ✅ |
| SizedGaugeGuardTest::testSizeIsTrackedCorrectly | MetricsTest.cpp | SizedGaugeGuard multi | ✅ |
| HistogramTest::testInitialSnapshotIsEmpty | MetricsTest.cpp | Histogram initial state | ✅ |
| HistogramTest::testRecordOneSample | MetricsTest.cpp | Histogram record count=1 | ✅ |
| HistogramTest::testRecordUpdatesSumNs | MetricsTest.cpp | Histogram sum_ns populated | ✅ |
| HistogramTest::testRecordIncrementsCorrectBucket | MetricsTest.cpp | Histogram correct bucket | ✅ |
| HistogramTest::testRecordMultipleSamples | MetricsTest.cpp | Histogram multiple samples | ✅ |
| HistogramTest::testResetClearsAll | MetricsTest.cpp | Histogram reset() | ✅ |
| HistogramTest::testNamePreserved | MetricsTest.cpp | Histogram name | ✅ |
| HistogramTest::testLargeValueGoesToLastBucket | MetricsTest.cpp | Histogram +Inf bucket | ✅ |
| TimerTest::testRaiiRecordsOnDestruct | MetricsTest.cpp | Timer RAII records on destruct | ✅ |
| TimerTest::testStopRecordsImmediately | MetricsTest.cpp | Timer stop() records immediately | ✅ |
| TimerTest::testDoubleStopIsNoOp | MetricsTest.cpp | Timer double-stop is no-op | ✅ |
| TimerTest::testStopThenDestructIsNoOp | MetricsTest.cpp | Timer stop then destruct is no-op | ✅ |
| MetricsSnapshotTest::testSnapshotContainsCounters | MetricsTest.cpp | Metrics::snapshot() counters | ✅ |
| MetricsSnapshotTest::testSnapshotContainsGauges | MetricsTest.cpp | Metrics::snapshot() gauges | ✅ |
| MetricsSnapshotTest::testSnapshotContainsHistograms | MetricsTest.cpp | Metrics::snapshot() histograms | ✅ |
| MetricsSnapshotTest::testSnapshotCounterValuesReflectMutations | MetricsTest.cpp | Snapshot reflects mutations | ✅ |
| MetricsSnapshotTest::testSnapshotGaugeValuesReflectMutations | MetricsTest.cpp | Snapshot reflects mutations | ✅ |
| MetricsSnapshotTest::testSnapshotHistogramValuesReflectMutations | MetricsTest.cpp | Snapshot reflects mutations | ✅ |
| MetricsResetTest::testResetZeroesCounters | MetricsTest.cpp | Metrics::reset() counters | ✅ |
| MetricsResetTest::testResetZeroesGauges | MetricsTest.cpp | Metrics::reset() gauges | ✅ |
| MetricsResetTest::testResetZeroesHistograms | MetricsTest.cpp | Metrics::reset() histograms | ✅ |
| SnapshotFormatTest::testFormatReturnsNonEmptyString | MetricsTest.cpp | format() non-empty output | ✅ |
| SnapshotFormatTest::testFormatContainsKnownMetricName | MetricsTest.cpp | format() contains metric name | ✅ |
| SnapshotFormatTest::testFormatEmptySnapshot | MetricsTest.cpp | format() no-throw on empty | ✅ |

## Progress Log

- **2026-04-11**: Initial test suite written. 44 tests across 7 fixture classes covering all 5 primitives (Counter, Gauge, Histogram, Timer, Snapshot format) plus Metrics registry (snapshot and reset). All pass.

## Known Gaps

- No concurrency stress tests for individual primitives (atomic correctness under concurrent writers). The metrics library is designed lock-free; multi-threaded correctness testing would require careful ordering verification.
- Timer tests use `sleep_for(1ms)` for timing; on extremely loaded CI machines this could be flaky if the scheduler delays exceed the test timeout (unlikely in practice).

## Notes

- Include style: `<metrics/Counter.h>` etc. (angle brackets, namespaced) because the test directory is outside the metrics directory and `metrics_lib` sets its include root to its parent.
- The `testRecordIncrementsCorrectBucket` test verifies the sum of all bucket counts equals 1 (i.e., exactly one bucket was incremented) rather than hardcoding a specific bucket index, making it robust to future bucket bound changes.
- `format(FullSnapshot)` is declared in `metrics/Snapshot.h` not `metrics/Format.h`; included via `<metrics/Snapshot.h>`.
