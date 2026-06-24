# Testing Dashboard

Summary of all test coverage docs for the imager project.

Cross-cutting test structure, build/run commands, and conventions live in
[CONVENTIONS.md](CONVENTIONS.md).

| Feature | Doc | Status | Coverage |
|---------|-----|--------|----------|
| Phase B multi-target integration (B6, B7, B8, B10) | [phase-b-multi-target.md](phase-b-multi-target.md) | complete | 5/5 (100%) |
| Metrics primitives (task #10, B4, B9) | [metrics-primitives.md](metrics-primitives.md) | complete | 9/9 (100%) |
| imagestore CLI (0006.UTILITY) | [imagestore-cli.md](imagestore-cli.md) | in-progress | 8/9 (89%) |
| Verbose/Normal output redesign (0016.VERBOSE_OUTPUT) | [verbose-output.md](verbose-output.md) | complete | 22/22 (100%) |
| Server config `[server]`/`[auth]` parsing (0022.SERVER §11, A2) | [server-config.md](server-config.md) | complete | 10/10 (100%) |
| Auth library: PasswordHash + UserStore + TokenService (0022.SERVER §5.1–5.3, B1–B3) | [auth-library.md](auth-library.md) | complete | 21/21 (100%) |
| imageradmin admin CLI (0022.SERVER §5.4, C1) | [imageradmin-cli.md](imageradmin-cli.md) | complete | 9/9 (100%) |
| Untagged items query — DB + facade (0022.SERVER §8 item 1, D1) | [untagged-query.md](untagged-query.md) | complete | 5/5 (100%) |
| Atomic tag replacement — DB + facade + multi-target (0022.SERVER §8 item 2, D2) | [tag-replace.md](tag-replace.md) | complete | 7/7 (100%) |
| Image path accessor — `getImagePath` + `resolveStoredPath` (0022.SERVER §8 item 3, D3) | [image-path.md](image-path.md) | complete | 5/5 (100%) |
| imagerd daemon — health endpoint + graceful shutdown (0022.SERVER §6.6/§12, E1) | [server-daemon.md](server-daemon.md) | in-progress | 3/5 (60%) |
| Server error utilities — `server/Json.{h,cpp}` (0022.SERVER §7, E2) | [server-errors.md](server-errors.md) | complete | 7/7 (100%) |
| Auth endpoints — `POST /auth/login`, `GET /auth/me` (0022.SERVER §6.1, F1) | [auth-endpoints.md](auth-endpoints.md) | complete | 7/7 (100%) |
| Auth authz, throttle, password change (0022.SERVER §6.1/§6.5/§13, F2) | [auth-authz.md](auth-authz.md) | complete | 11/11 (100%) |
