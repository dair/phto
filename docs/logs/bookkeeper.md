
## [2026-10-31 18:00] - Plan status audit
- **Agent**: bookkeeper
- **Task**: Fix stale status lines in docs/plan and ANALYSIS.md after plan 0022.
- **Outcome**: Added/updated status lines in plans 0001-0004, 0007-0011, 0013, 0014, 0016-0022; added checkpoint-done note to 0022 section 16; added plan status table to plan/README.md; added dated Phase 2 note to analysis/ANALYSIS.md. 0023 untouched. 0005, 0006, 0012, 0015 left as-is (not verified).
- **Next Step**: Verify 0005/0006/0015 status; settings hygiene (0021 section 3.6) still open.

## [2026-10-31 20:00] - Loose ends (L1-L4) documentation and status bookkeeping
- **Agent**: bookkeeper
- **Task**: Document bounded uploads, trusted proxies, access log and configurable timings; finish plan status bookkeeping.
- **Outcome**: Status lines for 0005, 0006, 0015; 0012 re-checked (still in progress: -Werror and Database.h split open); 0021 settings hygiene marked done; plan/README.md status table and Next work list; API.md and DEPLOYMENT.md updated (413 behaviour, new config keys, trusted_proxies, access log, memory sizing); README.md and CLAUDE.md (Crow patch, test list); ANALYSIS.md Phase 2 section. 1 MiB drain and 100-continue handling confirmed against third_party/crow/crow-body-sink.patch.
- **Next Step**: Replace the placeholder test count (36) in CLAUDE.md and ANALYSIS.md with the final `ctest -N` number once le-test reports.

## [2026-10-31 22:00] - Plan 0012 tidy-up documentation
- **Agent**: bookkeeper (td-docs)
- **Task**: Document the completed plan-0012 work from td-dev's summary; correct build-directory claims.
- **Outcome**: 0012 status set to Implemented with a per-item table; plan/README.md status row updated and the 0012 "Next work" entry removed; CLAUDE.md (`-Werror`/`IMAGER_WERROR`, database headers, build dir), CODING.md (database layout, exception snippet, warnings, build dir), database/CLAUDE.md (header split), validations/{jpeg,png,heic,nef,mov}/CLAUDE.md (`*_detail` namespaces). Build dir corrected to `build/` (CMakePresets.json:7) in CLAUDE.md, CODING.md, docs/user/architecture.md, docs/user/getting-started.md, 0021. Historical docs/logs entries left as-is. Verified against code: root CMakeLists.txt option, SYSTEM on both FetchContent deps, database/ headers, imager/ split headers, `*_detail` namespaces.
- **Next Step**: Match the 0012 table to td-test's audit. getting-started.md ctest sample output is stale (8 tests) and database/CMakeLists.txt:24 still has a redundant per-target warning line.

## [2026-10-31 23:00] - kFoo rename follow-up
- **Agent**: bookkeeper (td-docs)
- **Task**: Update current docs after `kStreamingThreshold` -> `STREAMING_THRESHOLD` and the project-wide L4 rename.
- **Outcome**: docs/DEVELOPMENT_NOTES.md and docs/plan/README.md (`imager/Imager.cpp:42`, verified in code) updated; 0012 table rows M1, L1, L4 and remainder note revised. getting-started.md ctest sample refreshed to an excerpt ending "out of 37". Historical plans 0009/0017/0019 and logs untouched.

## [2026-10-31 24:00] - Streaming step documentation (8 MB threshold, validateOnlyFile dry-run)
- **Agent**: bookkeeper (st-docs)
- **Task**: Document the streaming step from st-dev's summary: `STREAMING_THRESHOLD` 256 MB -> 8 MB, `.aae` TooLarge above 8 MB, no memory fallback for formats without a stream validator, `imagestore --dry-run` via `validateOnlyFile`.
- **Outcome**: Added a dated update with measurements to plan 0017 and updated its status line; 0006 status line; plan/README.md "Next work" item 5 removed; DEVELOPMENT_NOTES.md, DEPLOYMENT.md (sizing rule about 12 MB per concurrent upload), architecture.md, imagestore-cli.md, README.md API table, CLAUDE.md, ANALYSIS.md (phase 2 summary plus four dry-run gap notes), testing/loose-ends.md. Values verified against Imager.cpp:43 and Imager.h:55. Historical plan bodies and logs untouched.
- **Next Step**: No CHANGELOG file exists under docs/, so no entry was added. The 0017 body, 0019:106, 0020:193 and 0022:398 still say 256 MB (historical).

