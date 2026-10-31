# Writing the server part for the Image/Video Organizer program

## PHASE 1: core features, offline work

C++23, capable of running on modern Linux and macOS

Designed primarily for local networks with small number of users but quite large number of Images and Videos

Estimated total Image/Video library size is about 4 terabytes, possibly up to 10. Each file is about single megabytes to maybe single-number gigabytes for longer videos.

Images are stored as files on the disk, the root directory for images are provided through the application configuration file. There should be the possibility to provide multiple root directories to store the files in several copies to prevent loss of data in case of HDD malfunction.

There is the SQLite-based database along with the files located in the place specified in the application configuration file.

The application configuration file is supposed to be read on the start of the app and its changes when the app is running are not applied before the restart.

Images are identified with the sha256 of the entire file; this is being done to prevent duplicates.

before being stored, the image should be verified to be valid. For that, the specific libraries have been created, located in "validations/" directory: "validations/jpeg", "validations/png", "validations/heic", "validations/nef", "validations/mov", and "validations/aae" for, respectively JPEG, PNG, HEIC, Nikon NEF, MOV/MP4, and Apple AAE sidecar files. A common wrapping interface (IValidator) is used across all formats.

AAE files are sidecar files produced by Apple devices; they are stored using their parent image's content hash as a filename prefix, preserving the pairing relationship. Orphan AAE files (added before their parent) are relocated when the parent arrives.

The imagestore utility provides batch import from stdin with bounded concurrency, progress tracking, and an error log file.

The invalid image (i.e., image that failed to be validated with the appropriate validation library) should not be added and the appropriate request should return appropriate error.

The facade interface should provide functions to:

* Add image: accepts raw data as a parameter, returns either the id of the image (which is the sha256 hash of the source file), or error: broken image, duplicate image, not supported format, other errors possibly.
* Get image by id; all of the image's tags should be provided along with the image, maybe in a separate return or something
* Get list of IDs of images by given set of tags: all images should be associated with all tags provided; Pagination should be supported, i.e., the index of the starting image and number of images to return

Offer more interface functions.

## PHASE 2: network features, HTTP-accessible daemon

The program created at PHASE 1 becomes web-ready.

Network protocol is HTTP(S), Crow is the library for handling HTTP(S) requests

Secure authentication on the web today is Bearer token authentication using JWT (JSON Web Tokens), typically passed in the Authorization: Bearer <token> header. It's the backbone of most modern REST APIs and OAuth 2.0 flows. The application will use jwt-cpp library to handle the authentication.

## Plan status overview (2026-10-31)

Each plan carries its own `Status` line; this table is a summary. Per-plan design documents are indexed in the project `CLAUDE.md`.

| Plan | Status |
|---|---|
| 0001–0004 | Implemented |
| 0005 MONITORING | Substantially implemented (`imagerd` exports metrics via admin `GET /stats` and Prometheus `GET /metrics`; no OpenTelemetry) |
| 0006 UTILITY | Implemented (`--graph` later removed by 0016; `--delete`, `--memory-report` added) |
| 0007–0010 (HEIC, NEF, MOV, AAE) | Implemented |
| 0011 ARCHIVES | Planned, not implemented |
| 0012 CODING_STANDARDS | Implemented (`-Werror` via `IMAGER_WERROR`, database and validator header split; see its status table) |
| 0013 PROGRESS | Implemented |
| 0014 DISPLAY | Partially superseded (`--graph` removed by 0016) |
| 0015 GAPS | Historical snapshot, not maintained |
| 0016 VERBOSE_OUTPUT, 0017 LARGE_FILES | Implemented |
| 0018 MEMORY | Instrumentation implemented; investigation open |
| 0019 DELETE, 0020 TIMESTAMPS | Implemented |
| 0021 REORGANIZATION | Implemented (settings hygiene done 2026-10-31) |
| 0022 SERVER (Phase 2) | Implemented; see [API](../user/API.md), [DEPLOYMENT](../user/DEPLOYMENT.md) |
| 0023 TRANSCODING | Proposal, not implemented |

## Next work

1. **[0023 TRANSCODING](0023.TRANSCODING.md)** — design only so far; the next piece of planned work.
2. **[0011 ARCHIVES](0011.ARCHIVES.md)** — needs a decision on which archive library to use before implementation (the project allows no new libraries without discussion).
3. **[0018 MEMORY](0018.MEMORY.md)** — the instrumentation exists; the memory-growth investigation itself is still open.
4. **Known behaviour:** uploads still in progress when `imagerd` shuts down are cut off once the drain timeout expires (exit code 3); there is no resumable upload. Not planned to change.
5. **Finding (pre-existing): weak HEIC validation.** The HEIC validator accepts a file truncated to half its size, on both the in-memory and the streaming path, so its validation is weaker than for the other formats. Found by the tests of the streaming step; not yet fixed.
6. **Finding (pre-existing): empty id on duplicate.** `Imager::addFile` returns an empty `id` with `DuplicateFile`, while `validateOnly` and `validateOnlyFile` return it. The server works around this in `server/ItemRoutes.cpp:173-177` by parsing the id from the tail of the message. Returning the id from `addFile` would remove the workaround.
7. **Measure decoder memory:** the per-format decoder figures in [DEPLOYMENT.md](../user/DEPLOYMENT.md#large-uploads-and-memory) are estimates from reading the code. Measure peak RSS with real large files per format (progressive JPEG, HEIC, NEF, 4K HEVC MOV) and consider caps such as a maximum image pixel count or libheif security limits.
