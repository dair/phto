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
    file_read.snapshot(),
  };
  snap.counters = {
    {"storage_bytes_written", storage_bytes_written.value()},
    {"storage_bytes_read", storage_bytes_read.value()},
    {"images_added", images_added.value()},
    {"images_failed", images_failed.value()},
    {"stage_read", stage_read.value()},
    {"stage_read_bytes", stage_read_bytes.value()},
    {"stage_validated", stage_validated.value()},
    {"stage_validated_bytes", stage_validated_bytes.value()},
    {"stage_hashed", stage_hashed.value()},
    {"stage_hashed_bytes", stage_hashed_bytes.value()},
    {"stage_dedup_checked", stage_dedup_checked.value()},
    {"stage_stored", stage_stored.value()},
    {"stage_db_inserted", stage_db_inserted.value()},
    {"stage_db_inserted_bytes", stage_db_inserted_bytes.value()},
  };
  snap.gauges = {
    {"pool_queue_depth", pool_queue_depth.value()},
    {"pool_active_threads", pool_active_threads.value()},
    {"blobs_alive", blobs_alive.value()},
    {"blob_bytes_alive", blob_bytes_alive.value()},
    {"inflight_reading", inflight_reading.value()},
    {"inflight_validating", inflight_validating.value()},
    {"inflight_hashing", inflight_hashing.value()},
    {"inflight_waiting_mutex", inflight_waiting_mutex.value()},
    {"inflight_dedup_checking", inflight_dedup_checking.value()},
    {"inflight_writing_storage", inflight_writing_storage.value()},
    {"inflight_inserting_db", inflight_inserting_db.value()},
    {"inflight_reading_bytes", inflight_reading_bytes.value()},
    {"inflight_validating_bytes", inflight_validating_bytes.value()},
    {"inflight_hashing_bytes", inflight_hashing_bytes.value()},
    {"inflight_waiting_mutex_bytes", inflight_waiting_mutex_bytes.value()},
    {"inflight_dedup_checking_bytes", inflight_dedup_checking_bytes.value()},
    {"inflight_writing_storage_bytes", inflight_writing_storage_bytes.value()},
    {"inflight_inserting_db_bytes", inflight_inserting_db_bytes.value()},
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

  stage_read.reset();
  stage_read_bytes.reset();
  stage_validated.reset();
  stage_validated_bytes.reset();
  stage_hashed.reset();
  stage_hashed_bytes.reset();
  stage_dedup_checked.reset();
  stage_stored.reset();
  stage_db_inserted.reset();
  stage_db_inserted_bytes.reset();

  pool_queue_depth.reset();
  pool_active_threads.reset();
  blobs_alive.reset();
  blob_bytes_alive.reset();

  inflight_reading.reset();
  inflight_validating.reset();
  inflight_hashing.reset();
  inflight_waiting_mutex.reset();
  inflight_dedup_checking.reset();
  inflight_writing_storage.reset();
  inflight_inserting_db.reset();
  inflight_reading_bytes.reset();
  inflight_validating_bytes.reset();
  inflight_hashing_bytes.reset();
  inflight_waiting_mutex_bytes.reset();
  inflight_dedup_checking_bytes.reset();
  inflight_writing_storage_bytes.reset();
  inflight_inserting_db_bytes.reset();

  file_read.reset();
}

} // namespace metrics