## [2026-10-31 24:30] - Next work findings from the streaming step
- **Agent**: bookkeeper (st-docs)
- **Task**: Add two pre-existing findings to the plan README "Next work" list.
- **Outcome**: docs/plan/README.md items 5 (HEIC accepts a half-truncated file) and 6 (`addFile` empty id on DuplicateFile; workaround at server/ItemRoutes.cpp:173-177).
- **Next Step**: None.

## [2026-10-31 25:00] - Decoder-memory caveat in sizing docs
- **Agent**: bookkeeper (st-docs)
- **Task**: Rewrite the per-upload memory sizing to include decoder working memory (st-dev estimates, not measured).
- **Outcome**: DEPLOYMENT.md sizing rule and per-format estimate table; caveats in architecture.md, DEVELOPMENT_NOTES.md, CLAUDE.md, ANALYSIS.md, plan 0017 dated note; plan/README.md Next work item 7 (measure decoder memory, consider caps).
- **Next Step**: None.

## [2026-10-31 25:30] - Per-root write buffer in sizing docs
- **Agent**: bookkeeper (st-docs)
- **Task**: Word file I/O memory as up to 8 MB (in-memory) or 4 MB per target root while writing (streamed), per team-lead (FileStorage.cpp:179, STREAM_CHUNK_SIZE verified).
- **Outcome**: DEPLOYMENT.md, architecture.md, DEVELOPMENT_NOTES.md, ANALYSIS.md.
- **Next Step**: None.

## [2026-10-31 26:00] - Server metrics docs (/stats admin sections, /metrics)
- **Agent**: bookkeeper (m4-docs)
- **Task**: Document the server request metrics step per m4-dev's summary, verified against server/ServerMetrics.cpp and server/App.cpp.
- **Outcome**: Edited docs/user/API.md (/stats, new /metrics, plan-differences bullet), docs/user/DEPLOYMENT.md (Monitoring section, Prometheus scrape example), docs/user/metrics.md, README.md, CLAUDE.md, docs/plan/0005 status, docs/plan/README.md, docs/plan/0022 metrics note, docs/analysis/ANALYSIS.md. Example JSON/text values are illustrative; structure is from the code. No CHANGELOG exists in the repo, none created.
- **Next Step**: CLAUDE.md test-suite list still needs m4-test's new test names and count.

## [2026-10-31 26:30] - Test count/list refresh
- **Agent**: bookkeeper (m4-docs)
- **Task**: Fix stale test list per team-lead (add ImagerMemoryTests, imagestore_dryrun_streaming_tests, server_streaming_mem_test, server_metrics_test).
- **Outcome**: CLAUDE.md, docs/analysis/ANALYSIS.md, docs/user/getting-started.md now say 41 tests (provisional). docs/testing/README.md has no count.
- **Next Step**: Confirm 41 against m4-test's `ctest -N`.

## [2026-10-31 27:00] - Structured access log docs
- **Agent**: bookkeeper (l5-docs)
- **Task**: Document `[server].access_log_format` (text|json) and `X-Request-Id`.
- **Outcome**: Edited docs/user/DEPLOYMENT.md (config row, JSON format, field table, jq example, Request ids), docs/user/API.md (response header, plan-differences bullet), docs/plan/0022 §12 note, docs/plan/README.md, docs/analysis/ANALYSIS.md, CLAUDE.md, README.md. Verified against server/AccessLog.cpp, config/Config.cpp, AuthMiddleware.h, Upload.cpp. Example line values illustrative. No CHANGELOG in repo.
- **Next Step**: CLAUDE.md/ANALYSIS.md test count (new test_access_log_json.sh) pending team-lead.

## [2026-10-31 27:30] - Test count 42, unmatched-route notes
- **Agent**: bookkeeper (l5-docs)
- **Task**: Apply bytes correction, unmatched-route limits, final test count.
- **Outcome**: DEPLOYMENT.md/API.md/CLAUDE.md updated; CLAUDE.md, ANALYSIS.md, getting-started.md now 42 tests incl. server_access_log_json_test.
- **Next Step**: none.
