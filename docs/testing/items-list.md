# Testing: Item listing (`GET /items`)

**Plan Reference**: `docs/plan/0022.SERVER.md` §6.2, §7, §14.2–14.3, §16 row G2
**Status**: complete
**Coverage**: 8/8 acceptance criteria covered (100%) by 79 assertions

Test: `server/test/test_list.sh.in` → ctest `server_list_test` (ports 19520–19524, 120 s timeout).
Config has **two targets**; tags are seeded with python3/sqlite3 into **both** per-target DBs (tag routes arrive in milestone H).
Fixtures: 6 distinct JPEGs derived from `validations/jpeg/test/testimg.jpg` by inserting a unique COM segment after SOI
(`p1..p6.jpg`), plus `validations/mov/test/fixtures/valid.mov` (`clip.mov`). A duplicate re-upload must not add an item.
Tag layout: p1 {alpha,beta}, p2 {alpha}, p3 {beta}, p6 {gamma}, p4/p5/clip.mov untagged. Total 7 items.

## Test Inventory

| Test | Criteria Covered | Status |
|------|------------------|--------|
| empty library: all / untagged / tags=a → 200 and empty `items` | empty list | ✅ |
| no filter: 7 items, names exact, unique ids, shape `{id,name,size,ext,tags}`, types, tags/ext/size values, envelope offset/limit | all items, item shape | ✅ |
| `tags=alpha` → p1,p2; `tags=alpha,beta` → p1 only (AND); `alpha,gamma` → empty; unknown → empty; known+unknown → empty | tag filter | ✅ |
| `untagged=true` → exactly p4,p5,clip.mov, all with empty tags | untagged | ✅ |
| limit=3 pages at offsets 0/3/6 → 3/3/1 items; union == full set, no dup/gap, order matches unpaged; offset echoed | pagination | ✅ |
| offset past end and offset==total → empty; limit=1; filter+pagination combined | page bounds | ✅ |
| 400 for offset=-1, limit=-1, limit=0, offset=abc, limit=abc, limit=1.5, limit=100000, overflow; error code BadRequest | bad values | ✅ |
| extra 400s: limit=501, offset>2^31-1, empty/`5x`/`+1` values, `tags=`, `a,,b`, `,a`, untagged=yes/1/empty, untagged=true+tags; limit=500 ok; max offset ok; untagged=false = no filter; repeated/duplicate tags; no trimming; id ordering | contract edges | ✅ |
| 401 with no token / bad token / no token with filter | auth | ✅ |
| SIGTERM clean exit | shutdown | ✅ |

## Progress Log

- **2026-10-31**: Script and CMake registration written; aligned to confirmed contract; run: 79 passed, 0 failed; full ctest 27/27.

## Contract (confirmed by g2-dev)

limit default 50, 1..500, no clamping; offset 0..2147483647; envelope `{items, offset, limit}` (no total/next);
tag-filter and untagged results ordered by id; untagged=true + tags → 400.

## Known Gaps

- Ordering by id ascending is asserted on all three paths (all SQL uses ORDER BY id).
- Multi-target consistency is seeded identically; divergent targets are not tested.
