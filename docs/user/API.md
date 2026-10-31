# REST API Reference

HTTP(S) API served by `imagerd`. Source of truth: `server/*.cpp`. Design rationale: [plan 0022](../plan/0022.SERVER.md). For running the daemon see [Deployment and Operations](DEPLOYMENT.md).

All request and response bodies are JSON (`Content-Type: application/json`) except `PUT /items` (raw file bytes) and `GET /items/{id}/content` (raw file bytes). Field names are camelCase.

## Contents

- [Conventions](#conventions)
- [Authentication flow](#authentication-flow)
- [Error envelope and codes](#error-envelope-and-codes)
- [Pagination](#pagination)
- [Validation rules](#validation-rules)
- [Common response headers](#common-response-headers)
- Endpoints: [health](#get-health), [stats](#get-stats), [auth](#post-authlogin), [users](#get-users), [items](#put-items), [item tags](#get-itemsidtags), [global tags](#get-tags)
- [curl walkthrough](#curl-walkthrough)
- [Differences from plan 0022](#differences-from-plan-0022)

## Conventions

| Term | Meaning |
|------|---------|
| none | No authentication required |
| Bearer | `Authorization: Bearer <token>` from `POST /auth/login`; any enabled user |
| admin | Bearer, and the user's `isAdmin` is currently true |

`{id}` is an item id: 64 lowercase hex characters (SHA-256 of the file). `{login}` and `{name}` are percent-decoded path segments (`+` stays literal).

## Authentication flow

1. `POST /auth/login` with `{"login","password"}` returns a token and `expiresAt` (unix seconds).
2. Send `Authorization: Bearer <token>` on every other request (except `/health`).
3. Tokens are HS256 JWTs signed with the server secret, with the configured issuer. The lifetime is `[auth].token_ttl_seconds` (default 43200 = 12 h). There is no refresh endpoint: log in again.

**Revocation is live.** On every request the server re-reads the user from the user database after verifying the token. A deleted or disabled user gets `401` immediately even with an unexpired token. `isAdmin` and `fullName` come from the database, not the token, so promotion/demotion takes effect on the next request. Changing a password does not invalidate existing tokens.

**Login throttle** (in memory, reset on restart, window and lockout 900 s):

| Key | Failures to lock out | Reset by |
|-----|----------------------|----------|
| login name | 5 | a successful login for that login |
| client IP (`remote_ip_address`) | 20 | never (only expiry) |

Failures counted: unknown user or wrong password. A locked-out key gets `429 TooManyRequests` with `Retry-After: 900`, even for the correct password. The check runs before credential verification. Disabled-account attempts (correct password) return `403` and do not count. `X-Forwarded-For` is not trusted, so behind a reverse proxy the per-IP counter sees only the proxy's address (see [Deployment](DEPLOYMENT.md#reverse-proxy)).

Every `401` carries `WWW-Authenticate: Bearer`.

## Error envelope and codes

Every error produced by a handler or the auth/size middleware is:

```json
{"error":{"code":"NotFound","message":"item not found"}}
```

`code` is a stable string; `message` is human-readable and may change. 500 messages are always the generic `internal error`; details go to the log.

| Status | `code` | Typical cause |
|--------|--------|---------------|
| 400 | `BadRequest` | Malformed JSON, missing/ill-typed field, bad id/tag/login/query value |
| 400 | `BrokenFile` | Uploaded file failed format validation |
| 401 | `Unauthorized` | Missing/invalid/expired token, deleted or disabled user, wrong credentials |
| 403 | `Forbidden` | Admin required; account disabled (login) |
| 404 | `NotFound` | Item, tag, or user does not exist |
| 409 | `Conflict` | Duplicate user or tag; ambiguous AAE sidecar |
| 409 | `LastAdmin` | Operation would leave no enabled admin |
| 413 | `TooLarge` | JSON body over 64 KiB; upload over `max_upload_mb`; per-format size limit |
| 415 | `UnsupportedFormat` | Upload extension/format not supported |
| 429 | `TooManyRequests` | Login / password-change throttle (with `Retry-After`) |
| 500 | `Internal` | Storage, database, or config error; unexpected exception |

Facade `ErrorCode` mapping (`server/Json.cpp:19`): `BrokenFile`->400/`BrokenFile`, `DuplicateFile`->409/`Conflict`, `UnsupportedFormat`->415, `FileNotFound`->404/`NotFound`, `StorageError`/`DatabaseError`/`ConfigError`->500/`Internal`, `AmbiguousSidecar`->409/`Conflict`, `TooLarge`->413.

Responses that do not use the envelope: `204 No Content`, `206`, `304`, `416` (empty bodies), and routing-level responses for unknown paths or methods (Crow's default 404/405; not guaranteed to carry an envelope).

## Pagination

Listing routes (`GET /items`, `GET /tags`, `GET /users`) accept:

| Param | Type | Range | Default |
|-------|------|-------|---------|
| `offset` | integer | 0..2147483647 | 0 |
| `limit` | integer | 1..500 | 50 |

Out-of-range or non-numeric values return `400 BadRequest` (no clamping). Only `GET /items` echoes `offset` and `limit` in the response; the others return just the array. There is no total count: fetch until a page returns fewer than `limit` entries.

## Validation rules

| Item | Rule |
|------|------|
| Item id | exactly 64 chars `[0-9a-f]` (uppercase rejected) |
| Tag name | 1..255 bytes, valid UTF-8, no comma, no control characters (bytes < 0x20, 0x7F, NUL) |
| User login (create) | 1..64 chars from `[A-Za-z0-9._@-]` |
| Login request | `login` <= 64 bytes, `password` <= 1024 bytes (no charset check, to avoid enumeration) |
| fullName | 1..255 bytes |
| Password (set/create/change) | 8..1024 bytes |
| JSON body | <= 64 KiB (65536 bytes) on every route except `PUT /items`; larger gives `413 TooLarge` before parsing |
| Upload body | non-empty and <= `[server].max_upload_mb` (default 4096 MiB); also per-format limits from `[file_size_limits]` |
| Upload filename | at most 255 bytes after stripping directories (`/` and `\`), no control characters, not `.` or `..` |

## Common response headers

Added to every response by the middleware (`server/AuthMiddleware.h:60`):

- `X-Content-Type-Options: nosniff`
- `Cache-Control: no-store`, unless the handler set one (only item content does)

## Endpoints

### GET /health

Auth: none. Liveness probe.

`200` `{"status":"ok"}`

### GET /stats

Auth: Bearer.

`200`:

```json
{"imageCount":1234,"tagCount":17}
```

Errors: `401 Unauthorized`, `500 Internal`.

### POST /auth/login

Auth: none. Body (<= 64 KiB):

```json
{"login":"alice","password":"correct horse"}
```

`200`:

```json
{"token":"<jwt>","expiresAt":1793000000,"login":"alice","fullName":"Alice","isAdmin":false}
```

| Status | `code` | When |
|--------|--------|------|
| 400 | `BadRequest` | body not an object with string `login` and `password`; login > 64 bytes or password > 1024 bytes |
| 401 | `Unauthorized` | unknown user or wrong password (identical response) |
| 403 | `Forbidden` | correct credentials, account disabled |
| 429 | `TooManyRequests` | throttled; header `Retry-After: 900` |

### GET /auth/me

Auth: Bearer. `200` `{"login","fullName","isAdmin"}`. Errors: `401`.

### POST /auth/password

Auth: Bearer. Changes the caller's own password.

Body: `{"oldPassword":"...","newPassword":"..."}` (new: 8..1024 bytes). `204` on success.

Errors: `400 BadRequest` (shape, new password length, `oldPassword` over 1024 bytes), `401 Unauthorized` (not authenticated, or `oldPassword` wrong), `429 TooManyRequests` with `Retry-After: 900` (see below).

A wrong `oldPassword` counts as a failure against the **same** per-login and per-IP counters as `POST /auth/login` (shared budget: guesses through either endpoint add up). A locked-out key gets `429` before the password is checked, even with the correct `oldPassword`. A successful change resets the per-login counter.

### GET /users

Auth: admin. Query: `offset`, `limit` ([Pagination](#pagination)). `200`:

```json
{"users":[{"login":"alice","fullName":"Alice","isAdmin":true,"enabled":true,"createdAt":1793000000,"updatedAt":1793000000}]}
```

`createdAt`/`updatedAt` are unix seconds. Errors: `400`, `401`, `403`. Password material is never returned.

### POST /users

Auth: admin. Body requires all four fields: `login` (string), `fullName` (string), `password` (string), `isAdmin` (boolean). New users are enabled.

`201` with the user object (as in `GET /users`).

Errors: `400 BadRequest` (missing/ill-typed field, login charset/length, fullName, password), `401`, `403`, `409 Conflict` (login exists).

### GET /users/{login}

Auth: admin. `200` user object. Errors: `400` (bad percent-encoding), `401`, `403`, `404 NotFound`.

### PATCH /users/{login}

Auth: admin. Body: JSON object with at least one of `fullName` (string), `password` (string), `isAdmin` (bool), `enabled` (bool). Unknown fields, wrong types, or an empty object return `400`.

`204` on success. Errors: `400`, `401`, `403`, `404 NotFound`, `409 LastAdmin` (demoting or disabling the last enabled admin).

### DELETE /users/{login}

Auth: admin. `204`. Admins may delete themselves unless they are the last enabled admin. Errors: `400`, `401`, `403`, `404 NotFound`, `409 LastAdmin`.

### PUT /items

Auth: Bearer (any user). Uploads one file. The body is the raw file bytes (any `Content-Type`).

Filename: header `X-Filename`, else query `?filename=`. Only the extension and base name matter: the extension selects the validator. The server hashes the content; the id is the SHA-256.

| Status | Body | When |
|--------|------|------|
| 201 | `{"id":"<64 hex>","duplicate":false}` | stored |
| 200 | `{"id":"<64 hex>","duplicate":true}` | identical content already stored (not an error) |

Errors:

| Status | `code` | When |
|--------|--------|------|
| 400 | `BadRequest` | no filename, bad filename, empty body |
| 400 | `BrokenFile` | content fails validation |
| 401 | `Unauthorized` | |
| 409 | `Conflict` | ambiguous AAE sidecar |
| 413 | `TooLarge` | over `max_upload_mb` or the per-format limit |
| 415 | `UnsupportedFormat` | |
| 500 | `Internal` | spool or storage/database failure |

Supported formats: [formats.md](formats.md). Memory behaviour for large uploads: [Deployment](DEPLOYMENT.md#large-uploads-and-memory).

### GET /items

Auth: Bearer. Lists items in database order (no sort parameter).

| Query | Type | Meaning |
|-------|------|---------|
| `tags` | string | Comma-separated tag names; item must have all (AND). May be repeated; duplicates ignored. Each name follows the tag rule; empty elements give 400. |
| `untagged` | `true`/`false` | Only items with no tags. Cannot be combined with `tags` (400). |
| `offset`, `limit` | integer | [Pagination](#pagination) |

Tag names containing commas cannot be filtered with this parameter. A `%00` anywhere in the query returns 400.

`200`:

```json
{"items":[{"id":"<64 hex>","name":"IMG_0001.jpg","size":2481920,"ext":"jpg","tags":["holiday","2026"]}],"offset":0,"limit":50}
```

Errors: `400 BadRequest`, `401`, `500 Internal`.

### GET /items/{id}

Auth: Bearer. `200` single item object as above. Errors: `400` (id format), `401`, `404 NotFound`, `500`.

### GET /items/{id}/content

Auth: Bearer. Returns the file bytes.

Request headers:

| Header | Behaviour |
|--------|-----------|
| `Range: bytes=a-b`, `bytes=a-`, `bytes=-n` | Single range only. Malformed or multi-range headers are ignored and the full file is returned with 200. |
| `If-None-Match` | Matches `"<id>"` (weak `W/` prefix tolerated), lists, or `*`; gives 304. |

| Status | Meaning | Headers |
|--------|---------|---------|
| 200 | Full file, streamed | `Content-Type`, `Accept-Ranges: bytes`, `ETag: "<id>"`, `Cache-Control: private, immutable` |
| 206 | Partial content | as 200 plus `Content-Range: bytes first-last/size` |
| 304 | `If-None-Match` matched | `ETag`, `Cache-Control: private, immutable` |
| 416 | Range start beyond end of file, or zero-length suffix | `Content-Range: bytes */size`, `Accept-Ranges: bytes` |

Each 206 is capped at 8 MiB (8388608 bytes): a longer request is truncated and `Content-Range` reports the bytes actually sent, so clients must continue from `last+1`. The conditional check precedes the file-size check.

`Content-Type` by extension: jpg/jpeg `image/jpeg`, png `image/png`, heic/heif `image/heic`, nef `image/x-nikon-nef`, mov `video/quicktime`, mp4 `video/mp4`, aae `application/xml`, otherwise `application/octet-stream`.

Errors: `400` (id format), `401`, `404 NotFound` (unknown item, or file missing on disk), `500 Internal`.

### DELETE /items/{id}

Auth: Bearer (any user, not admin-only). Deletes the item from all targets and cascades to its AAE sidecar. `204`. Errors: `400`, `401`, `404 NotFound`, `500`.

### GET /items/{id}/tags

Auth: Bearer. `200` `{"tags":["a","b"]}`. Errors: `400`, `401`, `404 NotFound` (item).

### PUT /items/{id}/tags

Auth: Bearer. Replaces the item's tag set. Body: `{"tags":["a","b"]}` (empty array clears). Unknown tags are created. `200` `{"tags":[...]}` with the resulting set.

Errors: `400 BadRequest` (shape or tag name), `401`, `404 NotFound` (item), `500`.

### POST /items/{id}/tags

Auth: Bearer. Adds tags to the existing set. Body: `{"tags":["a","b"]}` or `{"tag":"a"}`. Already-present tags are ignored. `200` `{"tags":[...]}` with the resulting set. Errors as PUT.

### DELETE /items/{id}/tags/{tag}

Auth: Bearer. Removes one tag from an item. `{tag}` is percent-encoded. `204`.

Errors: `400` (id, percent-encoding, tag name), `401`, `404 NotFound` (item missing, or tag not set on item; message distinguishes), `500`.

### GET /tags

Auth: Bearer. Query: `offset`, `limit`. `200` `{"tags":["a","b"]}`. Errors: `400`, `401`, `500`.

### POST /tags

Auth: Bearer (any user). Body `{"name":"holiday"}`. `201` `{"name":"holiday"}`. Errors: `400` (shape or name), `401`, `409 Conflict` (exists), `500`.

### DELETE /tags/{name}

Auth: Bearer (any user). Deletes the tag globally and unbinds it from all items. `204`. Errors: `400`, `401`, `404 NotFound`, `500`.

## curl walkthrough

Assumes `imagerd` on `http://127.0.0.1:8443` and an admin `alice` created with `imageradmin` ([Deployment](DEPLOYMENT.md#first-admin-bootstrap)). Use `-k` or `--cacert` with a self-signed TLS certificate.

```bash
BASE=http://127.0.0.1:8443

# Log in; extract the token (jq optional)
TOKEN=$(curl -s -X POST $BASE/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"login":"alice","password":"s3cret-pass"}' | jq -r .token)
AUTH="Authorization: Bearer $TOKEN"

# Upload; X-Filename selects the validator by extension
curl -s -X PUT $BASE/items -H "$AUTH" -H 'X-Filename: IMG_0001.jpg' --data-binary @IMG_0001.jpg
# {"id":"<64 hex>","duplicate":false}   (HTTP 201; a repeat upload returns 200 and "duplicate":true)

ID=<id from above>

# Tag it, then search
curl -s -X PUT $BASE/items/$ID/tags -H "$AUTH" -d '{"tags":["holiday","2026"]}'
curl -s "$BASE/items?tags=holiday,2026&limit=10" -H "$AUTH"

# Download, then fetch the first KiB only
curl -s $BASE/items/$ID/content -H "$AUTH" -o copy.jpg
curl -s -i $BASE/items/$ID/content -H "$AUTH" -H 'Range: bytes=0-1023' -o /dev/null

# Admin: create a user
curl -s -X POST $BASE/users -H "$AUTH" \
  -d '{"login":"bob","fullName":"Bob","password":"another-pass","isAdmin":false}'

# Delete the item
curl -s -X DELETE $BASE/items/$ID -H "$AUTH"
```

## Differences from plan 0022

Where [plan 0022](../plan/0022.SERVER.md) and the code differ, this document follows the code:

- Validation failure is `400 BrokenFile`, not 422 (Crow has no 422 status).
- Error `code` vocabulary is generic (`NotFound`, `Conflict`, `Internal`, ...) rather than facade-specific names; `LastAdmin` and `TooManyRequests` are additions.
- `GET /stats` returns only `imageCount` and `tagCount`, not the metrics snapshot.
- Range responses are capped at 8 MiB each.
- Token validity is re-checked against the user database per request (live revocation, live admin flag).
- A per-IP login throttle (20 failures) exists in addition to the per-login one (5).
- The daemon has no structured access log; see [Deployment](DEPLOYMENT.md#logging).
