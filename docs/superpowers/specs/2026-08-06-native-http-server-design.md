# Native HTTP Server for Foundry Script

**Status:** Design approved 2026-08-06
**Depends on:** [cafecito-games/Foundry#1828](https://github.com/cafecito-games/Foundry/issues/1828) — *native-type namespaces* epic. The HTTP server's public classes are registered under the `foundry.http.server` namespace, which is not possible until #1828 lands.

## Motivation

Foundry Script has no way to *receive* HTTP requests today. The engine ships only HTTP client primitives (`HTTPClient`, `HTTPRequest`). Targeted use cases:

1. **Local dev tooling** — webhook receivers, editor/companion bridges, automation, OBS overlays (loopback).
2. **In-game embedded UI / dashboard** — a control surface served to the local machine or LAN.
3. **OAuth callback endpoint** — capture authorization-code redirects from a provider.

A pure-Foundry-Script server is impractical: correct HTTP/1.1 framing, request smuggling defenses, path-traversal protection, and resource limits are security-critical surfaces that belong in native code. This design adds a native module that owns those concerns and exposes a small, safe API to scripts.

## Approach

**Build on engine primitives + a tiny vendored parser** (chosen over vendoring a full server library like cpp-httplib).

The engine already ships every hard, security-critical part for free:

- `TCPServer::take_connection()` — TCP accept primitive.
- `StreamPeerTLS::accept_stream()` — server-side TLS, backed by the built-in mbedTLS module.
- `TLSOptions`, `CryptoKey`, `X509Certificate` — bound types for cert/key loading.

A full server library (cpp-httplib) was rejected because its `SSLServer` requires **OpenSSL**, which conflicts with the engine's mbedTLS — vendoring OpenSSL only for the HTTP server would mean a second, redundant TLS stack, and cpp-httplib's mbedTLS support is unofficial. The only correctness-sensitive part not already provided by the engine is HTTP/1.1 framing, which is covered by a small vendored parser (`picohttpparser`) rather than hand-rolled parsing.

Concretely, the engine owns: the accept loop, feeding bytes to the parser, the response writer, and static-file/range/conditional logic. The parser owns: turning a byte buffer into method/path/headers. TLS and sockets come from existing engine types.

## Module layout

A new `modules/http_server/`, following the `enet`/`mbedtls` module conventions:

```
modules/http_server/
  config.py             # can_build always true; module_add_dependencies(self, ["mbedtls"], True)
  SCsub                 # builds vendored picohttpparser (env_thirdparty, disable_warnings) + module sources
  register_types.h/.cpp # initialize_http_server_module @ MODULE_INITIALIZATION_LEVEL_CORE;
                        # FOUNDRY_REGISTER_CLASS for each public class
  doc_classes/*.xml      # class reference
  http_server.h/.cpp            # HTTPServer : Node
  http_inbound_request.h/.cpp   # HTTPInboundRequest : RefCounted  (read-only request view)
  http_response.h/.cpp          # HTTPResponse : RefCounted        (script writes the reply)
  http_server_connection.h/.cpp # HTTPServerConnection (internal C++ struct, not script-exposed)

thirdparty/picohttpparser/
  picohttpparser.{c,h}  # vendored (MIT), built with disable_warnings
  LICENSE
```

TLS comes from the `mbedtls` module dependency (built into every binary by default). No OpenSSL is introduced.

### Public classes and their namespacing

The three public classes are registered **only** under `foundry.http.server` (no flat global alias), keeping the global namespace clean and establishing the `foundry.*` type tree as the home for new engine types. This is blocked on #1828.

Until #1828 ships, the C++ module is built and tested with a temporary flat-global registration (`HTTPServer`, `HTTPInboundRequest`, `HTTPResponse`); the move to `foundry.http.server` is a one-line registration change once native-type namespaces exist.

| Class | Base | Role |
|---|---|---|
| `foundry.http.server.HTTPServer` | `Node` | Listens, dispatches, emits signals, holds routes and file mounts. |
| `foundry.http.server.HTTPInboundRequest` | `RefCounted` | Read-only view of a parsed request. |
| `foundry.http.server.HTTPResponse` | `RefCounted` | Script writes the reply; valid only during handling. |

## API surface

```python
# All types live in foundry.http.server.

var server := HTTPServer.new()
add_child(server)

# Plain HTTP (default bind is loopback)
server.listen(8080, "127.0.0.1")

# HTTPS — reuse the engine's bound TLS types
server.listen(8443, "127.0.0.1", TLSOptions.server(cert, key))

# Static file serving: mount a directory at a URL prefix.
# Ranges, ETag, and conditional GETs are handled internally.
server.mount_files("/app", "res://web")
server.mount_files("/", "user://export")

# Optional routing: method + path pattern -> Callable.
server.route("GET",  "/callback", _on_callback)
server.route("POST", "/api/echo",  _on_echo)

# Low-level path: handle every request yourself.
server.request_received.connect(_on_any_request)

server.use_threads = true   # optional off-main accept+parse loop
server.stop()
```

### Dispatch resolution order

For each request, in order, the first match wins:

1. **Static-file mount** — prefix match serves the file from disk.
2. **Registered route** — method + **exact path** match (the request's `get_path()`, query string excluded) invokes the Callable with `(req, res)`. v1 uses exact string match only; path-parameter patterns (`/users/:id`) and wildcards are deferred to post-v1 iteration. Path parameters are not needed for the target use cases — OAuth captures via `get_query()`, not path segments.
3. **`request_received` signal** — catch-all, fires with `(req, res)`.
4. **Default `404`** — nothing matched.

The signal is **opt-in as a catch-all**: by default (`emit_for_all = false`) it does not fire for requests already handled by a mount or route, so normal setups do not pay a signal dispatch per static-file hit. Set `emit_for_all = true` to observe every request.

### HTTPInboundRequest (read-only)

```python
req.get_method()      -> HTTPClient.Method      # reuses the existing enum
req.get_path()        -> String                 # "/callback" (no query string)
req.get_raw_path()    -> String                 # "/callback?code=xyz"
req.get_query()       -> Dictionary             # parsed query parameters
req.get_header(name)  -> String
req.get_headers()     -> Dictionary
req.get_body()        -> PackedByteArray
req.get_body_string() -> String
req.get_peer()        -> String                 # "127.0.0.1:54321"
```

### HTTPResponse

```python
res.set_status(200)
res.set_header("Content-Type", "application/json")
res.send(body: PackedByteArray)   # send and close
res.send_string(text)             # convenience overload
res.redirect(location, code := 302)  # the OAuth redirect
res.send_file(path)               # streamed from disk, ranges handled
```

### Synchronous-response contract (v1)

An `HTTPResponse` is valid only during the handling of its request. The script must complete the response **synchronously** within the handler/signal callback by calling one of `send` / `send_string` / `redirect` / `send_file`. Deferred/async responses are explicitly **out of scope for v1** and noted as a follow-up.

## Connection lifecycle and data flow

Each open connection is an internal `HTTPServerConnection` state machine (not exposed to scripts):

```
LISTEN --accept--> READING --parsed--> DISPATCH --handled--> WRITING --> CLOSE
                       ^                   (script fills HTTPResponse            |
                       └--partial--         synchronously in handler)            |
                  picohttpparser                                                    |
                  fed incremental                                                  (keep-alive loops
                                                                                   back to READING)
```

### Frame-poll mode (default, `use_threads = false`)

Mirrors `HTTPRequest` (`scene/main/http_request.cpp`):

1. Each `NOTIFICATION_INTERNAL_PROCESS`, `HTTPServer` calls `TCPServer::is_connection_available()`; non-blocking `take_connection()` yields a new `StreamPeerTCP`, wrapped in `StreamPeerTLS` when HTTPS is enabled.
2. For each connection, available bytes are read into a per-connection buffer (non-blocking; partial reads are fine) and fed to picohttpparser. If headers are not yet complete, the connection stays in `READING` until the next frame.
3. Once headers parse, the body (`Content-Length` or chunked) is read until complete, subject to the size caps below. An `HTTPInboundRequest` and a fresh `HTTPResponse` are constructed.
4. **Dispatch synchronously on the main thread**: resolve mount → route → signal → 404, invoke the matched Callable/signal with `(req, res)`. The handler completes the response.
5. Serialize the response (status line + headers + body, or streamed `send_file`). Honor `Connection: keep-alive` by looping back to `READING`; otherwise close.

### Threaded mode (`use_threads = true`)

The accept + read + parse loop runs on a `WorkerThreadPool` task. When a request is fully parsed, the connection is pushed onto a mutex-guarded `pending` queue and the worker **blocks only that connection**. The main thread drains `pending` during its internal-process notification, dispatches to script, fills the response, marks it done; the worker resumes to write and keep-alive-loop. This matches `HTTPRequest`'s threaded path: all script/VF calls stay on the main thread; workers only do socket I/O and parsing.

### Threading safety

Dispatch always happens on the main thread, so script callables and signal handlers run in the normal single-threaded VM context — no locks around script state, no re-entrancy. The cost is a minimum of one frame of latency in threaded mode (hand-off only happens at a frame boundary); acceptable for the target use cases.

## Error handling and security

Security is first-class. The server defaults to loopback and reuses mbedTLS, so the security boundary is request parsing and file serving.

### Request parsing

- **Hard limits, enforced before allocation:** `max_request_size` ~1 MiB (headers + body), `max_header_count` 100, `max_header_line_length` 8 KiB. Exceeding any limit returns `413 Payload Too Large` / `431 Request Header Fields Too Large` and closes the connection. No unbounded allocation.
- **Body caps:** bodies read up to `max_request_size` only; chunked bodies are dechunked with the same total cap. No streaming passthrough in v1.
- **Malformed input** (bad request line, invalid header characters, invalid chunk framing) → `400 Bad Request` + close. The parser operates on bounded buffers, so it cannot be made to allocate or loop without bound.
- **Request smuggling defense:** messages presenting both `Content-Length` and `Transfer-Encoding` are rejected; the connection is treated as non-reusable after any parse ambiguity (closed after the response).

### Static file serving

- **Path traversal (central security invariant):** every requested path is normalized and resolved *under* the mounted root via `DirAccess`/`FileAccess`, then verified by a prefix check on the resolved real path. `..`, encoded dots (`%2e`/`%2e%2e`), symlinks escaping the root, and absolute paths are rejected with `403`. Dedicated tests cover every vector.
- **No directory listing in v1** (directories return `404`). An `index_files` option is a possible later addition.
- **Range / conditional:** `Range` clamped to file bounds (`416` on unsatisfiable); `If-Modified-Since` / `If-None-Match` → `304`. Malformed ranges are ignored and served as `200`.
- **Content-Type** by extension via a built-in MIME map; client-supplied types are never trusted.

### TLS

TLS options come from the existing `TLSOptions.server(cert, key)`; the module does not handle certificates or keys itself. mbedTLS default ciphersuites; no client authentication by default.

### Resource exhaustion

- `max_connections` cap (default ~64). New accepts beyond the cap are immediately closed (or not accepted until a slot frees); pending connections never grow unbounded.
- Per-connection timeouts (request, keep-alive, TLS handshake) enforced via each connection's last-activity timestamp, checked each poll; stalled connections are dropped.

### Operational

- Binding defaults to `127.0.0.1` (loopback). Binding `0.0.0.0` is permitted but documented as exposing the server to the LAN/internet.
- Non-fatal errors (parse failures, dropped connections, TLS handshake errors) emit a single engine-log warning per event and never crash the host. A `server_error` signal gives scripts observability. Fatal `listen()` failures (e.g. port in use) return an `Error` from `listen()`.

Additional tuning knobs (limits, timeouts, `max_connections`, directory listing, etc.) are deferred to iteration after v1.

## Testing

Tests assert on observable behavior (responses, side-effects) — never on `HTTPServerConnection` internals or source text — and write scratch files under `FOUNDRY_TEST_SCRATCH`.

### C++ doctests (`tests/test_http_server.h`, wired into `test_main.cpp`)

- Round-trip a request through an in-process server on an ephemeral loopback port, using the engine's own `HTTPClient` as the test client; assert status, headers, body, query parsing.
- Every dispatch path: mount hit; mount miss → route; route miss → signal; full miss → 404.
- `HTTPResponse::redirect` status + `Location`.
- `send_file` with `Range` (including `416`), `If-Modified-Since` / `If-None-Match` → `304`.
- **Security boundary tests (required):** path-traversal vectors (`../`, `%2e%2e`, absolute paths, symlink escape) all return `403` and never open a file outside the root; oversized body → `413`; too many headers → `431`; ambiguous `Content-Length` + `Transfer-Encoding` → `400` and connection closed; `max_connections` enforcement.
- HTTPS path: TLS handshake with a generated self-signed cert, GET over TLS.
- Both threading modes (frame-poll and `use_threads`) produce identical results.

### Foundry Script fixtures (`modules/foundry_script/tests/scripts/`)

- A fixture that starts an `HTTPServer`, registers a route and a signal handler, serves a request via `HTTPClient`, and asserts the handler received the expected `HTTPInboundRequest` fields. (Guarded on #1828; uses the flat-global spelling until native-type namespaces land.)
- An OAuth-callback fixture: register `/callback`, simulate the provider redirect, assert the captured `code` query param and the `302` the handler emits.

## Out of scope for v1

- Deferred / async responses (handler must complete the response synchronously).
- Streaming request bodies and chunked/streaming *response* bodies (e.g. Server-Sent Events).
- WebSocket upgrade (a websocket module already exists separately).
- Directory listing and `index_files`.
- Additional tuning knobs and limits beyond the defaults above.
- Path-parameter / wildcard route patterns (v1 routes use exact path match only).

## Implementation prerequisite

The module is fully buildable and testable today against flat-global registration. The single change required once [cafecito-games/Foundry#1828](https://github.com/cafecito-games/Foundry/issues/1828) lands is moving the three `FOUNDRY_REGISTER_CLASS` registrations from flat globals into the `foundry.http.server` namespace. Until then, Foundry Script fixtures use the flat-global spelling.
