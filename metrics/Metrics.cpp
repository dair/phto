#include <metrics/Metrics.h>

namespace metrics {

FullSnapshot Metrics::snapshot() const {
  FullSnapshot snap;
  snap.histograms = {
    addimage_total.snapshot(),
    validate.snapshot(),
    hash.snapshot(),
    dedup_check.snapshot(),
    storage_write.snapshot(),
    storage_write_root.snapshot(),
    db_insert.snapshot(),
    db_insert_single.snapshot(),
    mutex_wait.snapshot(),
    pool_schedule_latency.snapshot(),
    storage_read_duration.snapshot(),
    db_read_duration.snapshot(),
    db_write_duration.snapshot(),
  };
  snap.counters = {
    {"storage_bytes_written", storage_bytes_written.value()},
    {"storage_bytes_read", storage_bytes_read.value()},
    {"images_added", images_added.value()},
    {"images_failed", images_failed.value()},
  };
  snap.gauges = {
    {"pool_queue_depth", pool_queue_depth.value()},
    {"pool_active_threads", pool_active_threads.value()},
    {"blobs_alive", blobs_alive.value()},
    {"blob_bytes_alive", blob_bytes_alive.value()},
  };
  return snap;
}

void Metrics::reset() {
  addimage_total.reset();
  validate.reset();
  hash.reset();
  dedup_check.reset();
  storage_write.reset();
  storage_write_root.reset();
  db_insert.reset();
  db_insert_single.reset();
  mutex_wait.reset();
  pool_schedule_latency.reset();
  storage_read_duration.reset();
  db_read_duration.reset();
  db_write_duration.reset();

  storage_bytes_written.reset();
  storage_bytes_read.reset();
  images_added.reset();
  images_failed.reset();

  pool_queue_depth.reset();
  pool_active_threads.reset();
  blobs_alive.reset();
  blob_bytes_alive.reset();
}

} // namespace metrics
