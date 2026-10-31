# Deployment and Operations

How to build, configure, run, and operate the `imagerd` HTTP(S) daemon. The endpoints are documented in the [REST API Reference](API.md); design rationale is in [plan 0022](../plan/0022.SERVER.md).

## Contents

- [Build](#build)
- [Configuration reference](#configuration-reference)
- [JWT secret](#jwt-secret)
- [First admin bootstrap](#first-admin-bootstrap)
- [Running imagerd](#running-imagerd)
- [Native TLS](#native-tls)
- [Reverse proxy](#reverse-proxy)
- [Large uploads and memory](#large-uploads-and-memory)
- [Backups and multiple targets](#backups-and-multiple-targets)
- [Logging](#logging)
- [Monitoring](#monitoring)
- [Access log](#access-log)
- [Troubleshooting](#troubleshooting)

## Build

The server needs everything the library needs ([Getting Started](getting-started.md)) plus:

| Dependency | Debian/Ubuntu package | Integration |
|------------|----------------------|-------------|
| asio (standalone) | `libasio-dev` | system, located by Crow's bundled `Findasio.cmake` |
| cpp-jwt | `libcpp-jwt-dev` (pulls in `nlohmann-json`) | system, `find_package(cpp-jwt CONFIG REQUIRED)` |
| OpenSSL | `libssl-dev` | system, `find_package(OpenSSL REQUIRED)`; used for TLS, SHA-256, PBKDF2 |
| Crow v1.2.1.2 | none | downloaded by CMake FetchContent at configure time (needs network access once) |

```bash
apt-get install libasio-dev libcpp-jwt-dev libssl-dev
cmake --preset default
cmake --build --preset default
```

Build output is under `build/` (see `CMakePresets.json`). The relevant executables are `imagerd` (the daemon, built from `server/`) and `imageradmin` (user administration CLI). Crow is configured without Boost (`CROW_USE_BOOST=OFF`) and with `CROW_ENABLE_SSL`.

## Configuration reference

`imagerd` and `imageradmin` read one TOML file, once at startup (no hot reload; restart to apply changes). The `[[targets]]` and `[file_size_limits]` sections are shared with the offline tools, see the [Configuration Guide](configuration.md). The daemon adds `[server]` and `[auth]`. Both are optional at parse time, but `[auth]` needs a database path and a secret in practice.

### `[server]`

| Key | Type | Default | Notes |
|-----|------|---------|-------|
| `bind` | string | `"0.0.0.0"` | Listen address. Use `"127.0.0.1"` behind a reverse proxy. |
| `port` | integer | `8443` | 1..65535, otherwise a config error. |
| `tls` | boolean | `false` | Serve HTTPS natively. See [Native TLS](#native-tls). |
| `tls_cert` | string (path) | empty | PEM certificate or full chain. Required when `tls = true`. |
| `tls_key` | string (path) | empty | PEM private key. Required when `tls = true`. |
| `threads` | integer | `0` | Worker threads; `0` means `std::thread::hardware_concurrency()`. |
| `max_upload_mb` | integer | `4096` | Cap for `PUT /items`, in MiB; must be > 0. Over the cap gives `413 TooLarge` before the body is read. |
| `trusted_proxies` | array of strings | `[]` | IPv4/IPv6 addresses or CIDRs of reverse proxies whose `X-Forwarded-For` is trusted. Empty means the header is ignored. An invalid entry is a startup error (exit 2). See [Reverse proxy](#reverse-proxy). |
| `access_log` | boolean | `true` | One log line per request on stderr. See [Access log](#access-log). |
| `access_log_format` | string | `"text"` | `"text"` (eight-field line) or `"json"` (one JSON object per line). Any other value is a startup error (exit 2). Only matters when `access_log = true`. See [Access log](#access-log). |
| `drain_timeout_s` | integer | `30` | Seconds to wait for running requests on `SIGINT`/`SIGTERM` before forcing exit `3`. Range 1..3600. |

### `[auth]`

| Key | Type | Default | Notes |
|-----|------|---------|-------|
| `database` | string (path) | empty | SQLite file holding users. Created with its schema on first open, so its directory must exist. Required for `imagerd` and `imageradmin`: an explicitly empty `database = ""` is a config error, and an absent one is refused at startup (SQLite would otherwise silently open a throwaway temporary database). Separate from the per-target image databases. |
| `jwt_secret_file` | string (path) | empty | Preferred. Takes precedence over `jwt_secret`. See [JWT secret](#jwt-secret). |
| `jwt_secret` | string | empty | Inline secret. Discouraged: the config file is then a secret. |
| `token_ttl_seconds` | integer | `43200` (12 h) | Must be > 0. |
| `issuer` | string | `"phto"` | JWT `iss`; tokens with a different issuer are rejected. Changing it invalidates all tokens. |
| `pbkdf2_iterations` | integer | `310000` | PBKDF2 iterations for new password hashes; must be >= 1000. Existing hashes keep working. |
| `lockout_seconds` | integer | `900` | Login throttle window and lockout duration; also the `Retry-After` value on `429`. Range 1..86400. |
| `max_login_failures` | integer | `5` | Failures per login name before lockout. Range 1..1000. |
| `max_ip_failures` | integer | `20` | Failures per client IP before lockout. Range 1..100000. |

A value outside its range, or of the wrong type, makes `imagerd` exit `2` with an `imagerd:` message naming the key.

### Example

```toml
[[targets]]
root     = "/mnt/disk1/images"
database = "/mnt/disk1/imager.db"

[[targets]]
root     = "/mnt/disk2/images-backup"
database = "/mnt/disk2/imager.db"

[server]
bind          = "127.0.0.1"
port          = 8443
max_upload_mb = 8192

[auth]
database        = "/var/lib/imager/auth.db"
jwt_secret_file = "/etc/imager/jwt.secret"
```

## JWT secret

Tokens are HS256, so anyone holding the secret can mint admin tokens. Requirements enforced by `imagerd` (`server/main.cpp`):

- `jwt_secret_file` must exist, be a regular file, and not be readable by "other" (`chmod 600` or `640`). Symlinks are followed by `stat`.
- One trailing newline is stripped; everything else counts, so avoid trailing spaces.
- At least 32 bytes after trimming, whether from the file or from `jwt_secret`.

Generate one:

```bash
umask 077
openssl rand -base64 48 > /etc/imager/jwt.secret
chmod 600 /etc/imager/jwt.secret
```

Rotating the secret invalidates every issued token; users must log in again.

## First admin bootstrap

There is no self-service registration. Create the first admin from the shell with `imageradmin`, using the same config file. The auth database is created if needed.

```bash
imageradmin /etc/imager/imager.toml user add alice "Alice Example" --admin
```

The command prompts twice for the password on the terminal (read from `/dev/tty`, no echo). For scripts, read one line from stdin:

```bash
printf '%s\n' "$PASSWORD" | imageradmin /etc/imager/imager.toml user add alice "Alice Example" --admin --password-stdin
```

All subcommands:

```
imageradmin <config.toml> user add    <login> <full name> [--admin] [--password-stdin]
imageradmin <config.toml> user del    <login>
imageradmin <config.toml> user passwd <login> [--password-stdin]
imageradmin <config.toml> user enable  <login>
imageradmin <config.toml> user disable <login>
imageradmin <config.toml> user promote <login>
imageradmin <config.toml> user demote  <login>
imageradmin <config.toml> user list   [--offset N] [--limit N]
```

Exit codes: `0` success, `1` usage error, `2` config/auth/database error (user not found, duplicate, ...). `imageradmin user add` and `passwd` apply the same field rules as the REST API (login 1..64 of `[A-Za-z0-9._@-]`, full name 1..255 bytes, password 8..1024 bytes); a violation prints `Error: <rule>` and exits `1`. `user del`, `disable` and `demote` apply the same last-admin guard as the REST API: removing, disabling or demoting the last enabled admin prints `Error: Cannot ...` and exits `2` without changing anything (`add --admin` and `promote` are unaffected, so bootstrap and recovery still work). `imageradmin` can run while `imagerd` is up, since both use SQLite with locking; changes take effect on the next request (tokens are re-checked per request).

`imageradmin` does not validate login characters the way `POST /users` does (`[A-Za-z0-9._@-]`, 1..64). Stick to that set so users can also be managed over REST.

Other users are normally created over REST by an admin ([API](API.md#post-users)).

## Running imagerd

```bash
imagerd /etc/imager/imager.toml
```

Exactly one argument is accepted. Startup order: parse config, load and check the JWT secret, open the image databases and user database, register routes, set up TLS, start the signal watcher, then listen. On success it prints to stderr:

```
imagerd: serving HTTP on 127.0.0.1:8443
```

(`HTTPS` when `tls = true`.) Check it:

```bash
curl http://127.0.0.1:8443/health
# {"status":"ok"}
```

**Signals.** `SIGINT` and `SIGTERM` are blocked in every thread and consumed by one watcher thread via `sigwait()`, so no code runs in signal-handler context. On a signal the daemon prints `imagerd: signal N, draining (up to 30s)` (the number is `[server].drain_timeout_s`) and calls Crow's `stop()`: the listener stops accepting and no further requests are read. A request whose handler has already started finishes (Crow writes the response, including streamed downloads, synchronously inside that handler, and `run()` joins its workers), so in-flight downloads, and uploads whose body has been fully received, complete. The wait is bounded by `[server].drain_timeout_s` (default 30; 1..3600); if a request is still running after that (for example a client that stopped reading a download), the daemon prints `imagerd: drain timeout, forcing exit` and exits `3`. Otherwise the facade is destroyed (closing the databases), it prints `imagerd: shutdown complete` and exits `0`. Idle keep-alive connections and requests not yet fully received are dropped (an upload whose body is still arriving at SIGTERM is cut off, the client gets no response). There is no reload on `SIGHUP`; restart for config changes. The login throttle counters are in memory and reset on restart.

**Exit codes:** `0` clean shutdown, `1` wrong argument count, `2` startup error (config, secret, TLS, database open, bind), `3` drain timeout on shutdown.

Example systemd unit:

```ini
[Unit]
Description=phto imagerd
After=network.target

[Service]
User=imager
ExecStart=/usr/local/bin/imagerd /etc/imager/imager.toml
Restart=on-failure
# Uploads are spooled to the temp dir; give it room (see Large uploads)
PrivateTmp=false

[Install]
WantedBy=multi-user.target
```

## Native TLS

Set in `[server]`:

```toml
tls      = true
tls_cert = "/etc/imager/server.crt"
tls_key  = "/etc/imager/server.key"
```

Requirements, all checked at startup (failure exits `2` with `imagerd: TLS error: ...`):

- Both paths set; both are regular files.
- `tls_key` is not readable by "other" (`chmod 600`).
- `tls_cert` is PEM; it may be a single certificate or a full chain with the leaf first.
- `tls_key` is PEM and matches the certificate.

Minimum protocol is TLS 1.2 (SSLv2/v3 disabled). Certificates are loaded once; renewal needs a restart. Self-signed certificate for testing:

```bash
openssl req -x509 -newkey rsa:3072 -nodes -days 365 -subj "/CN=imager.lan" \
  -keyout server.key -out server.crt
chmod 600 server.key
curl --cacert server.crt https://imager.lan:8443/health
```

## Reverse proxy

Alternative to native TLS, and the recommended setup if you need ACME certificates, HTTP/2, or request spooling. Run `imagerd` with `bind = "127.0.0.1"` and `tls = false` and let the proxy face the network. Plain HTTP on a non-loopback address exposes bearer tokens and passwords.

Caddy:

```
imager.example.lan {
    request_body {
        max_size 8GB
    }
    reverse_proxy 127.0.0.1:8443
}
```

nginx:

```nginx
server {
    listen 443 ssl;
    server_name imager.example.lan;
    ssl_certificate     /etc/ssl/imager.crt;
    ssl_certificate_key /etc/ssl/imager.key;

    client_max_body_size 8g;      # keep in line with max_upload_mb
    proxy_request_buffering off;  # stream the upload to imagerd, don't buffer twice
    proxy_read_timeout 600s;

    location / {
        proxy_pass http://127.0.0.1:8443;
        proxy_set_header Host $host;
    }
}
```

Caveats:

- **`X-Forwarded-For` is ignored unless you configure `trusted_proxies`.** By default `imagerd` uses the TCP peer address for the per-IP login throttle and the access log, so behind a proxy every client appears as `127.0.0.1`, and the per-IP limit applies to all users together: an attacker spraying wrong passwords can lock out the whole IP bucket. List your proxy to fix this:

  ```toml
  [server]
  trusted_proxies = ["127.0.0.1", "::1"]   # addresses or CIDRs, e.g. "10.0.0.0/8"
  ```

  If the TCP peer is in the list, `imagerd` walks `X-Forwarded-For` from right to left, skipping entries that are themselves trusted; the first untrusted entry is the client. A malformed or empty entry, or a chain made only of trusted addresses, falls back to the peer address. The header is never trusted from a peer that is not listed. Make sure the proxy overwrites or appends to `X-Forwarded-For` (nginx: `proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;`), and never list addresses that clients can reach directly.
- The proxy must pass `Range`, `If-None-Match` and `ETag` unchanged for resumable downloads and caching.
- The server does not read `X-Forwarded-Proto` or `Host`.

## Large uploads and memory

Stock Crow buffers the whole request body before any handler runs. `imagerd` builds Crow with a small patch (`third_party/crow/crow-body-sink.patch`, applied by CMake at configure time; a second patch, `crow-remote-ip.patch`, makes the client address available while the URL is parsed) that adds a header filter and a body sink, so the body is no longer buffered. Policy lives in `server/Upload.cpp`.

- **Early rejection.** The limit is checked when the headers arrive: `max_upload_mb` for authenticated `PUT /items`, 64 KiB for every other route. A larger `Content-Length` gets `413 TooLarge` with `Connection: close` without the body being read. Chunked bodies are cut off with `413` once they pass the limit.
- **Streaming spool.** An authenticated upload is written to `phto-upload-XXXXXX` in the system temp directory (`$TMPDIR`, else `/tmp`) in 64 KiB blocks, then ingested via `Imager::addFile`. The temp file is deleted on every path: success, error, client abort and forced exit. The temp directory needs free space of at least the largest concurrent uploads, and `/tmp` on tmpfs counts against RAM.
- **Memory is bounded per upload, independent of file size.** `Imager::addFile` loads files of 8 MB or less into memory in one piece; larger files use the streaming pipeline in chunks of at most 4 MB (plan 0017). File I/O per upload is therefore up to 8 MB (in-memory files), or for streamed files one 4 MB chunk in the hash pass and then a 4 MB buffer per `[[targets]]` root while writing (the roots are written in parallel; `imager/FileStorage.cpp:179`). With two targets that is about 12 MB at most. The validator's decoder memory comes on top of that while the file is validated, on both paths, and depends on the image or video, not on the file size:

  | Format | Estimated decoder memory per upload |
  |--------|-------------------------------------|
  | JPEG, baseline | under 1 MB |
  | JPEG, progressive | about 3-6 bytes per pixel (70-140 MB at 24 MP) |
  | PNG | under 1 MB (row by row) |
  | HEIC | about 4-6 bytes per pixel (50-70 MB at 12 MP, up to about 300 MB at 48 MP) |
  | NEF | raw sensor buffer, width x height x 2 bytes (about 12 MB at 6 MP, about 90 MB at 45 MP) |
  | MOV/MP4 | about 20-50 MB at 1080p H.264, 50-100 MB at 4K, more for HEVC or 10-bit |
  | AAE | negligible |

  These figures are estimates from reading the code, not measurements. The only measured run is 8 MOV files (718 MB total), which peaked at 75 MiB RSS. Per concurrent upload, budget about `12 MB + the format's decoder memory` (the 12 MB assumes two targets; for streamed files use `4 MB x number of targets` instead, and 8 MB for files that fit in memory). Peak server memory is that times the number of uploads running at once, which `threads` bounds, plus the base process. Two format limits follow from this: an `.aae` sidecar above 8 MB is rejected with `TooLarge` even if `[file_size_limits]` allows more, and a format without a stream validator is rejected with `UnsupportedFormat` above 8 MB. Rejected requests (413, 401) close the connection.
- Uploads still arriving at shutdown are cut off and their temp files removed.
- Downloads do not buffer the file: full responses are streamed in 16 KiB chunks. Range responses buffer up to 8 MiB each.

A reverse proxy limit (`client_max_body_size`, `request_body max_size`) is still worthwhile, but is no longer needed to protect RAM from a declared oversized body.

## Backups and multiple targets

Each `[[targets]]` entry is a full replica: files are written to every root and metadata to every target database, all-or-nothing with rollback. Putting targets on separate disks protects against a single disk failure. It is not a backup: deletes and corruption are replicated too.

- The user database (`[auth].database`) is separate and is not replicated. Back it up on its own; it is small.
- Back up each target root and database. For the SQLite files, stop `imagerd` or use `sqlite3 <db> ".backup <dest>"`; do not copy a live database file with `cp`.
- Keep the config file and JWT secret out of the media backups or encrypt them.
- If a target is lost, restore the files and database for that target from a good replica before starting `imagerd`; targets are expected to be identical. See [Storage and Data Model](storage.md).

## Logging

`imagerd` writes to stderr only; use systemd/journald or redirect. Contents:

- Startup line `imagerd: serving ... on <bind>:<port>`, error lines prefixed `imagerd:`, and `imagerd: shutdown complete`.
- One [access log](#access-log) line per request (unless `access_log = false`). Crow's own per-request INFO lines are suppressed.
- Handler error lines such as `GET /stats: count failed (database error)` or `PUT /items: cannot spool upload to temp file`. Handlers log the cause of every 500 here, because the response body is only `internal error`.
- Tokens, passwords and Authorization headers are not logged.

Request and pipeline metrics are not in the log; see [Monitoring](#monitoring).

## Monitoring

`imagerd` keeps request and pipeline metrics in memory (no persistence; counters reset on restart) and exposes them to admins in two forms:

- `GET /metrics`: Prometheus text format, for scraping. Metric names start with `phto_`; labels are `method`, `route` (a template such as `/items/{id}`, never the concrete path) and `status_class`.
- `GET /stats`: the same data as JSON, in the `server` and `library` sections that only admins receive.

Both are admin-only: the bearer token must belong to a user with `isAdmin` true, or the endpoint answers `403`. Everything else is covered in [`GET /metrics`](API.md#get-metrics) and [`GET /stats`](API.md#get-stats).

Tokens expire after `[auth].token_ttl_seconds` (12 h by default), so a scraper needs a token it can renew. Either set a long `token_ttl_seconds` (this applies to every user's tokens) or refresh the token file on a schedule. Create a dedicated admin user for it so it can be disabled independently; a disabled or demoted user's token stops working immediately.

```bash
# Obtain a token for a dedicated admin user and store it where Prometheus can read it
curl -s -X POST https://photos.example.org/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"login":"prometheus","password":"..."}'
# copy the token field from the response into /etc/prometheus/phto.token (mode 0600)
```

Example `prometheus.yml` scrape job:

```yaml
scrape_configs:
  - job_name: phto
    scheme: https
    metrics_path: /metrics
    authorization:
      type: Bearer
      credentials_file: /etc/prometheus/phto.token
    static_configs:
      - targets: ["photos.example.org:8443"]
```

Older Prometheus versions use `bearer_token_file: /etc/prometheus/phto.token` instead of the `authorization` block. For plain HTTP on loopback use `scheme: http`; do not send tokens over plain HTTP across a network.

Notes:

- Behind a reverse proxy, scrape the daemon directly or make sure the proxy forwards `/metrics` and the `Authorization` header; do not expose `/metrics` publicly.
- Each scrape is an authenticated request and shows up in the [access log](#access-log) and in `phto_http_requests_total` (`route="/metrics"`).
- `phto_process_resident_memory_bytes` and `..._peak_bytes` read `/proc/self/status` and are `0` on systems without it; use them with the figures in [Large uploads and memory](#large-uploads-and-memory).
- `phto_http_rejected_total{reason="unauthorized"}` and `phto_logins_total{result="failure"}` are the useful signals for alerting on credential guessing.

## Access log

With `[server].access_log = true` (the default), `imagerd` writes one line to stderr per request, after the response has been produced:

```
<YYYY-MM-DDTHH:MM:SSZ> <client-ip> <login|-> <METHOD> <path> <status> <bytes|-> <ms>ms
```

| Field | Meaning |
|-------|---------|
| time | UTC, ISO 8601 |
| client-ip | The [client IP](#reverse-proxy) (peer address, or the `X-Forwarded-For` client for a trusted proxy) |
| login | Authenticated login, or `-` for unauthenticated requests |
| path | Request path without the query string; spaces and non-printable bytes are written as `\xHH` |
| status | HTTP status code |
| bytes | Response bytes, or `-` if unknown |
| duration | Time spent handling the request, in milliseconds |

A line always has exactly eight space-separated fields, and lines from different threads never interleave. `Authorization` headers, cookies, tokens, passwords and request bodies are never logged. Set `access_log = false` to turn it off. The text format carries no request id; use the JSON format for that.

### JSON format

Set `access_log_format = "json"` to get one compact JSON object per line on stderr instead (default is `"text"`, unchanged):

```toml
[server]
access_log = true
access_log_format = "json"
```

An illustrative line (values are examples; the structure comes from `server/AccessLog.cpp`):

```json
{"ts":"2026-10-31T21:05:03.123Z","client_ip":"203.0.113.7","login":"alice","method":"GET","route":"/items/{id}","path":"/items/9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08","status":200,"bytes":48213,"duration_ms":12.345,"request_id":"3f2a9c1e7b4d4a0e8c55d1f06a7b9e21"}
```

| Field | Type | Meaning |
|-------|------|---------|
| `ts` | string | UTC, RFC 3339 with milliseconds |
| `client_ip` | string | The [client IP](#reverse-proxy), as in the text format |
| `login` | string or `null` | Authenticated login; `null` for unauthenticated requests |
| `method` | string | HTTP method |
| `route` | string | Route template, the same label as in the [metrics](#monitoring) (`/items/{id}`, ...), or `"other"` for unmatched paths |
| `path` | string | Request path without the query string |
| `status` | number | HTTP status code |
| `bytes` | number | Response bytes; always a number, `0` for an empty body (the text format prints `-` instead) |
| `duration_ms` | number | Handling time in milliseconds with microsecond resolution (three decimals). Requests rejected early show `0.000` |
| `request_id` | string | See [Request ids](#request-ids) |

Fields appear in this order. Strings escape `"` and `\`, write control bytes as `\u00XX`, replace invalid UTF-8 with U+FFFD, and are truncated with `…` after 2048 bytes, so a hostile path cannot break or forge a line. Headers, tokens, passwords, query strings and bodies are never logged. Each line is written with a single `write(2)` under a mutex, so lines do not interleave.

Example: show slow requests and count status codes with `jq` (use `fromjson?` to skip any non-JSON lines such as startup messages):

```bash
imagerd config.toml 2>&1 >/dev/null | jq -R 'fromjson? | select(.duration_ms > 500) | {ts, route, status, duration_ms, request_id}'
```

Any collector that parses JSON lines from stderr/journald works the same way; index `request_id` and `route` for lookups.

### Request ids

Every response carries an `X-Request-Id` header, including early rejects such as `413` and `401` on `PUT /items`. If the client (or your reverse proxy) sends `X-Request-Id` matching `^[A-Za-z0-9._-]{1,64}$`, it is used as is; otherwise `imagerd` generates a 32-character hex id. Have the proxy forward or set the header to correlate proxy logs with `imagerd` logs. The id appears only in the JSON access log.

**Unmatched routes.** Requests whose path matches no route are answered by Crow while it is still parsing the request line, so their log line has these limits: `client_ip` is the direct peer address (`X-Forwarded-For` is not parsed yet, so behind a trusted proxy it is the proxy's address); an inbound `X-Request-Id` is ignored and a generated id is used in both the log and the response header; `duration_ms` is `0.000`. Everything else is as for other requests.

The server writes no separate error log of its own; Crow's error lines on stderr are unchanged.

## Troubleshooting

Startup messages from `server/main.cpp` and `server/App.cpp`. Exit code is shown in parentheses.

| Message | Cause and fix |
|---------|---------------|
| `Usage: imagerd <config.toml>` (1) | Wrong number of arguments. |
| `imagerd: config error: ...` (2) | TOML parse failure or invalid value: no `[[targets]]`, duplicate root or database, `port` outside 1..65535, `max_upload_mb`/`token_ttl_seconds` <= 0, `pbkdf2_iterations` < 1000, `drain_timeout_s`, `lockout_seconds`, `max_login_failures` or `max_ip_failures` out of range, an invalid `trusted_proxies` entry, wrong value type. The message names the key. |
| `imagerd: cannot stat jwt_secret_file: <path>` (empty secret, exit 2) | The file does not exist or its directory is not traversable. |
| `imagerd: jwt_secret_file must be a regular file not readable by others (chmod 600): <path>` | Mode has the world-read bit, or it is a directory/device. `chmod 600`. |
| `imagerd: cannot open jwt_secret_file: <path>` | The daemon user lacks read permission. |
| `imagerd: no JWT secret configured (set [auth].jwt_secret_file)` (2) | No secret, or an empty one, from `jwt_secret_file` or `jwt_secret`. |
| `imagerd: JWT secret must be >= 32 bytes (got N)` (2) | The secret is present but short. |
| `imagerd: no auth database configured (set [auth].database)` (2) | `[auth].database` is absent. (`database = ""` is rejected earlier as a config error.) |
| `imagerd: cannot open storage: ...` (2) | A `[[targets]]` root or database could not be opened. |
| `imagerd: TLS error: tls = true requires [server].tls_cert and [server].tls_key` (2) | One of the paths is unset. |
| `imagerd: TLS error: tls_cert is missing or not a regular file: <path>` / `tls_key is missing ...` | Wrong path, or a directory. |
| `imagerd: TLS error: tls_key must not be readable by others (chmod 600): <path>` | Fix the key's mode. |
| `imagerd: TLS error: cannot load tls_cert <path>: <openssl error>` | Not PEM, or a corrupt file. |
| `imagerd: TLS error: cannot load tls_key <path>: <openssl error>` | Not PEM, or an encrypted key (passphrases are not supported). |
| `imagerd: TLS error: tls_key does not match tls_cert: ...` | Key and certificate are from different pairs. |
| `imagerd: TLS error: cannot set minimum TLS version: ...` | OpenSSL build problem. |
| `imagerd: cannot open auth database <path>: <reason>` (2) | `[auth].database` directory missing or not writable. |
| `imagerd: server error: ...` (2) (bind failure: address in use / permission denied) | Another process holds the port, or ports < 1024 need privileges. |

Runtime symptoms:

| Symptom | Cause |
|---------|-------|
| Every request returns `401 Unauthorized` after a restart | The secret or `issuer` changed, so old tokens fail. Log in again. |
| A user gets `401` mid-session | The account was deleted or disabled (tokens are re-checked on every request). |
| `429 TooManyRequests` on login or `POST /auth/password` | Throttle (shared counters); wait `Retry-After` (`[auth].lockout_seconds`, default 900 s) or restart the daemon. Behind a proxy see [Reverse proxy](#reverse-proxy). |
| `413 TooLarge` on small JSON | Body over 64 KiB. |
| `400 BrokenFile` on upload | The file failed validation; see [Supported Formats](formats.md). |
| `500 Internal` | Read the stderr log for the cause. |
