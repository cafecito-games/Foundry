# Native HTTP Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native `modules/http_server/` module exposing a loopback-default HTTP/HTTPS server to Foundry Script, built on `TCPServer` + `StreamPeerTLS` (mbedTLS) with a vendored `picohttpparser`.

**Architecture:** A new optional engine module owns accept/parsing/dispatch/file-serving. It reuses existing engine socket and TLS types rather than vendoring a full server library (avoids an OpenSSL/mbedTLS conflict). Public classes `HTTPServer`, `HTTPInboundRequest`, `HTTPResponse` are registered as flat globals now and moved under the `foundry.http.server` namespace once epic #1828 (native-type namespaces) lands.

**Tech Stack:** C++ (engine core/module conventions), SCons build (`config.py`/`SCsub`/`register_types`), picohttpparser (vendored, MIT), mbedTLS (existing module), doctest C++ tests, Foundry Script fixtures.

**Spec:** `docs/superpowers/specs/2026-08-06-native-http-server-design.md`
**Blocking dependency:** [cafecito-games/Foundry#1828](https://github.com/cafecito-games/Foundry/issues/1828) — required only for the namespace move (Task 11), not for the module build.

**Build/test commands** (from repo root, see CLAUDE.md):
- Build: `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4` (default is `dev_mode=yes dev_build=yes tests=yes`, the required pre-PR validation build)
- Focused tests: `./bin/foundry.<platform> --headless test run --case "*HTTPServer*" --force-colors`
- Full suite: `./bin/foundry.<platform> --headless test run --force-colors`

**Engine signatures referenced by this plan** (verify at the file before use):
- `TCPServer::listen(uint16_t p_port, const IPAddress &p_bind_address = IPAddress("*"), bool p_reuse_address = true)` — `core/io/tcp_server.h:44`
- `Ref<StreamPeerTCP> TCPServer::take_connection()` — `core/io/tcp_server.h:46`
- `HTTPClient::Method` / `HTTPClient::ResponseCode` enums — `core/io/http_client.h:43-115`
- `StreamPeerTLS::accept_stream(Ref<StreamPeer> p_base, Ref<TLSOptions> p_options)`, `StreamPeerTLS::create()`, `poll()`, `get_status()` — `core/io/stream_peer_tls.h:52-62`
- `TLSOptions::server(Ref<CryptoKey> p_own_key, Ref<X509Certificate> p_own_certificate)` — `core/crypto/crypto.h:93`
- Module scaffolding pattern: copy `modules/enet/{config.py,SCsub,register_types.h,register_types.cpp}` verbatim, then adapt.
- Node frame-poll pattern: `scene/main/http_request.{h,cpp}` (`set_process_internal`, `NOTIFICATION_INTERNAL_PROCESS`).
- Test pattern: `tests/core/io/test_tcp_server.h` (namespace + `TEST_CASE` + `wait_for_condition` helper); registered by `#include` in `tests/test_main.cpp`.

---

## File Structure

**Created — vendored:**
- `thirdparty/picohttpparser/picohttpparser.{c,h}`, `LICENSE` — vendored parser (MIT).

**Created — module:**
- `modules/http_server/config.py` — `can_build` always true; declares the mbedtls dependency; doc classes.
- `modules/http_server/SCsub` — builds picohttpparser with `disable_warnings()` + module sources.
- `modules/http_server/register_types.{h,cpp}` — registers public classes at `MODULE_INITIALIZATION_LEVEL_SCENE`.
- `modules/http_server/http_server.{h,cpp}` — `HTTPServer : Node` (listen/stop/poll, signals, routing, mounts, connection list).
- `modules/http_server/http_inbound_request.{h,cpp}` — `HTTPInboundRequest : RefCounted` (read-only parsed request).
- `modules/http_server/http_response.{h,cpp}` — `HTTPResponse : RefCounted` (script writes reply).
- `modules/http_server/http_server_connection.{h,cpp}` — `HTTPServerConnection` (internal per-connection state machine; not script-exposed).
- `modules/http_server/http_file_serve.{h,cpp}` — static-file serving helpers (MIME map, range, conditional, path-traversal check).
- `modules/http_server/doc_classes/HTTPServer.xml`, `HTTPInboundRequest.xml`, `HTTPResponse.xml`.

**Created — tests:**
- `tests/core/io/test_http_server.h` — C++ doctests; included from `tests/test_main.cpp`.

**Created — Foundry Script fixtures:**
- `modules/foundry_script/tests/scripts/http_server/` — `.fs` fixtures + expected outputs.

**Modified:**
- `tests/test_main.cpp` — add `#include "tests/core/io/test_http_server.h"` in the io block (near line 120/138).

---

## Task 1: Vendor picohttpparser and scaffold the module

**Goal:** A new `modules/http_server/` module that builds and registers a stub `HTTPServer : Node` class (flat global), proving the build wiring before any logic.

**Files:**
- Create: `thirdparty/picohttpparser/picohttpparser.c`, `thirdparty/picohttpparser/picohttpparser.h`, `thirdparty/picohttpparser/LICENSE`
- Create: `modules/http_server/config.py`, `modules/http_server/SCsub`, `modules/http_server/register_types.h`, `modules/http_server/register_types.cpp`, `modules/http_server/http_server.h`, `modules/http_server/http_server.cpp`
- Test: build compiles; class visible at runtime

**Acceptance Criteria:**
- [ ] `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4` succeeds.
- [ ] `./bin/foundry.<platform> --headless test run --case "*HTTPServer*" --force-colors` runs (no tests yet) without error.
- [ ] `HTTPServer` appears in `ClassDB::get_class_list()` (verified by a doctest in Task 2; here, just confirm it builds and registers by adding a trivial TEST_CASE that prints the class).

**Verify:** `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 && ./bin/foundry.<platform> --headless test run --case "*HTTPServer*" --force-colors` → build OK, doctest suite runs.

**Steps:**

- [ ] **Step 1: Vendor picohttpparser.** Download `picohttpparser.{c,h}` (MIT, from the H2O project) into `thirdparty/picohttpparser/`. Copy its MIT `LICENSE` text into `thirdparty/picohttpparser/LICENSE`. Add `README.patched.md` only if patches are applied (none expected).

- [ ] **Step 2: Create `modules/http_server/config.py`** (adapted from `modules/enet/config.py`):

```python
def can_build(env, platform):
    return True


def configure(env):
    # HTTP server reuses the engine's built-in mbedTLS for HTTPS.
    env.module_add_dependencies(self, ["mbedtls"], True)


def get_doc_classes():
    return [
        "HTTPServer",
        "HTTPInboundRequest",
        "HTTPResponse",
    ]


def get_doc_path():
    return "doc_classes"
```

Note: `module_add_dependencies` is called with `env` by the modules framework; mirror exactly how an existing module that depends on another module calls it (check `modules/foundry_script/config.py` for the `self`/`env` form used in this repo and match it).

- [ ] **Step 3: Create `modules/http_server/SCsub`** (adapted from `modules/enet/SCsub`):

```python
#!/usr/bin/env python
from misc.utility.scons_hints import *

Import("env")
Import("env_modules")

env_http_server = env_modules.Clone()

# Vendored picohttpparser (HTTP/1.1 framing parser).
thirdparty_obj = []

thirdparty_dir = "#thirdparty/picohttpparser/"
thirdparty_sources = [
    "picohttpparser.c",
]
thirdparty_sources = [thirdparty_dir + file for file in thirdparty_sources]

env_http_server.Prepend(CPPPATH=[thirdparty_dir])

env_thirdparty = env_http_server.Clone()
env_thirdparty.disable_warnings()
env_thirdparty.add_source_files(thirdparty_obj, thirdparty_sources)
env.modules_sources += thirdparty_obj

# Module sources.
module_obj = []
env_http_server.add_source_files(module_obj, "*.cpp")
env.modules_sources += module_obj

env.Depends(module_obj, thirdparty_obj)
```

- [ ] **Step 4: Create `modules/http_server/register_types.h`** (copy the license header from `modules/enet/register_types.h`, then):

```cpp
#pragma once

#include "modules/register_module_types.h"

void initialize_http_server_module(ModuleInitializationLevel p_level);
void uninitialize_http_server_module(ModuleInitializationLevel p_level);
```

- [ ] **Step 5: Create `modules/http_server/http_server.h`** (stub for this task; the full API is built up across later tasks):

```cpp
// License header (copy from modules/enet/register_types.h).
#pragma once

#include "scene/main/node.h"

class HTTPServer : public Node {
	FOUNDRY_CLASS(HTTPServer, Node);

protected:
	static void _bind_methods();

public:
	HTTPServer();
	~HTTPServer();
};
```

- [ ] **Step 6: Create `modules/http_server/http_server.cpp`** (stub):

```cpp
// License header.
#include "http_server.h"

void HTTPServer::_bind_methods() {
}

HTTPServer::HTTPServer() {
}

HTTPServer::~HTTPServer() {
}
```

- [ ] **Step 7: Create `modules/http_server/register_types.cpp`** (adapted from `modules/enet/register_types.cpp`):

```cpp
// License header.
#include "register_types.h"

#include "http_server.h"

void initialize_http_server_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	FOUNDRY_REGISTER_CLASS(HTTPServer);
}

void uninitialize_http_server_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
```

`HTTPInboundRequest` / `HTTPResponse` are added to this file in Tasks 2–3.

- [ ] **Step 8: Build and confirm registration.** Run the build. If it fails on the `config.py` `self`/`env` form, match whatever `modules/foundry_script/config.py` uses for `module_add_dependencies`.

Run: `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4`
Expected: build succeeds; `modules/modules_enabled.gen.h` now lists `http_server`.

- [ ] **Step 9: Commit.**

```bash
git add thirdparty/picohttpparser modules/http_server
git commit -m "feat(http_server): scaffold module and vendor picohttpparser"
```

---

## Task 2: HTTPInboundRequest and HTTPResponse data objects

**Goal:** The two `RefCounted` data objects with their full bound APIs, validated by unit tests that construct them in C++ and read values back. No networking yet.

**Files:**
- Create: `modules/http_server/http_inbound_request.{h,cpp}`, `modules/http_server/http_response.{h,cpp}`
- Modify: `modules/http_server/register_types.cpp` (register both classes)
- Test: `tests/core/io/test_http_server.h` (new file), `tests/test_main.cpp` (include it)

**Acceptance Criteria:**
- [ ] `HTTPInboundRequest` exposes: `get_method() -> int` (HTTPClient::Method), `get_path()`, `get_raw_path()`, `get_query() -> Dictionary`, `get_header(String)`, `get_headers() -> Dictionary`, `get_body() -> PackedByteArray`, `get_body_string()`, `get_peer()`.
- [ ] `HTTPResponse` exposes: `set_status(int)`, `set_header(String,String)`, `send(PackedByteArray)`, `send_string(String)`, `redirect(String, int)`, `send_file(String)`. State is queryable from C++ (status, header map, body bytes, redirect target, file path, "completed" flag) so tests and the connection writer can read it.
- [ ] All three classes appear in `ClassDB`.
- [ ] Unit tests pass: setting/getting round-trips; `redirect` sets status 302 + `Location`; `send` marks the response completed.

**Verify:** `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 && ./bin/foundry.<platform> --headless test run --case "*HTTPServer*" --force-colors` → PASS.

**Steps:**

- [ ] **Step 1: Write the failing test** in `tests/core/io/test_http_server.h`. Copy the license header from `tests/core/io/test_tcp_server.h`. Then:

```cpp
#pragma once

#include "modules/http_server/http_inbound_request.h"
#include "modules/http_server/http_response.h"

#include "tests/test_macros.h"

namespace TestHTTPServer {

TEST_CASE("[HTTPServer] Response send and redirect set state") {
	HTTPResponse response;
	CHECK(response.is_completed() == false);

	response.set_status(200);
	CHECK(response.get_status() == 200);

	response.set_header("Content-Type", "text/plain");
	CHECK(String(response.get_headers()["Content-Type"]) == "text/plain");

	response.send_string("hello");
	CHECK(response.is_completed() == true);
	CHECK(String(response.get_body()) == "hello");
}

TEST_CASE("[HTTPServer] Redirect sets status and Location") {
	HTTPResponse response;
	response.redirect("https://example.com/cb?code=abc");
	CHECK(response.get_status() == 302);
	CHECK(response.get_headers()["Location"] == "https://example.com/cb");
	// Query is preserved on the Location the caller passes; redirect() must not strip it.
}

TEST_CASE("[HTTPServer] Inbound request getters") {
	HTTPInboundRequest request;
	request._set_internal(HTTPClient::METHOD_GET, "/callback", "/callback?code=xyz",
			Dictionary(), PackedByteArray(), "127.0.0.1:54321");
	CHECK(request.get_method() == (int)HTTPClient::METHOD_GET);
	CHECK(request.get_path() == "/callback");
	CHECK(request.get_raw_path() == "/callback?code=xyz");
	CHECK(String(request.get_query()["code"]) == "xyz");
	CHECK(request.get_peer() == "127.0.0.1:54321");
}

} // namespace TestHTTPServer
```

`_set_internal` is a C++-only setter (not bound) used by the connection state machine to populate the request; tests and the connection code share it.

- [ ] **Step 2: Register the test header** in `tests/test_main.cpp` by adding this line next to the other io test includes (around line 120/138):

```cpp
#include "tests/core/io/test_http_server.h"
```

- [ ] **Step 3: Run the test to verify it fails.**
Run: `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 && ./bin/foundry.<platform> --headless test run --case "*HTTPServer*" --force-colors`
Expected: FAIL — types/methods undefined.

- [ ] **Step 4: Implement `modules/http_server/http_inbound_request.h`:**

```cpp
// License header.
#pragma once

#include "core/io/http_client.h" // HTTPClient::Method
#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"

class HTTPInboundRequest : public RefCounted {
	FOUNDRY_CLASS(HTTPInboundRequest, RefCounted);

	HTTPClient::Method method = HTTPClient::METHOD_GET;
	String path;
	String raw_path;
	Dictionary headers;
	PackedByteArray body;
	String peer;

protected:
	static void _bind_methods();

public:
	// C++-only populator used by HTTPServerConnection.
	void _set_internal(HTTPClient::Method p_method, const String &p_path, const String &p_raw_path,
			const Dictionary &p_headers, const PackedByteArray &p_body, const String &p_peer);

	int get_method() const;
	String get_path() const;
	String get_raw_path() const;
	Dictionary get_query() const;
	String get_header(const String &p_name) const;
	Dictionary get_headers() const;
	PackedByteArray get_body() const;
	String get_body_string() const;
	String get_peer() const;
};
```

- [ ] **Step 5: Implement `http_inbound_request.cpp`.** `_bind_methods` binds the seven getters. `get_query()` parses `raw_path`'s `?`-suffix into a `Dictionary` via `http_client.h`'s query-encoding helpers (use `HTTPClient::query_from_dict` / `decode_percent`-style utilities already in the engine; if a plain `String::percent_decode` path is simpler, split on `&`/`=`, percent-decode each part). `get_body_string()` returns `String::from_utf8(body)`.

- [ ] **Step 6: Implement `modules/http_server/http_response.h` / `.cpp`:**

```cpp
// License header (header).
#pragma once

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"

class HTTPResponse : public RefCounted {
	FOUNDRY_CLASS(HTTPResponse, RefCounted);

	int status = 200;
	Dictionary headers;
	PackedByteArray body;
	bool completed = false;
	bool is_file = false;
	String file_path;
	bool is_redirect = false;

protected:
	static void _bind_methods();

public:
	void set_status(int p_status);
	int get_status() const { return status; }
	void set_header(const String &p_name, const String &p_value);
	Dictionary get_headers() const { return headers; }

	void send(const PackedByteArray &p_body);
	void send_string(const String &p_text);
	void redirect(const String &p_location, int p_code = 302);
	void send_file(const String &p_path);

	// C++-only readback for the connection writer.
	bool is_completed() const { return completed; }
	PackedByteArray get_body() const { return body; }
	bool get_is_file() const { return is_file; }
	String get_file_path() const { return file_path; }
	bool get_is_redirect() const { return is_redirect; }
};
```

`send` / `send_string` / `redirect` / `send_file` each set their fields and flip `completed = true` (and `is_file` / `is_redirect` accordingly). Binding binds the six public methods plus `get_status`/`get_headers` for introspection.

- [ ] **Step 7: Register both classes** in `modules/http_server/register_types.cpp`:

```cpp
#include "http_inbound_request.h"
#include "http_response.h"
// ...inside initialize_http_server_module, after HTTPServer:
FOUNDRY_REGISTER_CLASS(HTTPInboundRequest);
FOUNDRY_REGISTER_CLASS(HTTPResponse);
```

- [ ] **Step 8: Run tests to verify they pass.**
Run: `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 && ./bin/foundry.<platform> --headless test run --case "*HTTPServer*" --force-colors`
Expected: PASS (3 cases).

- [ ] **Step 9: Commit.**

```bash
git add modules/http_server/http_inbound_request.h modules/http_server/http_inbound_request.cpp \
        modules/http_server/http_response.h modules/http_server/http_response.cpp \
        modules/http_server/register_types.cpp tests/core/io/test_http_server.h tests/test_main.cpp
git commit -m "feat(http_server): add HTTPInboundRequest and HTTPResponse data objects"
```

---

## Task 3: Plain-HTTP listen, accept, parse, and one round-trip

**Goal:** `HTTPServer::listen(port, bind_address)` starts a `TCPServer`; the frame-poll loop accepts connections, parses requests with picohttpparser, dispatches the first registered route, and writes a `200` body. One end-to-end GET round-trip via the engine's `HTTPClient` as the test client.

**Files:**
- Create: `modules/http_server/http_server_connection.{h,cpp}`
- Modify: `modules/http_server/http_server.{h,cpp}`
- Test: `tests/core/io/test_http_server.h`

**Acceptance Criteria:**
- [ ] `HTTPServer` is a `Node`; calling `listen(port, "127.0.0.1")` from a test and polling the server drives the loop.
- [ ] A registered route (`server->route("GET", "/hello", callable)`) is invoked with a populated `HTTPInboundRequest`; the callable fills `HTTPResponse`, and the bytes the client reads back decode to a `200` with the expected body.
- [ ] `stop()` closes the listener and all connections.
- [ ] Test client uses an ephemeral port and the `wait_for_condition` helper from `tests/core/io/test_tcp_server.h`.

**Verify:** focused test run → the round-trip test PASSES.

**Steps:**

- [ ] **Step 1: Write the failing test.** Append to `tests/core/io/test_http_server.h`:

```cpp
#include "modules/http_server/http_server.h"

#include "core/io/http_client_tcp.h"

namespace TestHTTPServer {

// Returns the response body string from a one-shot HTTPClient GET, or "" on failure.
String http_get(int p_port, const String &p_path) {
	// Build an HTTPClient, connect to 127.0.0.1:p_port, send GET p_path,
	// poll until RESPONSE_BODY, read the body. Mirror tests/core/io/test_http_client.h.
	// ...use the established wait_for_condition pattern...
}

TEST_CASE("[HTTPServer] GET round-trip through a registered route") {
	HTTPServer server;
	REQUIRE(server.listen(next_free_port(), "127.0.0.1") == OK);
	int port = server.get_listening_port();

	server.route("GET", "/hello", [](HTTPInboundRequest *req, HTTPResponse *res) {
		res->set_header("Content-Type", "text/plain");
		res->send_string("hi");
	});

	// Drive the server's frame poll while the client performs the GET.
	String body = http_get(port, "/hello");
	CHECK(body == "hi");
	server.stop();
}

} // namespace TestHTTPServer
```

`next_free_port()` is a small test helper: pick a high port (e.g. start at 38000) and retry on `ERR_BUSY`, mirroring `tests/core/io/test_tcp_server.h`. (`TCPServer::get_local_port()` at `core/io/tcp_server.h:45` returns the bound port; the server exposes it to tests as `get_listening_port()`.)

- [ ] **Step 2: Run to verify it fails.** Expected: FAIL — `listen`/`route` undefined.

- [ ] **Step 3: Implement `HTTPServerConnection`** (`http_server_connection.{h,cpp}`): holds `Ref<StreamPeer> stream`, a `Vector<uint8_t> read_buffer`, parser state (`phr_header` array, `method`/`path`/`minor_version` out-params, `prev_buf_len`), a `State { READING, DISPATCH, WRITING, DONE }`, and `last_activity_usec`. Method `PollResult poll()` non-blockingly reads available bytes, feeds `phr_parse_request`, returns `{PARSING, READY, ERROR}`. When READY, exposes the parsed method/path/headers/body to the server. The connection owns no script dispatch — the server drives it.

- [ ] **Step 4: Implement `HTTPServer` listen/poll.** In `http_server.h` add: `Error listen(int p_port, const String &p_bind = "127.0.0.1")`, `int get_listening_port() const`, `void stop()`, `void route(const String &p_method, const String &p_path, const Callable &p_callable)`, an internal `_notification(int p_what)` handling `NOTIFICATION_INTERNAL_PROCESS` (call `set_process_internal(true)` in `listen`), a `TCPServer tcp_server`, and `LocalVector<HTTPServerConnection> connections`. The poll: `if (tcp_server->is_connection_available())` push a new connection from `take_connection()`; for each connection, `poll()`; on READY, build an `HTTPInboundRequest` via `_set_internal`, create an `HTTPResponse`, match the route by method+exact path, invoke the `Callable` with `(req, res)` via `Callable::callp` (or `callv`), then write the response bytes to the stream and close (keep-alive added in Task 7).

- [ ] **Step 5: Run to verify it passes.** Expected: round-trip PASS.

- [ ] **Step 6: Commit.**

```bash
git add modules/http_server/http_server.h modules/http_server/http_server.cpp \
        modules/http_server/http_server_connection.h modules/http_server/http_server_connection.cpp \
        tests/core/io/test_http_server.h
git commit -m "feat(http_server): listen, accept, parse, and route a GET round-trip"
```

---

## Task 4: Dispatch resolution order and the request_received signal

**Goal:** Full dispatch order — static-file mount → route → `request_received` signal → 404 — plus the `emit_for_all` property (default false) and the signal signature.

**Files:**
- Modify: `modules/http_server/http_server.{h,cpp}`
- Test: `tests/core/io/test_http_server.h`

**Acceptance Criteria:**
- [ ] `ADD_SIGNAL(MethodInfo("request_received", PropertyInfo(Variant::OBJECT, "request"), PropertyInfo(Variant::OBJECT, "response")))` is declared in `_bind_methods`.
- [ ] A request matching a route does NOT fire the signal when `emit_for_all == false`; it DOES when `emit_for_all == true`.
- [ ] A request matching no mount/route fires the signal (catch-all).
- [ ] A request matching nothing and no signal connection returns `404`.
- [ ] Tests cover all four branches.

**Verify:** focused test run → PASS.

**Steps:**

- [ ] **Step 1: Write failing tests** for the four branches (route-hit-no-signal; route-hit-signal-with-emit_for_all; miss→signal; miss→404). Each test registers an expectation (e.g. a counter `Callable`) and asserts both the response the client sees and whether the signal/side-effect fired.

- [ ] **Step 2: Run to verify failure.**

- [ ] **Step 3: Implement** the resolution ladder in the dispatch path: (a) check file mounts (Task 6 wires the actual serving; here a mount hit still delegates to `HTTPFileServe` if present, else skip), (b) exact route match, (c) if `emit_for_all` or nothing matched, emit `request_received`, (d) if `HTTPResponse` still not completed, write a default `404 Not Found`. Add `bool emit_for_all = false;` and `GDVIRTUAL`/`ADD_PROPERTY` for it.

- [ ] **Step 4: Run to verify pass.**

- [ ] **Step 5: Commit.**

```bash
git add modules/http_server/http_server.h modules/http_server/http_server.cpp tests/core/io/test_http_server.h
git commit -m "feat(http_server): dispatch resolution order and request_received signal"
```

---

## Task 5: Query, body, and headers; send_string and redirect

**Goal:** Confirm the full `HTTPInboundRequest`/`HTTPResponse` surface works over the wire — query parsing, header reading, body reading (POST), `send_string`, and `redirect` (the OAuth callback primitive).

**Files:**
- Modify: `modules/http_server/http_server.{h,cpp}` (writer must emit status line + headers + body correctly; redirect writes `Location`)
- Test: `tests/core/io/test_http_server.h`

**Acceptance Criteria:**
- [ ] A `GET /cb?code=abc&state=xyz` route handler reads `req->get_query()["code"] == "abc"` and emits `res->redirect("https://app/dest")`; the client sees status `302` + `Location: https://app/dest`.
- [ ] A `POST /echo` with a body is delivered via `req->get_body()`/`get_body_string()`; handler echoes it; client round-trips the bytes.
- [ ] A custom request header is readable via `req->get_header("X-Token")`.

**Verify:** focused test run → PASS.

**Steps:**

- [ ] **Step 1: Write failing tests** (OAuth-callback redirect + POST echo + header read) using the `http_get`/`http_post` helpers.

- [ ] **Step 2: Run to verify failure.**

- [ ] **Step 3: Implement the response writer** that serializes status line (`HTTP/1.1 <code> <reason>`), the header map (always include `Content-Length` unless `send_file`/chunked), a blank line, then the body. For `redirect`, ensure `Location` is written and no body. Body reading: after picohttpparser returns the request line + headers, read `Content-Length` bytes (capped per Task 7) into the request before dispatch.

- [ ] **Step 4: Run to verify pass.**

- [ ] **Step 5: Commit.**

```bash
git add modules/http_server/http_server.cpp tests/core/io/test_http_server.h
git commit -m "feat(http_server): query/body/header passthrough, send_string, redirect"
```

---

## Task 6: Static file serving with path-traversal security

**Goal:** `mount_files(url_prefix, root_dir)` serves files from disk with correct MIME types, `Range`/`416`, and `If-Modified-Since`/`If-None-Match`→`304`, with path traversal blocked as the central security invariant.

**Files:**
- Create: `modules/http_server/http_file_serve.{h,cpp}`
- Modify: `modules/http_server/http_server.{h,cpp}` (mount table + mount hit in dispatch)
- Test: `tests/core/io/test_http_server.h`

**Acceptance Criteria:**
- [ ] `mount_files("/app", scratch_dir)` serves `<scratch>/index.html` at `GET /app/index.html` with `Content-Type: text/html`.
- [ ] **Path traversal vectors all return `403` and never open a file outside the root:** `/app/../secret`, `/app/%2e%2e/secret`, `/app//etc/passwd` (absolute), and a symlink that escapes the root.
- [ ] `Range: bytes=0-3` on a 10-byte file returns `206` with 4 bytes and `Content-Range`; an unsatisfiable range returns `416`.
- [ ] Repeating the request with `If-None-Match: <etag>` returns `304`.
- [ ] All scratch files written under `FOUNDRY_TEST_SCRATCH` (use `OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH")`; fall back to `user://`).

**Verify:** focused test run → all security + serving cases PASS.

**Steps:**

- [ ] **Step 1: Write failing tests** — serve + each traversal vector + range 206/416 + conditional 304. Create files under `FOUNDRY_TEST_SCRATCH` in the test setup and clean up in teardown.

- [ ] **Step 2: Run to verify failure.**

- [ ] **Step 3: Implement `HTTPFileServe`.** Resolution: `root_dir.path_join(url_path_relative_to_prefix)`, then `DirAccess::get_filesystem_path` / normalize; reject if the normalized real path does not start with the normalized root. Percent-decode the path segment before resolving. MIME map: a static `HashMap<String, String>` of extension→type (`html→text/html`, `js→application/javascript`, `css→text/css`, `json→application/json`, `png→image/png`, …). Range/conditional parsing is local to this file. Return results by populating the `HTTPResponse` (or returning a structured result the server writes).

- [ ] **Step 4: Wire `mount_files`** into `HTTPServer`: a `Vector<{String prefix, String root}>`; in dispatch, if the path starts with a prefix, delegate to `HTTPFileServe` and skip routes/signal unless it returns "not found".

- [ ] **Step 5: Run to verify pass.**

- [ ] **Step 6: Commit.**

```bash
git add modules/http_server/http_file_serve.h modules/http_server/http_file_serve.cpp \
        modules/http_server/http_server.h modules/http_server/http_server.cpp tests/core/io/test_http_server.h
git commit -m "feat(http_server): static file serving with path-traversal defense"
```

---

## Task 7: Request limits, smuggling defense, keep-alive, max_connections, timeouts

**Goal:** Hard resource limits and protocol-hardening, plus keep-alive and the operational caps from the spec.

**Files:**
- Modify: `modules/http_server/http_server.{h,cpp}`, `modules/http_server/http_server_connection.{h,cpp}`
- Test: `tests/core/io/test_http_server.h`

**Acceptance Criteria:**
- [ ] Body > `max_request_size` (default 1 MiB) → `413` + close.
- [ ] More than `max_header_count` (100) headers → `431` + close.
- [ ] A header line > `max_header_line_length` (8 KiB) → `431` + close.
- [ ] A request with both `Content-Length` and `Transfer-Encoding` → `400` + close.
- [ ] `Connection: keep-alive` reuses the connection for a second request on the same socket (test issues two pipelined GETs on one `HTTPClient`/socket and asserts both bodies).
- [ ] `max_connections` (default 64) is enforced: opening a 65th connection is immediately closed/not accepted.
- [ ] A connection that sends no complete request within the request timeout is dropped.

**Verify:** focused test run → PASS.

**Steps:**

- [ ] **Step 1: Write failing tests** for each limit and the keep-alive/multi-request path.

- [ ] **Step 2: Run to verify failure.**

- [ ] **Step 3: Implement** the caps as `HTTPServer` properties with defaults, enforced in the connection read path (check accumulated bytes/count against caps before allocation). Reject CL+TE during header processing. After writing a response, if `Connection: keep-alive` (HTTP/1.1 default, unless `Connection: close`) and no parse ambiguity, reset the connection to `READING` instead of `DONE`. Track `last_activity_usec`; in the server poll, drop connections idle beyond the timeout. Enforce `max_connections` at accept time.

- [ ] **Step 4: Run to verify pass.**

- [ ] **Step 5: Commit.**

```bash
git add modules/http_server/http_server.h modules/http_server/http_server.cpp \
        modules/http_server/http_server_connection.h modules/http_server/http_server_connection.cpp \
        tests/core/io/test_http_server.h
git commit -m "feat(http_server): limits, smuggling defense, keep-alive, connection cap, timeouts"
```

---

## Task 8: HTTPS via StreamPeerTLS

**Goal:** `listen(port, bind, tls_options)` terminates TLS using the engine's mbedTLS-backed `StreamPeerTLS::accept_stream`.

**Files:**
- Modify: `modules/http_server/http_server.{h,cpp}`, `modules/http_server/http_server_connection.{h,cpp}`
- Test: `tests/core/io/test_http_server.h`

**Acceptance Criteria:**
- [ ] `listen` accepts an optional `Ref<TLSOptions>`; when present, accepted `StreamPeerTCP` connections are wrapped with `StreamPeerTLS::create()` + `accept_stream`.
- [ ] A GET over HTTPS (client using `TLSOptions::client` against a self-signed cert generated in-test via `Crypto`) returns the expected body.
- [ ] Plaintext `listen` (no TLS) still works unchanged.

**Verify:** focused test run → HTTPS case PASS.

**Steps:**

- [ ] **Step 1: Write the failing HTTPS test.** Generate a self-signed cert+key in-test with `Crypto` (mirror `tests/core/crypto/test_crypto*` or `modules/mbedtls` tests), `TLSOptions::server(key, cert)`, and an `HTTPClient` configured with `tls_options = TLSOptions::client(...)` trusting it.

- [ ] **Step 2: Run to verify failure.**

- [ ] **Step 3: Implement.** In the accept path, when TLS is enabled, create `Ref<StreamPeerTLS> tls = StreamPeerTLS::create(); tls->accept_stream(tcp, options);` store it as the connection's `Ref<StreamPeer>`, and `poll()` the TLS layer each frame until `STATUS_CONNECTED` before feeding the parser. Handle `STATUS_ERROR` by closing.

- [ ] **Step 4: Run to verify pass.**

- [ ] **Step 5: Commit.**

```bash
git add modules/http_server/http_server.h modules/http_server/http_server.cpp \
        modules/http_server/http_server_connection.h modules/http_server/http_server_connection.cpp \
        tests/core/io/test_http_server.h
git commit -m "feat(http_server): HTTPS termination via StreamPeerTLS/mbedTLS"
```

---

## Task 9: Threaded mode (use_threads)

**Goal:** `use_threads = true` runs accept+read+parse on a `WorkerThreadPool` task; script dispatch still happens on the main thread via a mutex-guarded queue drained in `NOTIFICATION_INTERNAL_PROCESS`. Identical observable behavior to frame-poll mode.

**Files:**
- Modify: `modules/http_server/http_server.{h,cpp}`, `modules/http_server/http_server_connection.{h,cpp}`
- Test: `tests/core/io/test_http_server.h`

**Acceptance Criteria:**
- [ ] All Task 3–8 tests pass with `use_threads = true` as well as the default (run the suite twice, or add a parameterized test loop).
- [ ] The worker never calls a `Callable`/signal directly; dispatch is always on the main thread (verify by reading the dispatch site; the worker only enqueues parsed requests).
- [ ] `stop()` cleanly joins the worker task.

**Verify:** focused test run twice (threads off / on) → both PASS.

**Steps:**

- [ ] **Step 1: Add a threaded variant** of the round-trip + redirect + file tests (loop the existing cases with `server->set_use_threads(true)`).

- [ ] **Step 2: Run to verify failure.**

- [ ] **Step 3: Implement** the worker: a `WorkerThreadPool::get_singleton()->start_task` loop that owns accept + per-connection read/parse; on READY, pushes `{connection*, req, res}` into a `Mutex`-guarded `pending` queue and parks that connection. The main-thread notification drains `pending`, dispatches to script (filling `res`), marks the connection ready-to-write, and signals the worker (or the worker re-polls) to serialize the response. Mirror `scene/main/http_request.cpp`'s threaded path structure.

- [ ] **Step 4: Run to verify pass** (both modes).

- [ ] **Step 5: Commit.**

```bash
git add modules/http_server/http_server.h modules/http_server/http_server.cpp \
        modules/http_server/http_server_connection.h modules/http_server/http_server_connection.cpp \
        tests/core/io/test_http_server.h
git commit -m "feat(http_server): optional threaded accept/parse loop with main-thread dispatch"
```

---

## Task 10: Foundry Script fixtures

**Goal:** Foundry Script integration fixtures proving the types are usable from `.fs`: a route + signal handler serving a request, and an OAuth-callback capture. Uses flat-global spelling until #1828 lands.

**Files:**
- Create: `modules/foundry_script/tests/scripts/http_server/` fixtures (`.fs` + expected-output config per the foundry_script test runner convention; see sibling dirs like `modules/foundry_script/tests/scripts/analyzer/` for the file layout).

**Acceptance Criteria:**
- [ ] A fixture starts an `HTTPServer`, registers `/hello` and a `request_received` handler, and asserts the handler receives an `HTTPInboundRequest` with the expected method/path/query (fixture exercises the types via the test runner; follow the runner's expected-output format).
- [ ] An OAuth-callback fixture registers `/callback`, simulates the redirect, and asserts the captured `code` query param and the emitted `302`.
- [ ] Fixtures reference the flat-global names (`HTTPServer`, `HTTPInboundRequest`, `HTTPResponse`) with a `# TODO(#1828): move to foundry.http.server` note.

**Verify:** `./bin/foundry.<platform> --headless test run --case "*HTTPServer*" --force-colors` includes the script fixtures and they pass.

**Steps:**

- [ ] **Step 1: Inspect** an existing fixture dir under `modules/foundry_script/tests/scripts/` to copy the exact expected-output file naming/format the runner expects.

- [ ] **Step 2: Write the fixtures** exercising route registration, signal connection, `HTTPResponse.send_string`/`redirect`, and reading `HTTPInboundRequest` getters.

- [ ] **Step 3: Run** the focused suite; if output changed intentionally, regenerate fixtures with `test generate-fixtures modules/foundry_script/tests/scripts/http_server`.

- [ ] **Step 4: Commit.**

```bash
git add modules/foundry_script/tests/scripts/http_server
git commit -m "test(http_server): Foundry Script integration fixtures"
```

---

## Task 11: Move public classes under foundry.http.server (blocked by #1828)

**Goal:** Once epic #1828 (native-type namespaces) lands, register the three public classes under `foundry.http.server` instead of flat globals, update fixtures, and add class-reference XML.

**Files:**
- Modify: `modules/http_server/register_types.cpp` (use the native-namespace registration API added by #1828)
- Modify: `modules/foundry_script/tests/scripts/http_server/` fixtures (flat-global → `foundry.http.server.*`)
- Modify: `tests/core/io/test_http_server.h` (only if any test references the class by string name)
- Create: `modules/http_server/doc_classes/HTTPServer.xml`, `HTTPInboundRequest.xml`, `HTTPResponse.xml`

**Acceptance Criteria:**
- [ ] `foundry.http.server.HTTPServer` resolves in Foundry Script; the flat global is removed (per the clean-break decision — no alias).
- [ ] Fixtures use the namespaced spelling and pass.
- [ ] `doc_classes/*.xml` exists for all three classes and `get_doc_classes()` lists them (already in `config.py`).
- [ ] `pre-commit run --all-files` passes (doc/XML checks).

**Verify:** build + `./bin/foundry.<platform> --headless test run --case "*HTTPServer*" --force-colors` + `pre-commit run --all-files`.

**Steps:**

- [ ] **Step 1: Confirm #1828 is merged** and read its API for registering a native class under a namespace. Replace the three `FOUNDRY_REGISTER_CLASS(...)` calls with the namespaced form.

- [ ] **Step 2: Update fixtures** to `foundry.http.server.HTTPServer.new()` etc.; remove the `TODO(#1828)` notes.

- [ ] **Step 3: Write `doc_classes/*.xml`** following an existing module's XML (e.g. `modules/enet/doc_classes/ENetConnection.xml`) — document every bound method/signal/property.

- [ ] **Step 4: Build + test + pre-commit.** Run all three verify commands.

- [ ] **Step 5: Commit.**

```bash
git add modules/http_server/register_types.cpp modules/http_server/doc_classes \
        modules/foundry_script/tests/scripts/http_server tests/core/io/test_http_server.h
git commit -m "feat(http_server): register public classes under foundry.http.server namespace"
```

---

## Final validation (before PR)

- [ ] `python3 scripts/agent_build.py --compiler-cache ccache --jobs 4` (strict, `dev_mode=yes`) succeeds.
- [ ] `./bin/foundry.<platform> --headless test run --force-colors` → `[doctest] Status: SUCCESS!`.
- [ ] `pre-commit run --all-files` passes.
- [ ] Spec (`docs/superpowers/specs/2026-08-06-native-http-server-design.md`) and this plan are referenced in the PR description; #1828 is linked as a dependency.

## Notes for the implementer

- **Macro names:** this fork renames `GDCLASS`→`FOUNDRY_CLASS`, `GDREGISTER_CLASS`→`FOUNDRY_REGISTER_CLASS`. Match `modules/enet/` exactly.
- **Don't hand-roll HTTP parsing.** picohttpparser does request-line + header framing. You own body framing (`Content-Length`/chunked), the response writer, file serving, and limits.
- **Dispatch is always on the main thread.** Never call a script `Callable` or emit a signal from a worker thread.
- **Path traversal is the most important security property.** Resolve and prefix-check the real path; percent-decode first; never trust the raw URL.
- **Scratch files in tests go under `FOUNDRY_TEST_SCRATCH`**, never the repo root or fixture dirs.
