/**************************************************************************/
/*  test_http_server.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/doc_data.h"
#include "core/object/class_db.h"

#include "tests/test_macros.h"

#ifdef MODULE_HTTP_SERVER_ENABLED

#include "modules/http_server/http_response.h"
#include "modules/http_server/http_server.h"
#include "modules/http_server/http_server_request.h"

#include "core/crypto/crypto.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/stream_peer_tls.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"

#include "tests/test_utils.h"

namespace TestHTTPServer {

TEST_CASE("[HTTPServer] The module classes are reachable only through their namespace") {
	const StringName request_qualified = "foundry.http.server.HTTPRequest";
	const StringName response_qualified = "foundry.http.server.HTTPResponse";
	const StringName server_qualified = "foundry.http.server.HTTPServer";

	CHECK(ClassDB::class_exists(request_qualified));
	CHECK(ClassDB::class_exists(response_qualified));
	CHECK(ClassDB::class_exists(server_qualified));

	// A bare simple name never resolves for a namespaced class, and neither does the C++ token the
	// server request keeps so it does not collide with the global client node.
	CHECK(ClassDB::resolve_type_name(request_qualified) == request_qualified);
	CHECK(ClassDB::resolve_type_name(response_qualified) == response_qualified);
	CHECK(ClassDB::resolve_type_name(server_qualified) == server_qualified);
	CHECK(ClassDB::resolve_type_name("HTTPResponse") == StringName());
	CHECK(ClassDB::resolve_type_name("HTTPServer") == StringName());
	CHECK(ClassDB::resolve_type_name("HTTPServerRequest") == StringName());
	CHECK_FALSE(ClassDB::class_exists("HTTPServerRequest"));

	// The exposed simple name is what tooling shows, while the C++ token is what maps back.
	StringName simple_name;
	CHECK(ClassDB::class_get_by_qualified_name(request_qualified, simple_name));
	CHECK(simple_name == StringName("HTTPRequest"));
	CHECK(ClassDB::class_get_qualified_name("HTTPServerRequest") == request_qualified);
	CHECK(HTTPServerRequest::get_class_static() == request_qualified);
	CHECK(HTTPResponse::get_class_static() == response_qualified);

	// The bare `HTTPRequest` still belongs to the client node in its own namespace, so the server
	// class's exposed name did not steal it.
	CHECK(ClassDB::class_get_qualified_name("HTTPRequest") == StringName("foundry.http.client.HTTPRequest"));
	CHECK(ClassDB::class_get_in_namespace("foundry.http.server", "HTTPRequest") == request_qualified);

	CHECK(ClassDB::is_parent_class(request_qualified, "RefCounted"));
	CHECK(ClassDB::is_parent_class(response_qualified, "RefCounted"));
}

TEST_CASE("[HTTPServer] HTTPRequest instantiates through its qualified name") {
	Ref<HTTPServerRequest> request = Ref<HTTPServerRequest>(Object::cast_to<HTTPServerRequest>(ClassDB::instantiate("foundry.http.server.HTTPRequest")));
	REQUIRE(request.is_valid());

	CHECK(request->get_class() == String("foundry.http.server.HTTPRequest"));
	CHECK(String(request->call("get_method")).is_empty());
	CHECK(String(request->call("get_path")).is_empty());
	CHECK(PackedByteArray(request->call("get_body")).is_empty());
	CHECK(String(request->call("get_body_string")).is_empty());
	CHECK_FALSE(bool(request->call("has_header", "Accept")));
}

TEST_CASE("[HTTPServer] HTTPRequest exposes the parsed request line") {
	Ref<HTTPServerRequest> request;
	request.instantiate();

	SUBCASE("The method is normalized to upper case") {
		request->set_method("get");
		CHECK(request->get_method() == "GET");
		CHECK(String(request->call("get_method")) == "GET");
	}

	SUBCASE("A target without a query decodes into the path") {
		request->set_raw_path("/files/a%20b.txt");
		CHECK(request->get_raw_path() == "/files/a%20b.txt");
		CHECK(request->get_path() == "/files/a b.txt");
		CHECK(request->get_query().is_empty());
	}

	SUBCASE("A query string is split, form-decoded and exposed as a dictionary") {
		request->set_raw_path("/search?q=hello+world&page=2&debug&plus=%2B");

		CHECK(request->get_raw_path() == "/search?q=hello+world&page=2&debug&plus=%2B");
		CHECK(request->get_path() == "/search");

		const Dictionary query = request->get_query();
		CHECK(query.size() == 4);
		CHECK(String(query["q"]) == "hello world");
		CHECK(String(query["page"]) == "2");
		// A key with no `=` is present with an empty value rather than absent.
		CHECK(query.has("debug"));
		CHECK(String(query["debug"]).is_empty());
		CHECK(String(query["plus"]) == "+");

		CHECK(Dictionary(request->call("get_query")).size() == 4);
	}

	SUBCASE("A repeated query key keeps its last value") {
		request->set_raw_path("/search?tag=a&tag=b");
		const Dictionary query = request->get_query();
		CHECK(query.size() == 1);
		CHECK(String(query["tag"]) == "b");
	}

	SUBCASE("A plus sign in the path stays literal") {
		// Only the query string is form-encoded, where `+` stands for a space.
		request->set_raw_path("/c++/notes?lang=c%2B%2B");
		CHECK(request->get_path() == "/c++/notes");
		CHECK(String(request->get_query()["lang"]) == "c++");

		request->set_raw_path("/notes?title=a+b");
		CHECK(String(request->get_query()["title"]) == "a b");
	}

	SUBCASE("Re-parsing a target replaces the previous query") {
		request->set_raw_path("/first?a=1");
		request->set_raw_path("/second");
		CHECK(request->get_path() == "/second");
		CHECK(request->get_query().is_empty());
	}
}

TEST_CASE("[HTTPServer] HTTPRequest header lookup is case-insensitive") {
	Ref<HTTPServerRequest> request;
	request.instantiate();

	request->add_header("Content-Type", "application/json");
	request->add_header("X-Trace", "abc");

	CHECK(request->get_header("content-type") == "application/json");
	CHECK(request->get_header("CONTENT-TYPE") == "application/json");
	CHECK(request->has_header("x-trace"));
	CHECK_FALSE(request->has_header("authorization"));
	CHECK(request->get_header("authorization").is_empty());

	// The dictionary view keeps the casing the client sent.
	const Dictionary headers = request->get_headers();
	CHECK(headers.size() == 2);
	CHECK(String(headers["Content-Type"]) == "application/json");

	SUBCASE("A repeated field folds into one comma-separated value") {
		request->add_header("Accept", "text/html");
		request->add_header("accept", "application/json");
		CHECK(request->get_header("Accept") == "text/html, application/json");
		CHECK(request->get_headers().size() == 3);
	}

	SUBCASE("An empty field name is rejected") {
		ERR_PRINT_OFF;
		request->add_header("", "value");
		ERR_PRINT_ON;
		CHECK(request->get_headers().size() == 2);
	}

	SUBCASE("The bound methods answer the same way") {
		CHECK(String(request->call("get_header", "cOnTeNt-TyPe")) == "application/json");
		CHECK(bool(request->call("has_header", "X-TRACE")));
		CHECK(Dictionary(request->call("get_headers")).size() == 2);
	}
}

TEST_CASE("[HTTPServer] HTTPRequest carries the body and the peer") {
	Ref<HTTPServerRequest> request;
	request.instantiate();

	// Built through `String::utf8` so the literal's bytes are decoded as UTF-8 rather than Latin-1.
	const String payload = String::utf8("{\"name\":\"café\"}");
	request->set_body(payload.to_utf8_buffer());
	request->set_peer("127.0.0.1:52344");

	CHECK(request->get_body().size() == payload.to_utf8_buffer().size());
	CHECK(request->get_body_string() == payload);
	CHECK(String(request->call("get_body_string")) == payload);
	CHECK(request->get_peer() == "127.0.0.1:52344");
	CHECK(String(request->call("get_peer")) == "127.0.0.1:52344");
}

TEST_CASE("[HTTPServer] HTTPResponse records the status and headers") {
	Ref<HTTPResponse> response;
	response.instantiate();

	CHECK(response->get_status() == 200);
	CHECK(int(response->get("status")) == 200);
	CHECK_FALSE(response->is_sent());
	CHECK(response->get_body_source() == HTTPResponse::BODY_SOURCE_NONE);

	SUBCASE("A status outside the HTTP range is rejected and leaves the previous one") {
		response->set_status(404);
		ERR_PRINT_OFF;
		response->set_status(99);
		response->call("set_status", 600);
		ERR_PRINT_ON;
		CHECK(response->get_status() == 404);
		CHECK(int(response->call("get_status")) == 404);
	}

	SUBCASE("The status is also reachable as a property") {
		bool valid = false;
		response->set("status", 201, &valid);
		CHECK(valid);
		CHECK(response->get_status() == 201);
	}

	SUBCASE("Setting a field again replaces it regardless of casing") {
		response->set_header("Content-Type", "text/plain");
		response->set_header("CONTENT-TYPE", "application/json");
		CHECK(response->get_headers().size() == 1);
		CHECK(response->get_header("content-type") == "application/json");
		// The dictionary keeps the spelling used first.
		CHECK(String(response->get_headers()["Content-Type"]) == "application/json");
		CHECK(response->has_header("Content-Type"));
	}

	SUBCASE("An empty field name is rejected") {
		ERR_PRINT_OFF;
		response->set_header("", "value");
		ERR_PRINT_ON;
		CHECK(response->get_headers().is_empty());
	}
}

TEST_CASE("[HTTPServer] HTTPResponse commits its body exactly once") {
	Ref<HTTPResponse> response;
	response.instantiate();

	SUBCASE("send_string stores the UTF-8 encoding of the text") {
		const String text = String::utf8("café");
		response->send_string(text);
		CHECK(response->is_sent());
		CHECK(response->get_body_source() == HTTPResponse::BODY_SOURCE_BYTES);
		CHECK(response->get_body_string() == text);
		// Four characters, but five bytes: the accented one needs two.
		CHECK(response->get_body().size() == 5);
		CHECK(response->get_file_path().is_empty());
	}

	SUBCASE("send stores raw bytes") {
		PackedByteArray payload;
		payload.push_back(0x00);
		payload.push_back(0xff);
		response->send(payload);
		CHECK(response->is_sent());
		CHECK(response->get_body_source() == HTTPResponse::BODY_SOURCE_BYTES);
		CHECK(response->get_body() == payload);
	}

	SUBCASE("redirect sets the status and the Location field") {
		response->redirect("/login", 303);
		CHECK(response->is_sent());
		CHECK(response->get_status() == 303);
		CHECK(response->get_header("location") == "/login");
		CHECK(response->get_body().is_empty());
		CHECK(response->get_body_source() == HTTPResponse::BODY_SOURCE_BYTES);
	}

	SUBCASE("redirect defaults to 302 through the bound method") {
		response->call("redirect", "/elsewhere");
		CHECK(response->get_status() == 302);
		CHECK(String(response->call("get_header", "Location")) == "/elsewhere");
	}

	SUBCASE("An invalid redirect neither commits nor changes the status") {
		ERR_PRINT_OFF;
		response->redirect("/somewhere", 200);
		response->redirect("", 302);
		ERR_PRINT_ON;
		CHECK_FALSE(response->is_sent());
		CHECK(response->get_status() == 200);
		CHECK_FALSE(response->has_header("Location"));
	}

	SUBCASE("send_file records the path instead of a byte body") {
		response->send_file("res://index.html");
		CHECK(response->is_sent());
		CHECK(response->get_body_source() == HTTPResponse::BODY_SOURCE_FILE);
		CHECK(response->get_file_path() == "res://index.html");
		CHECK(response->get_body().is_empty());
		CHECK(String(response->call("get_file_path")) == "res://index.html");
	}

	SUBCASE("An empty file path is rejected and does not commit") {
		ERR_PRINT_OFF;
		response->send_file("");
		ERR_PRINT_ON;
		CHECK_FALSE(response->is_sent());
		CHECK(response->get_body_source() == HTTPResponse::BODY_SOURCE_NONE);
	}

	SUBCASE("A second commit is refused and leaves the first one intact") {
		response->send_string("first");
		ERR_PRINT_OFF;
		response->send_string("second");
		response->send_file("res://other.html");
		response->redirect("/elsewhere", 302);
		ERR_PRINT_ON;
		CHECK(response->get_body_string() == "first");
		CHECK(response->get_body_source() == HTTPResponse::BODY_SOURCE_BYTES);
		CHECK(response->get_file_path().is_empty());
		CHECK(response->get_status() == 200);
	}
}

TEST_CASE("[HTTPServer] HTTPResponse instantiates through its qualified name") {
	Ref<HTTPResponse> response = Ref<HTTPResponse>(Object::cast_to<HTTPResponse>(ClassDB::instantiate("foundry.http.server.HTTPResponse")));
	REQUIRE(response.is_valid());

	CHECK(response->get_class() == String("foundry.http.server.HTTPResponse"));

	response->call("set_status", 201);
	response->call("set_header", "X-Powered-By", "Foundry");
	response->call("send_string", "created");

	CHECK(int(response->call("get_status")) == 201);
	CHECK(String(response->call("get_header", "x-powered-by")) == "Foundry");
	CHECK(bool(response->call("is_sent")));
	CHECK(String(response->call("get_body_string")) == "created");
	CHECK(int(response->call("get_body_source")) == int(HTTPResponse::BODY_SOURCE_BYTES));
}

TEST_CASE("[HTTPServer] A bound enum documents its owner by qualified name") {
	// `VARIANT_ENUM_CAST` records the C++ token of the owning class, which is not the registry key
	// for a namespaced class. The class reference has to name the owner the way it is registered,
	// otherwise the enum reference cannot be resolved.
	MethodBind *method = ClassDB::get_method("foundry.http.server.HTTPResponse", "get_body_source");
	REQUIRE(method != nullptr);

	DocData::MethodDoc method_doc;
	DocData::return_doc_from_retinfo(method_doc, method->get_return_info());
	CHECK(method_doc.return_type == "int");
	CHECK(method_doc.return_enum == "foundry.http.server.HTTPResponse.BodySource");

	// A flat owner is left alone, and so is a reference that names no class at all.
	CHECK(DocData::qualify_enum_owner("Object.ConnectFlags") == "Object.ConnectFlags");
	CHECK(DocData::qualify_enum_owner("Variant.Type") == "Variant.Type");
	CHECK(DocData::qualify_enum_owner("BodySource") == "BodySource");
}

namespace {

constexpr uint64_t ROUND_TRIP_TIMEOUT_USEC = 5000000;
constexpr uint32_t POLL_SLEEP_USEC = 1000;
const char *LOOPBACK = "127.0.0.1";

// Records what a route handler saw and answers with a fixed body, so a test can assert both the
// handler's view of the request and the bytes the client reads back.
class RecordingRouteHandler : public Object {
	FOUNDRY_SOFTCLASS(RecordingRouteHandler, Object);

public:
	int call_count = 0;
	String seen_method;
	String seen_path;
	String seen_query_value;
	String seen_peer;
	String seen_user_agent;

	int reply_status = 200;
	String reply_body = "hi";

	void handle(const Ref<HTTPServerRequest> &p_request, const Ref<HTTPResponse> &p_response) {
		call_count++;
		seen_method = p_request->get_method();
		seen_path = p_request->get_path();
		seen_query_value = p_request->get_query().get("q", String());
		seen_peer = p_request->get_peer();
		seen_user_agent = p_request->get_header("user-agent");

		p_response->set_status(reply_status);
		p_response->set_header("Content-Type", "text/plain");
		p_response->send_string(reply_body);
	}
};

// Calls back into the server from inside a handler, which is the re-entrancy the poll pass has to
// survive: registering routes reallocates the route storage, and stopping closes the very
// connection being dispatched.
class ReentrantRouteHandler : public Object {
	FOUNDRY_SOFTCLASS(ReentrantRouteHandler, Object);

public:
	HTTPServer *server = nullptr;
	bool stop_server = false;
	int extra_routes = 0;
	int call_count = 0;

	void handle(const Ref<HTTPServerRequest> &p_request, const Ref<HTTPResponse> &p_response) {
		call_count++;
		for (int i = 0; i < extra_routes; i++) {
			server->route("GET", "/extra" + itos(i), callable_mp(this, &ReentrantRouteHandler::handle));
		}
		if (stop_server) {
			server->stop();
		}
		p_response->send_string("done");
	}
};

// Receives the `request_received` signal. It records what it saw and, when asked to, answers the
// request itself, which is how the catch-all step of the resolution ladder is meant to be used.
class RecordingSignalObserver : public Object {
	FOUNDRY_SOFTCLASS(RecordingSignalObserver, Object);

public:
	int call_count = 0;
	String seen_path;
	bool seen_response_already_sent = false;

	bool answer = false;
	bool amend_status = true;
	int reply_status = 200;
	String reply_body = "from signal";

	void on_request_received(const Ref<HTTPServerRequest> &p_request, const Ref<HTTPResponse> &p_response) {
		call_count++;
		seen_path = p_request->get_path();
		seen_response_already_sent = p_response->is_sent();

		if (answer) {
			if (amend_status) {
				p_response->set_status(reply_status);
			}
			p_response->send_string(reply_body);
		}
	}
};

// Answers the way an OAuth callback endpoint does: it reads a query parameter off the request and
// redirects the browser somewhere else.
class RedirectingRouteHandler : public Object {
	FOUNDRY_SOFTCLASS(RedirectingRouteHandler, Object);

public:
	String seen_code;
	String location = "/done";
	int reply_status = 302;

	void handle(const Ref<HTTPServerRequest> &p_request, const Ref<HTTPResponse> &p_response) {
		seen_code = p_request->get_query().get("code", String());
		p_response->redirect(location + "?code=" + seen_code.uri_encode(), reply_status);
	}
};

// Echoes the request body back, which is the round trip a POST endpoint has to survive: the body
// bytes only exist if the connection kept reading past the header block.
class EchoRouteHandler : public Object {
	FOUNDRY_SOFTCLASS(EchoRouteHandler, Object);

public:
	int call_count = 0;
	int seen_body_size = -1;
	String seen_body;
	String seen_content_type;
	String seen_custom_header;

	void handle(const Ref<HTTPServerRequest> &p_request, const Ref<HTTPResponse> &p_response) {
		call_count++;
		seen_body_size = p_request->get_body().size();
		seen_body = p_request->get_body_string();
		seen_content_type = p_request->get_header("content-type");
		seen_custom_header = p_request->get_header("x-request-id");

		p_response->set_header("X-Echo", "1");
		p_response->send(p_request->get_body());
	}
};

// Answers with a status whose semantics forbid a body, and still hands the response a body, so the
// writer's framing decision is what the client observes.
class BodylessStatusRouteHandler : public Object {
	FOUNDRY_SOFTCLASS(BodylessStatusRouteHandler, Object);

public:
	int reply_status = 204;
	String reply_body;

	void handle(const Ref<HTTPServerRequest> &p_request, const Ref<HTTPResponse> &p_response) {
		p_response->set_status(reply_status);
		p_response->send_string(reply_body);
	}
};

// A signal receiver is script code just like a route handler, so it may also close the connection
// out from under the dispatch that is running.
class StoppingSignalObserver : public Object {
	FOUNDRY_SOFTCLASS(StoppingSignalObserver, Object);

public:
	HTTPServer *server = nullptr;
	int call_count = 0;

	void on_request_received(const Ref<HTTPServerRequest> &p_request, const Ref<HTTPResponse> &p_response) {
		call_count++;
		server->stop();
	}
};

struct ClientResult {
	Error error = FAILED;
	int status = 0;
	String body;
	// Lower-cased field name -> value, as read back from the status line's header block.
	HashMap<String, String> headers;

	bool has_header(const String &p_name) const { return headers.has(p_name.to_lower()); }
	String get_header(const String &p_name) const {
		HashMap<String, String>::ConstIterator found = headers.find(p_name.to_lower());
		return found ? found->value : String();
	}
};

// Performs one request against `p_server` while driving the server's poll loop, so the whole
// exchange runs on this thread without a scene tree.
ClientResult http_request(HTTPServer *p_server, int p_port, HTTPClient::Method p_method, const String &p_path,
		const String &p_body = String(), const Vector<String> &p_headers = Vector<String>(),
		const Ref<TLSOptions> &p_tls_options = Ref<TLSOptions>()) {
	ClientResult result;

	Ref<HTTPClient> client = HTTPClient::create();
	if (client.is_null()) {
		return result;
	}
	result.error = client->connect_to_host(LOOPBACK, p_port, p_tls_options);
	if (result.error != OK) {
		return result;
	}

	const CharString request_body = p_body.utf8();

	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + ROUND_TRIP_TIMEOUT_USEC;
	bool requested = false;
	PackedByteArray body;

	while (OS::get_singleton()->get_ticks_usec() < deadline) {
		p_server->poll();

		// Polling a client that is reading a body treats a close by the peer as a connection error,
		// so once the body starts arriving, drain it instead of polling.
		if (client->get_status() == HTTPClient::STATUS_BODY) {
			body.append_array(client->read_response_body_chunk());
		} else {
			client->poll();
		}

		if (client->has_response() && result.status == 0) {
			result.status = client->get_response_code();
			// `get_response_headers()` drains the list, so this is the one chance to read it.
			List<String> header_lines;
			client->get_response_headers(&header_lines);
			for (const String &line : header_lines) {
				const int separator = line.find_char(':');
				if (separator < 0) {
					continue;
				}
				result.headers[line.substr(0, separator).strip_edges().to_lower()] = line.substr(separator + 1).strip_edges();
			}
		}

		const HTTPClient::Status status = client->get_status();
		if (result.status == 0 &&
				(status == HTTPClient::STATUS_CANT_RESOLVE || status == HTTPClient::STATUS_CANT_CONNECT ||
						status == HTTPClient::STATUS_CONNECTION_ERROR || status == HTTPClient::STATUS_TLS_HANDSHAKE_ERROR)) {
			result.error = ERR_CONNECTION_ERROR;
			return result;
		}

		if (!requested) {
			if (status == HTTPClient::STATUS_CONNECTED) {
				result.error = client->request(p_method, p_path, p_headers,
						request_body.length() > 0 ? (const uint8_t *)request_body.get_data() : nullptr, request_body.length());
				if (result.error != OK) {
					return result;
				}
				requested = true;
			}
			continue;
		}

		if (result.status != 0 && status != HTTPClient::STATUS_REQUESTING && status != HTTPClient::STATUS_BODY) {
			result.body = body.is_empty() ? String() : String::utf8((const char *)body.ptr(), body.size());
			result.error = OK;
			return result;
		}

		OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
	}

	result.error = ERR_TIMEOUT;
	return result;
}

ClientResult http_get(HTTPServer *p_server, int p_port, const String &p_path) {
	return http_request(p_server, p_port, HTTPClient::METHOD_GET, p_path);
}

// Connects a raw socket and drives the server until it owns the connection, so a test can place the
// boundaries between reads itself instead of relying on how the network stack packs the bytes.
Ref<StreamPeerTCP> connect_raw(HTTPServer *p_server, int p_port) {
	Ref<StreamPeerTCP> client;
	client.instantiate();
	if (client->connect_to_host(IPAddress(LOOPBACK), p_port) != OK) {
		return Ref<StreamPeerTCP>();
	}

	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + ROUND_TRIP_TIMEOUT_USEC;
	while (p_server->get_connection_count() == 0 && OS::get_singleton()->get_ticks_usec() < deadline) {
		p_server->poll();
		client->poll();
		OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
	}
	return client;
}

void send_raw(const Ref<StreamPeerTCP> &p_client, const String &p_bytes) {
	const CharString bytes = p_bytes.utf8();
	p_client->put_data((const uint8_t *)bytes.get_data(), bytes.length());
}

// Runs a fixed number of poll passes, which is what "the server saw everything sent so far" means
// when no response is expected yet.
void pump(HTTPServer *p_server, const Ref<StreamPeerTCP> &p_client, int p_rounds) {
	for (int i = 0; i < p_rounds; i++) {
		p_server->poll();
		p_client->poll();
		OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
	}
}

// Runs poll passes without a client attached, which is how a test observes what the server does to
// a connection nobody is feeding.
void pump_server(HTTPServer *p_server, int p_rounds) {
	for (int i = 0; i < p_rounds; i++) {
		p_server->poll();
		OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
	}
}

// One response decoded off a raw socket, which is what a test reads when it needs the status line
// the server wrote rather than what an HTTP client made of it.
struct RawResponse {
	int status = 0;
	HashMap<String, String> headers;
	String body;

	bool has_header(const String &p_name) const { return headers.has(p_name.to_lower()); }
	String get_header(const String &p_name) const {
		HashMap<String, String>::ConstIterator found = headers.find(p_name.to_lower());
		return found ? found->value : String();
	}
};

// Drives the server until one complete response has been read off `p_client`, and removes the bytes
// it consumed from `r_pending`, so a reused socket reads its next response from where this one
// stopped. Returns false if no complete response arrived before the timeout.
bool read_raw_response(HTTPServer *p_server, const Ref<StreamPeerTCP> &p_client, String &r_pending, RawResponse &r_response) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + ROUND_TRIP_TIMEOUT_USEC;
	while (OS::get_singleton()->get_ticks_usec() < deadline) {
		p_server->poll();
		p_client->poll();

		const int available = p_client->get_available_bytes();
		if (available > 0) {
			Vector<uint8_t> chunk;
			chunk.resize(available);
			int received = 0;
			if (p_client->get_partial_data(chunk.ptrw(), available, received) == OK && received > 0) {
				r_pending += String::utf8((const char *)chunk.ptr(), received);
			}
		}

		const int head_end = r_pending.find("\r\n\r\n");
		if (head_end >= 0) {
			const Vector<String> lines = r_pending.substr(0, head_end).split("\r\n");
			const Vector<String> status_parts = lines[0].split(" ");
			r_response.status = status_parts.size() > 1 ? status_parts[1].to_int() : 0;
			r_response.headers.clear();
			for (int i = 1; i < lines.size(); i++) {
				const int separator = lines[i].find_char(':');
				if (separator < 0) {
					continue;
				}
				r_response.headers[lines[i].substr(0, separator).strip_edges().to_lower()] = lines[i].substr(separator + 1).strip_edges();
			}

			const int content_length = r_response.get_header("content-length").to_int();
			const int body_start = head_end + 4;
			if (r_pending.length() - body_start >= content_length) {
				r_response.body = r_pending.substr(body_start, content_length);
				r_pending = r_pending.substr(body_start + content_length);
				return true;
			}
		}

		OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
	}
	return false;
}

} // namespace

TEST_CASE("[HTTPServer] Listening binds an OS-assigned port and stop releases it") {
	HTTPServer *server = memnew(HTTPServer);

	CHECK_FALSE(server->is_listening());
	CHECK(server->get_listening_port() == -1);

	CHECK(server->get_port() == 8080);
	CHECK(server->get_bind_address() == LOOPBACK);

	// Port 0 asks the OS for a free port, so concurrent test runs cannot collide.
	server->set_port(0);
	REQUIRE(server->listen() == OK);
	CHECK(server->is_listening());
	const int port = server->get_listening_port();
	CHECK(port > 0);

	SUBCASE("The bound port is reported back while the configured port stays 0") {
		CHECK(server->get_port() == 0);
	}

	SUBCASE("Listening twice is refused and leaves the first listener alone") {
		ERR_PRINT_OFF;
		CHECK(server->listen() == ERR_ALREADY_IN_USE);
		ERR_PRINT_ON;
		CHECK(server->get_listening_port() == port);
	}

	SUBCASE("An out-of-range port is rejected and leaves the property unchanged") {
		HTTPServer *other = memnew(HTTPServer);
		ERR_PRINT_OFF;
		other->set_port(70000);
		other->set_bind_address("");
		ERR_PRINT_ON;
		CHECK(other->get_port() == 8080);
		CHECK(other->get_bind_address() == LOOPBACK);
		memdelete(other);
	}

	SUBCASE("An unparsable bind address fails instead of binding a wildcard") {
		HTTPServer *other = memnew(HTTPServer);
		other->set_port(0);
		other->set_bind_address("not-an-address");
		ERR_PRINT_OFF;
		CHECK(other->listen() == ERR_INVALID_PARAMETER);
		ERR_PRINT_ON;
		CHECK_FALSE(other->is_listening());
		memdelete(other);
	}

	server->stop();
	CHECK_FALSE(server->is_listening());
	CHECK(server->get_listening_port() == -1);

	memdelete(server);
}

TEST_CASE("[HTTPServer] GET round-trip through a registered route") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	RecordingRouteHandler *handler = memnew(RecordingRouteHandler);
	server->route("GET", "/hello", callable_mp(handler, &RecordingRouteHandler::handle));

	SUBCASE("A matching request reaches the handler and its body reaches the client") {
		const ClientResult result = http_get(server, port, "/hello?q=world");

		CHECK(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "hi");

		CHECK(handler->call_count == 1);
		CHECK(handler->seen_method == "GET");
		CHECK(handler->seen_path == "/hello");
		CHECK(handler->seen_query_value == "world");
		CHECK(handler->seen_peer.begins_with("127.0.0.1:"));
		CHECK_FALSE(handler->seen_user_agent.is_empty());
	}

	SUBCASE("A handler-chosen status and body are written verbatim") {
		handler->reply_status = 201;
		handler->reply_body = "created";

		const ClientResult result = http_get(server, port, "/hello");

		CHECK(result.error == OK);
		CHECK(result.status == 201);
		CHECK(result.body == "created");
	}

	SUBCASE("A path with no route answers 404 without reaching the handler") {
		const ClientResult result = http_get(server, port, "/missing");

		CHECK(result.error == OK);
		CHECK(result.status == 404);
		CHECK(handler->call_count == 0);
	}

	SUBCASE("A route only matches its own method") {
		const ClientResult result = http_get(server, port, "/hello");
		CHECK(result.status == 200);

		// The same path registered for another method must not shadow the GET route.
		RecordingRouteHandler *post_handler = memnew(RecordingRouteHandler);
		server->route("POST", "/hello", callable_mp(post_handler, &RecordingRouteHandler::handle));

		const ClientResult second = http_get(server, port, "/hello");
		CHECK(second.status == 200);
		CHECK(post_handler->call_count == 0);
		memdelete(post_handler);
	}

	SUBCASE("The first registered route for a method and path wins") {
		RecordingRouteHandler *later = memnew(RecordingRouteHandler);
		later->reply_body = "later";
		server->route("GET", "/hello", callable_mp(later, &RecordingRouteHandler::handle));

		const ClientResult result = http_get(server, port, "/hello");
		CHECK(result.body == "hi");
		CHECK(later->call_count == 0);
		memdelete(later);
	}

	SUBCASE("Consecutive requests from separate clients are each served") {
		CHECK(http_get(server, port, "/hello").body == "hi");
		CHECK(http_get(server, port, "/hello").body == "hi");
		CHECK(handler->call_count == 2);

		// Each client is gone by now, so the sockets it left behind are dropped on the next passes.
		pump_server(server, 16);
		CHECK(server->get_connection_count() == 0);
	}

	server->stop();
	memdelete(handler);
	memdelete(server);
}

TEST_CASE("[HTTPServer] A GET round-trip terminates TLS for HTTPS") {
	if (!StreamPeerTLS::is_available()) {
		return;
	}

	Ref<Crypto> crypto = Crypto::create();
	REQUIRE(crypto.is_valid());

	// Generated in memory for the duration of the test, so no key material is ever written to disk.
	Ref<CryptoKey> key = crypto->generate_rsa(2048);
	REQUIRE(key.is_valid());
	Ref<X509Certificate> certificate = crypto->generate_self_signed_certificate(key, "CN=foundry-http-test", "20140101000000", "20340101000000");
	REQUIRE(certificate.is_valid());

	const Ref<TLSOptions> server_options = TLSOptions::server(key, certificate);
	REQUIRE(server_options.is_valid());
	// The self-signed certificate is its own trust anchor, and the common name it was issued under is
	// what the client verifies against rather than the loopback address it dialed.
	const Ref<TLSOptions> client_options = TLSOptions::client(certificate, "foundry-http-test");
	REQUIRE(client_options.is_valid());

	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	REQUIRE(server->listen(server_options) == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	RecordingRouteHandler *handler = memnew(RecordingRouteHandler);
	handler->reply_body = "secure";
	server->route("GET", "/secure", callable_mp(handler, &RecordingRouteHandler::handle));

	SUBCASE("A request over the TLS channel reaches the handler and its body reaches the client") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/secure", String(), Vector<String>(), client_options);

		CHECK(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "secure");

		CHECK(handler->call_count == 1);
		CHECK(handler->seen_method == "GET");
		CHECK(handler->seen_path == "/secure");
		CHECK(handler->seen_peer.begins_with("127.0.0.1:"));
	}

	SUBCASE("A plaintext client cannot speak to the TLS listener") {
		ERR_PRINT_OFF;
		const ClientResult result = http_get(server, port, "/secure");
		ERR_PRINT_ON;

		// The plaintext request is not a TLS record, so the handshake never completes and no handler
		// runs. It either fails to connect or times out; either way it is not a served 200.
		CHECK(result.status != 200);
		CHECK(handler->call_count == 0);
	}

	server->stop();
	memdelete(handler);
	memdelete(server);
}

TEST_CASE("[HTTPServer] A handler may mutate the server from inside dispatch") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	ReentrantRouteHandler *handler = memnew(ReentrantRouteHandler);
	handler->server = server;
	server->route("GET", "/reentrant", callable_mp(handler, &ReentrantRouteHandler::handle));

	SUBCASE("Registering more routes during dispatch still answers the running request") {
		handler->extra_routes = 16;

		const ClientResult result = http_get(server, port, "/reentrant");
		CHECK(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "done");
		CHECK(handler->call_count == 1);
	}

	SUBCASE("Stopping the server during dispatch closes the connection instead of writing") {
		handler->stop_server = true;

		const ClientResult result = http_get(server, port, "/reentrant");
		CHECK(result.error != OK);
		CHECK(handler->call_count == 1);
		CHECK_FALSE(server->is_listening());
		CHECK(server->get_connection_count() == 0);
	}

	server->stop();
	memdelete(handler);
	memdelete(server);
}

TEST_CASE("[HTTPServer] The resolution ladder runs routes, then the signal, then 404") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	CHECK_FALSE(server->is_emitting_for_all());
	CHECK_FALSE(bool(server->get("emit_for_all")));

	RecordingRouteHandler *handler = memnew(RecordingRouteHandler);
	server->route("GET", "/hello", callable_mp(handler, &RecordingRouteHandler::handle));

	RecordingSignalObserver *observer = memnew(RecordingSignalObserver);
	const Callable receiver = callable_mp(observer, &RecordingSignalObserver::on_request_received);

	SUBCASE("A routed request does not reach the signal while emit_for_all is off") {
		REQUIRE(server->connect("request_received", receiver) == OK);

		const ClientResult result = http_get(server, port, "/hello");

		CHECK(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "hi");
		CHECK(handler->call_count == 1);
		CHECK(observer->call_count == 0);
	}

	SUBCASE("A routed request also reaches the signal once emit_for_all is on") {
		server->set_emit_for_all(true);
		CHECK(server->is_emitting_for_all());
		REQUIRE(server->connect("request_received", receiver) == OK);

		// The observer tries to answer too, which must not overwrite the body the route committed.
		observer->answer = true;
		observer->amend_status = false;
		observer->reply_body = "observer";

		ERR_PRINT_OFF;
		const ClientResult result = http_get(server, port, "/hello");
		ERR_PRINT_ON;

		CHECK(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "hi");
		CHECK(handler->call_count == 1);
		CHECK(observer->call_count == 1);
		CHECK(observer->seen_path == "/hello");
		// The route committed first, so the observer sees a response that is already sent.
		CHECK(observer->seen_response_already_sent);
	}

	SUBCASE("A receiver may no longer amend the status of a response a route committed") {
		server->set_emit_for_all(true);
		REQUIRE(server->connect("request_received", receiver) == OK);

		// Committing locks the whole response, status line included, so a receiver running behind a
		// route cannot rewrite what the client is about to read.
		observer->answer = true;
		observer->amend_status = true;
		observer->reply_status = 503;

		ERR_PRINT_OFF;
		const ClientResult result = http_get(server, port, "/hello");
		ERR_PRINT_ON;

		CHECK(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "hi");
	}

	SUBCASE("An unrouted request reaches the signal, which may answer it") {
		REQUIRE(server->connect("request_received", receiver) == OK);
		observer->answer = true;
		observer->reply_status = 201;
		observer->reply_body = "catch-all";

		const ClientResult result = http_get(server, port, "/missing");

		CHECK(result.error == OK);
		CHECK(result.status == 201);
		CHECK(result.body == "catch-all");
		CHECK(handler->call_count == 0);
		CHECK(observer->call_count == 1);
		CHECK(observer->seen_path == "/missing");
		CHECK_FALSE(observer->seen_response_already_sent);
	}

	SUBCASE("An unrouted request a receiver leaves alone falls through to 404") {
		REQUIRE(server->connect("request_received", receiver) == OK);
		observer->answer = false;

		const ClientResult result = http_get(server, port, "/missing");

		CHECK(result.error == OK);
		CHECK(result.status == 404);
		CHECK(result.body == "Not Found");
		CHECK(observer->call_count == 1);
	}

	SUBCASE("An unrouted request with nothing connected answers 404") {
		const ClientResult result = http_get(server, port, "/missing");

		CHECK(result.error == OK);
		CHECK(result.status == 404);
		CHECK(result.body == "Not Found");
		CHECK(handler->call_count == 0);
		CHECK(observer->call_count == 0);
	}

	SUBCASE("A receiver may stop the server instead of answering") {
		StoppingSignalObserver *stopper = memnew(StoppingSignalObserver);
		stopper->server = server;
		REQUIRE(server->connect("request_received", callable_mp(stopper, &StoppingSignalObserver::on_request_received)) == OK);

		const ClientResult result = http_get(server, port, "/missing");

		CHECK(result.error != OK);
		CHECK(stopper->call_count == 1);
		CHECK_FALSE(server->is_listening());
		CHECK(server->get_connection_count() == 0);
		memdelete(stopper);
	}

	server->stop();
	memdelete(observer);
	memdelete(handler);
	memdelete(server);
}

TEST_CASE("[HTTPServer] The request and response surface survives a round trip") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	SUBCASE("A query parameter is readable and the redirect it drives reaches the client") {
		RedirectingRouteHandler *handler = memnew(RedirectingRouteHandler);
		server->route("GET", "/cb", callable_mp(handler, &RedirectingRouteHandler::handle));

		const ClientResult result = http_get(server, port, "/cb?code=abc&state=xyz");

		CHECK(result.error == OK);
		CHECK(result.status == 302);
		CHECK(result.get_header("Location") == "/done?code=abc");
		CHECK(handler->seen_code == "abc");
		CHECK(result.body.is_empty());
		CHECK(result.get_header("content-length") == "0");

		memdelete(handler);
	}

	SUBCASE("A redirect may name another status in the redirection range") {
		RedirectingRouteHandler *handler = memnew(RedirectingRouteHandler);
		handler->reply_status = 303;
		handler->location = "/elsewhere";
		server->route("GET", "/cb", callable_mp(handler, &RedirectingRouteHandler::handle));

		const ClientResult result = http_get(server, port, "/cb?code=a%20b");

		CHECK(result.status == 303);
		CHECK(handler->seen_code == "a b");
		CHECK(result.get_header("Location") == "/elsewhere?code=a%20b");

		memdelete(handler);
	}

	SUBCASE("A POST body is read past the header block and echoed back") {
		EchoRouteHandler *handler = memnew(EchoRouteHandler);
		server->route("POST", "/echo", callable_mp(handler, &EchoRouteHandler::handle));

		Vector<String> headers;
		headers.push_back("Content-Type: application/json");
		headers.push_back("X-Request-Id: 42");
		const String payload = "{\"name\":\"Ana\",\"note\":\"café\"}";

		const ClientResult result = http_request(server, port, HTTPClient::METHOD_POST, "/echo", payload, headers);

		CHECK(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == payload);
		CHECK(result.get_header("x-echo") == "1");
		// The multi-byte character makes the byte count differ from the character count, so this also
		// pins that framing counts bytes.
		CHECK(handler->seen_body_size == payload.utf8().length());
		CHECK(result.get_header("content-length") == itos(payload.utf8().length()));
		CHECK(handler->seen_content_type == "application/json");
		CHECK(handler->seen_custom_header == "42");

		memdelete(handler);
	}

	SUBCASE("A POST with no body reaches the handler with an empty body") {
		EchoRouteHandler *handler = memnew(EchoRouteHandler);
		server->route("POST", "/echo", callable_mp(handler, &EchoRouteHandler::handle));

		const ClientResult result = http_request(server, port, HTTPClient::METHOD_POST, "/echo");

		CHECK(result.error == OK);
		CHECK(result.status == 200);
		CHECK(handler->seen_body_size == 0);
		CHECK(result.body.is_empty());

		memdelete(handler);
	}

	SUBCASE("A body larger than one read is assembled before the handler runs") {
		EchoRouteHandler *handler = memnew(EchoRouteHandler);
		server->route("POST", "/echo", callable_mp(handler, &EchoRouteHandler::handle));

		String payload;
		for (int i = 0; i < 512; i++) {
			payload += "0123456789abcdef";
		}

		const ClientResult result = http_request(server, port, HTTPClient::METHOD_POST, "/echo", payload);

		CHECK(result.error == OK);
		CHECK(handler->seen_body_size == payload.length());
		CHECK(result.body == payload);

		memdelete(handler);
	}

	server->stop();
	memdelete(server);
}

TEST_CASE("[HTTPServer] A body that arrives after its header block is assembled before dispatch") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	EchoRouteHandler *handler = memnew(EchoRouteHandler);
	server->route("POST", "/echo", callable_mp(handler, &EchoRouteHandler::handle));

	SUBCASE("A request split between its header block and its body waits for the rest") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		REQUIRE(server->get_connection_count() == 1);

		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 11\r\n\r\n");
		pump(server, client, 8);

		// The header block is complete but the body is not, so nothing has been dispatched.
		CHECK(handler->call_count == 0);
		CHECK(server->get_connection_count() == 1);

		send_raw(client, "hello");
		pump(server, client, 8);
		CHECK(handler->call_count == 0);

		send_raw(client, " world");
		const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + ROUND_TRIP_TIMEOUT_USEC;
		while (handler->call_count == 0 && OS::get_singleton()->get_ticks_usec() < deadline) {
			pump(server, client, 1);
		}

		CHECK(handler->call_count == 1);
		CHECK(handler->seen_body_size == 11);
		CHECK(handler->seen_body == "hello world");
	}

	SUBCASE("A request framed by both a content length and a transfer coding is refused with 400") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		REQUIRE(server->get_connection_count() == 1);

		// Ambiguous framing: the two fields disagree about where the body ends, so no handler may be
		// given a body assembled from a guess. This is the shape a request-smuggling attempt takes.
		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n"
						 "Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 400);
		CHECK(response.get_header("connection") == "close");

		CHECK(handler->call_count == 0);
		pump(server, client, 4);
		CHECK(server->get_connection_count() == 0);
	}

	SUBCASE("A content length that is not a plain number is refused with 400") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		REQUIRE(server->get_connection_count() == 1);

		// A repeated field folds into "5, 5", which is not a length this layer can act on, and which
		// is the other way a smuggled request tries to make two parties disagree on the framing.
		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 400);

		CHECK(handler->call_count == 0);
		pump(server, client, 4);
		CHECK(server->get_connection_count() == 0);
	}

	SUBCASE("A content length past the body cap is refused with 413 without buffering it") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		REQUIRE(server->get_connection_count() == 1);

		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 99999999\r\n\r\n");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 413);

		CHECK(handler->call_count == 0);
		pump(server, client, 4);
		CHECK(server->get_connection_count() == 0);
	}

	SUBCASE("A transfer coding on its own is refused with 501") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		// Nothing here decodes a transfer coding, so a body framed by one cannot be handed to a
		// handler at all.
		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 501);
		CHECK(handler->call_count == 0);
	}

	SUBCASE("A header line folded onto the previous one is refused with 400") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		// A recipient that honors the deprecated fold reads a length of 5 here and one that ignores
		// it reads nothing, so answering at all means answering a request two parties frame
		// differently.
		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length:\r\n 5\r\n\r\nhello");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 400);
		CHECK(handler->call_count == 0);
	}

	SUBCASE("A malformed request line is refused with 400") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		send_raw(client, "not a request line\r\n\r\n");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 400);
		CHECK(handler->call_count == 0);
	}

	server->stop();
	memdelete(handler);
	memdelete(server);
}

TEST_CASE("[HTTPServer] A status that forbids a body is framed without one") {
	// RFC 9110 forbids Content-Length on 204 and 304, and both statuses carry no body at all.
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	BodylessStatusRouteHandler *handler = memnew(BodylessStatusRouteHandler);
	server->route("GET", "/none", callable_mp(handler, &BodylessStatusRouteHandler::handle));

	SUBCASE("A 204 carries no Content-Length and no body") {
		handler->reply_status = 204;

		const ClientResult result = http_get(server, port, "/none");

		CHECK(result.error == OK);
		CHECK(result.status == 204);
		CHECK_FALSE(result.has_header("content-length"));
		CHECK(result.body.is_empty());
	}

	SUBCASE("A 304 carries no Content-Length even when the handler committed a body") {
		handler->reply_status = 304;
		handler->reply_body = "ignored";

		const ClientResult result = http_get(server, port, "/none");

		CHECK(result.error == OK);
		CHECK(result.status == 304);
		CHECK_FALSE(result.has_header("content-length"));
		CHECK(result.body.is_empty());
	}

	SUBCASE("A status that allows a body still carries Content-Length") {
		handler->reply_status = 200;
		handler->reply_body = "body";

		const ClientResult result = http_get(server, port, "/none");

		CHECK(result.status == 200);
		CHECK(result.get_header("content-length") == "4");
		CHECK(result.body == "body");
	}

	SUBCASE("An empty 200 body still carries a zero Content-Length") {
		handler->reply_status = 200;

		const ClientResult result = http_get(server, port, "/none");

		CHECK(result.status == 200);
		CHECK(result.get_header("content-length") == "0");
	}

	server->stop();
	memdelete(handler);
	memdelete(server);
}

TEST_CASE("[HTTPServer] Committing a response closes it to further changes") {
	Ref<HTTPResponse> response;
	response.instantiate();
	response->set_status(201);
	response->set_header("Content-Type", "text/plain");
	response->send_string("created");

	SUBCASE("The status can no longer be changed") {
		ERR_PRINT_OFF;
		response->set_status(503);
		ERR_PRINT_ON;

		CHECK(response->get_status() == 201);
	}

	SUBCASE("The status property is gated the same way as the method") {
		ERR_PRINT_OFF;
		response->set("status", 503);
		ERR_PRINT_ON;

		CHECK(int(response->get("status")) == 201);
	}

	SUBCASE("A header field can no longer be added or replaced") {
		ERR_PRINT_OFF;
		response->set_header("Content-Type", "text/html");
		response->set_header("X-Late", "1");
		ERR_PRINT_ON;

		CHECK(response->get_header("content-type") == "text/plain");
		CHECK_FALSE(response->has_header("x-late"));
	}

	SUBCASE("The body is untouched by the refused changes") {
		ERR_PRINT_OFF;
		response->set_status(503);
		response->set_header("X-Late", "1");
		response->send_string("late");
		ERR_PRINT_ON;

		CHECK(response->get_body_string() == "created");
		CHECK(response->get_status() == 201);
	}

	SUBCASE("redirect still sets its own status and Location before committing") {
		Ref<HTTPResponse> fresh;
		fresh.instantiate();
		fresh->redirect("/next", 307);

		CHECK(fresh->is_sent());
		CHECK(fresh->get_status() == 307);
		CHECK(fresh->get_header("location") == "/next");

		// And a redirect on top of a commit changes nothing at all.
		ERR_PRINT_OFF;
		fresh->redirect("/other", 302);
		ERR_PRINT_ON;

		CHECK(fresh->get_status() == 307);
		CHECK(fresh->get_header("location") == "/next");
	}
}

TEST_CASE("[HTTPServer] stop closes the listener and every open connection") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	Ref<StreamPeerTCP> client;
	client.instantiate();
	REQUIRE(client->connect_to_host(IPAddress(LOOPBACK), port) == OK);

	// Drive the accept loop until the server owns the connection.
	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + ROUND_TRIP_TIMEOUT_USEC;
	while (server->get_connection_count() == 0 && OS::get_singleton()->get_ticks_usec() < deadline) {
		server->poll();
		client->poll();
		OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
	}
	REQUIRE(server->get_connection_count() == 1);

	server->stop();

	CHECK_FALSE(server->is_listening());
	CHECK(server->get_connection_count() == 0);

	// The peer observes the close, and the released port no longer accepts connections.
	const uint64_t close_deadline = OS::get_singleton()->get_ticks_usec() + ROUND_TRIP_TIMEOUT_USEC;
	while (client->get_status() == StreamPeerTCP::STATUS_CONNECTED && OS::get_singleton()->get_ticks_usec() < close_deadline) {
		client->poll();
		OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
	}
	CHECK(client->get_status() != StreamPeerTCP::STATUS_CONNECTED);

	Ref<StreamPeerTCP> rejected;
	rejected.instantiate();
	if (rejected->connect_to_host(IPAddress(LOOPBACK), port) == OK) {
		const uint64_t reject_deadline = OS::get_singleton()->get_ticks_usec() + ROUND_TRIP_TIMEOUT_USEC;
		while (rejected->get_status() == StreamPeerTCP::STATUS_CONNECTING && OS::get_singleton()->get_ticks_usec() < reject_deadline) {
			rejected->poll();
			OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
		}
		CHECK(rejected->get_status() != StreamPeerTCP::STATUS_CONNECTED);
	}

	// Polling a stopped server is a no-op rather than an error.
	server->poll();

	memdelete(server);
}

namespace {

// The shared scratch space the agent build exports, so a generated tree never lands in the
// repository. Without it the per-process temporary directory the other tests use is good enough.
String file_serve_scratch_root() {
	String base;
	if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
		base = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH").simplify_path();
	}
	if (base.is_empty()) {
		base = TestUtils::get_temp_path("http_server");
	}
	return base.path_join("http_file_serve");
}

// Removes a generated tree without ever descending through a symbolic link, so the traversal
// fixtures cannot delete anything outside the tree they created.
void remove_tree(const String &p_path) {
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		return;
	}
	if (filesystem->is_link(p_path) || !filesystem->dir_exists(p_path)) {
		DirAccess::remove_absolute(p_path);
		return;
	}

	Ref<DirAccess> listing = DirAccess::open(p_path);
	if (listing.is_valid()) {
		listing->list_dir_begin();
		for (String name = listing->get_next(); !name.is_empty(); name = listing->get_next()) {
			if (name == "." || name == "..") {
				continue;
			}
			remove_tree(p_path.path_join(name));
		}
		listing->list_dir_end();
	}
	DirAccess::remove_absolute(p_path);
}

void write_text_file(const String &p_path, const String &p_contents) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string(p_contents);
}

// A mount root holding one file per behavior under test, next to a file that lives outside the
// root and that every traversal vector is trying to reach. The tree is rebuilt from scratch and
// removed again, so an interrupted run leaves nothing behind that a later run would read.
struct MountTree {
	String base;
	String root;

	explicit MountTree(const String &p_case_name) {
		base = file_serve_scratch_root().path_join(p_case_name + "_" + itos(OS::get_singleton()->get_process_id()));
		root = base.path_join("public");

		remove_tree(base);
		REQUIRE(DirAccess::make_dir_recursive_absolute(root.path_join("data")) == OK);

		write_text_file(root.path_join("index.html"), "<h1>hi</h1>");
		write_text_file(root.path_join("data").path_join("app.js"), "console.log(1);");
		write_text_file(root.path_join("ten.txt"), "0123456789");
		write_text_file(base.path_join("secret.txt"), "top secret");
	}

	// Both shapes of symbolic-link escape: a link to a file outside the root, and a link to the
	// directory that contains it.
	void link_out_of_root() {
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		REQUIRE(filesystem->create_link(base.path_join("secret.txt"), root.path_join("escape.txt")) == OK);
		REQUIRE(filesystem->create_link(base, root.path_join("up")) == OK);
	}

	~MountTree() { remove_tree(base); }
};

// Brings up a listening server with `p_tree` mounted under `/app`, which is the fixture every
// static-file case starts from.
HTTPServer *mounted_server(const MountTree &p_tree, int &r_port) {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	server->mount_files("/app", p_tree.root);
	REQUIRE(server->listen() == OK);
	r_port = server->get_listening_port();
	REQUIRE(r_port > 0);
	return server;
}

} // namespace

TEST_CASE("[HTTPServer] A mount serves files from its root") {
	MountTree tree("serves");
	int port = 0;
	HTTPServer *server = mounted_server(tree, port);

	SUBCASE("A file is served with its content type and validators") {
		const ClientResult result = http_get(server, port, "/app/index.html");
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "<h1>hi</h1>");
		CHECK(result.get_header("content-type") == "text/html");
		CHECK(result.get_header("content-length") == "11");
		CHECK(result.get_header("accept-ranges") == "bytes");
		CHECK_FALSE(result.get_header("etag").is_empty());
		CHECK(result.get_header("last-modified").ends_with("GMT"));
	}

	SUBCASE("A nested file keeps the content type of its own extension") {
		const ClientResult result = http_get(server, port, "/app/data/app.js");
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "console.log(1);");
		CHECK(result.get_header("content-type") == "application/javascript");
	}

	SUBCASE("An unknown extension falls back to an opaque content type") {
		write_text_file(tree.root.path_join("blob.unknownext"), "xx");
		const ClientResult result = http_get(server, port, "/app/blob.unknownext");
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.get_header("content-type") == "application/octet-stream");
	}

	SUBCASE("A percent-escaped file name resolves to the file it names") {
		write_text_file(tree.root.path_join("a b.txt"), "spaced");
		const ClientResult result = http_get(server, port, "/app/a%20b.txt");
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "spaced");
	}

	SUBCASE("A file larger than one write chunk is streamed whole") {
		// The writer copies a file body into its buffer a piece at a time, so a body has to be
		// bigger than one piece for the refill path to run at all.
		String contents;
		while (contents.length() < 200000) {
			contents += "0123456789abcdef";
		}
		write_text_file(tree.root.path_join("large.txt"), contents);

		const ClientResult result = http_get(server, port, "/app/large.txt");
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.get_header("content-length") == itos(contents.length()));
		CHECK(result.body.length() == contents.length());
		CHECK(result.body == contents);
	}

	SUBCASE("A range spanning several write chunks is streamed whole") {
		String contents;
		while (contents.length() < 200000) {
			contents += "0123456789abcdef";
		}
		write_text_file(tree.root.path_join("large.txt"), contents);

		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/large.txt", String(),
				{ "Range: bytes=1000-150999" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 206);
		CHECK(result.get_header("content-length") == "150000");
		CHECK(result.body.length() == 150000);
		CHECK(result.body == contents.substr(1000, 150000));
	}

	SUBCASE("A missing file inside the root is a plain 404") {
		const ClientResult result = http_get(server, port, "/app/missing.html");
		REQUIRE(result.error == OK);
		CHECK(result.status == 404);
	}

	SUBCASE("A directory is never served as a body") {
		const ClientResult data = http_get(server, port, "/app/data");
		REQUIRE(data.error == OK);
		CHECK(data.status == 404);

		const ClientResult mount_root = http_get(server, port, "/app");
		REQUIRE(mount_root.error == OK);
		CHECK(mount_root.status == 404);
	}

	SUBCASE("A path outside the mount prefix is untouched by the mount") {
		const ClientResult result = http_get(server, port, "/elsewhere/index.html");
		REQUIRE(result.error == OK);
		CHECK(result.status == 404);
	}

	SUBCASE("HEAD answers with the framing of the file and no body") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_HEAD, "/app/index.html");
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.get_header("content-length") == "11");
		CHECK(result.body.is_empty());
	}

	SUBCASE("A method a mount cannot answer is refused with the ones it can") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_POST, "/app/index.html", "payload");
		REQUIRE(result.error == OK);
		CHECK(result.status == 405);
		CHECK(result.get_header("allow") == "GET, HEAD");
	}

	memdelete(server);
}

TEST_CASE("[HTTPServer] A mount refuses every path that leaves its root") {
	MountTree tree("traversal");
	tree.link_out_of_root();
	int port = 0;
	HTTPServer *server = mounted_server(tree, port);

	// Every one of these resolves outside the mount root, either literally or through a link. The
	// file they reach exists and is readable, so a leak would show up as its contents.
	const Vector<String> vectors = {
		"/app/../secret.txt",
		"/app/%2e%2e/secret.txt",
		"/app/%2E%2E/secret.txt",
		"/app/data/../../secret.txt",
		"/app/..%2fsecret.txt",
		"/app//etc/passwd",
		"/app/./../secret.txt",
		"/app/up/secret.txt",
		"/app/escape.txt",
		// A separator only some platforms honor, which is how a path that is safe on one platform
		// becomes an escape on another.
		"/app/..%5Csecret.txt",
		"/app/sub%5C..%5C..%5Csecret.txt",
		// A control character, which is what a truncation attack is built out of.
		"/app/index.html%00.txt",
		"/app/index%0d%0a.html",
	};

	for (const String &vector : vectors) {
		CAPTURE(vector);
		const ClientResult result = http_get(server, port, vector);
		REQUIRE(result.error == OK);
		CHECK(result.status == 403);
		CHECK_FALSE(result.body.contains("top secret"));
	}

	// A refusal says nothing about what does or does not exist outside the root: an escaping path
	// that names nothing is refused exactly like one that names a real file.
	const ClientResult absent = http_get(server, port, "/app/../no_such_file.txt");
	REQUIRE(absent.error == OK);
	CHECK(absent.status == 403);

	memdelete(server);
}

TEST_CASE("[HTTPServer] A mount answers a byte range") {
	MountTree tree("range");
	int port = 0;
	HTTPServer *server = mounted_server(tree, port);

	SUBCASE("A leading range is answered with 206 and the requested bytes") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/ten.txt", String(),
				{ "Range: bytes=0-3" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 206);
		CHECK(result.body == "0123");
		CHECK(result.get_header("content-range") == "bytes 0-3/10");
		CHECK(result.get_header("content-length") == "4");
	}

	SUBCASE("An open-ended range runs to the end of the file") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/ten.txt", String(),
				{ "Range: bytes=7-" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 206);
		CHECK(result.body == "789");
		CHECK(result.get_header("content-range") == "bytes 7-9/10");
	}

	SUBCASE("A suffix range counts back from the end") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/ten.txt", String(),
				{ "Range: bytes=-4" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 206);
		CHECK(result.body == "6789");
		CHECK(result.get_header("content-range") == "bytes 6-9/10");
	}

	SUBCASE("A range that runs past the end is clamped to the last byte") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/ten.txt", String(),
				{ "Range: bytes=8-99" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 206);
		CHECK(result.body == "89");
		CHECK(result.get_header("content-range") == "bytes 8-9/10");
	}

	SUBCASE("A suffix longer than the file is the whole file") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/ten.txt", String(),
				{ "Range: bytes=-999" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 206);
		CHECK(result.body == "0123456789");
		CHECK(result.get_header("content-range") == "bytes 0-9/10");
	}

	SUBCASE("A range that ends before it starts is ignored") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/ten.txt", String(),
				{ "Range: bytes=5-2" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "0123456789");
	}

	SUBCASE("A range that starts past the end is unsatisfiable") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/ten.txt", String(),
				{ "Range: bytes=10-12" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 416);
		CHECK(result.get_header("content-range") == "bytes */10");
	}

	SUBCASE("A range this layer cannot act on falls back to the whole file") {
		const ClientResult multiple = http_request(server, port, HTTPClient::METHOD_GET, "/app/ten.txt", String(),
				{ "Range: bytes=0-1,4-5" });
		REQUIRE(multiple.error == OK);
		CHECK(multiple.status == 200);
		CHECK(multiple.body == "0123456789");

		const ClientResult other_unit = http_request(server, port, HTTPClient::METHOD_GET, "/app/ten.txt", String(),
				{ "Range: items=0-1" });
		REQUIRE(other_unit.error == OK);
		CHECK(other_unit.status == 200);
		CHECK(other_unit.body == "0123456789");
	}

	memdelete(server);
}

TEST_CASE("[HTTPServer] A mount answers a conditional request with 304") {
	MountTree tree("conditional");
	int port = 0;
	HTTPServer *server = mounted_server(tree, port);

	const ClientResult first = http_get(server, port, "/app/index.html");
	REQUIRE(first.error == OK);
	REQUIRE(first.status == 200);
	const String etag = first.get_header("etag");
	const String last_modified = first.get_header("last-modified");
	REQUIRE_FALSE(etag.is_empty());
	REQUIRE_FALSE(last_modified.is_empty());

	SUBCASE("A matching entity tag skips the body") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/index.html", String(),
				{ "If-None-Match: " + etag });
		REQUIRE(result.error == OK);
		CHECK(result.status == 304);
		CHECK(result.body.is_empty());
		CHECK(result.get_header("etag") == etag);
		CHECK_FALSE(result.has_header("content-length"));
	}

	SUBCASE("A tag list is matched entry by entry, and the wildcard always matches") {
		const ClientResult listed = http_request(server, port, HTTPClient::METHOD_GET, "/app/index.html", String(),
				{ "If-None-Match: \"other\", " + etag });
		REQUIRE(listed.error == OK);
		CHECK(listed.status == 304);

		const ClientResult wildcard = http_request(server, port, HTTPClient::METHOD_GET, "/app/index.html", String(),
				{ "If-None-Match: *" });
		REQUIRE(wildcard.error == OK);
		CHECK(wildcard.status == 304);
	}

	SUBCASE("A stale entity tag is answered with the body") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/index.html", String(),
				{ "If-None-Match: \"stale\"" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "<h1>hi</h1>");
	}

	SUBCASE("A modification date that is not older than the file skips the body") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/index.html", String(),
				{ "If-Modified-Since: " + last_modified });
		REQUIRE(result.error == OK);
		CHECK(result.status == 304);
		CHECK(result.body.is_empty());
	}

	SUBCASE("An older modification date is answered with the body") {
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/index.html", String(),
				{ "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "<h1>hi</h1>");
	}

	SUBCASE("An entity tag decides on its own when both conditions are sent") {
		// The date says the file changed, but the tag says it did not, and the tag wins.
		const ClientResult result = http_request(server, port, HTTPClient::METHOD_GET, "/app/index.html", String(),
				{ "If-None-Match: " + etag, "If-Modified-Since: Thu, 01 Jan 1970 00:00:00 GMT" });
		REQUIRE(result.error == OK);
		CHECK(result.status == 304);
	}

	memdelete(server);
}

TEST_CASE("[HTTPServer] A mount resolves ahead of routes but yields when it holds no file") {
	MountTree tree("ladder");
	int port = 0;
	HTTPServer *server = mounted_server(tree, port);

	RecordingRouteHandler handler;
	handler.reply_body = "from route";
	server->route("GET", "/app/index.html", callable_mp(&handler, &RecordingRouteHandler::handle));
	server->route("GET", "/app/generated.html", callable_mp(&handler, &RecordingRouteHandler::handle));

	SUBCASE("A file the mount holds is served instead of the route that shadows it") {
		const ClientResult result = http_get(server, port, "/app/index.html");
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "<h1>hi</h1>");
		CHECK(handler.call_count == 0);
	}

	SUBCASE("A path the mount holds no file for is left to the route") {
		const ClientResult result = http_get(server, port, "/app/generated.html");
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "from route");
		CHECK(handler.call_count == 1);
	}

	SUBCASE("A refusal is final and never reaches a route") {
		server->route("GET", "/app/../secret.txt", callable_mp(&handler, &RecordingRouteHandler::handle));
		const ClientResult result = http_get(server, port, "/app/../secret.txt");
		REQUIRE(result.error == OK);
		CHECK(result.status == 403);
		CHECK(handler.call_count == 0);
	}

	SUBCASE("A served file does not announce itself on the signal") {
		RecordingSignalObserver observer;
		server->connect("request_received", callable_mp(&observer, &RecordingSignalObserver::on_request_received));

		const ClientResult served = http_get(server, port, "/app/index.html");
		REQUIRE(served.error == OK);
		CHECK(served.status == 200);
		CHECK(observer.call_count == 0);

		// A path no mount and no route claimed still reaches the catch-all.
		const ClientResult unclaimed = http_get(server, port, "/app/missing.html");
		REQUIRE(unclaimed.error == OK);
		CHECK(unclaimed.status == 404);
		CHECK(observer.call_count == 1);
	}

	memdelete(server);
}

TEST_CASE("[HTTPServer] Mounting validates its prefix and its root") {
	MountTree tree("registration");
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);

	ERR_PRINT_OFF;
	server->mount_files("app", tree.root);
	server->mount_files("", tree.root);
	server->mount_files("/app", tree.base.path_join("no_such_directory"));
	server->mount_files("/app", tree.root.path_join("index.html"));
	server->mount_files("/app", "");
	ERR_PRINT_ON;
	CHECK(server->get_mount_count() == 0);

	server->mount_files("/app/", tree.root);
	CHECK(server->get_mount_count() == 1);

	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	SUBCASE("A trailing slash on the prefix does not change what the prefix matches") {
		const ClientResult result = http_get(server, port, "/app/index.html");
		REQUIRE(result.error == OK);
		CHECK(result.status == 200);
		CHECK(result.body == "<h1>hi</h1>");
	}

	SUBCASE("A prefix only matches on a whole path segment") {
		const ClientResult result = http_get(server, port, "/application/index.html");
		REQUIRE(result.error == OK);
		CHECK(result.status == 404);
	}

	SUBCASE("The first mount that matches answers, in registration order") {
		server->mount_files("/app", tree.base);
		CHECK(server->get_mount_count() == 2);

		// The second mount holds `secret.txt`, but the first one claims the path and has no such
		// file, so the request never reaches the second.
		const ClientResult result = http_get(server, port, "/app/secret.txt");
		REQUIRE(result.error == OK);
		CHECK(result.status == 404);
	}

	memdelete(server);
}

TEST_CASE("[HTTPServer] The request limits carry the documented defaults") {
	HTTPServer *server = memnew(HTTPServer);

	CHECK(server->get_max_request_body_bytes() == 1048576);
	CHECK(server->get_max_header_count() == 100);
	CHECK(server->get_max_header_line_bytes() == 8192);
	CHECK(server->get_max_header_block_bytes() == 32768);
	CHECK(server->get_max_connections() == 64);
	CHECK(server->get_connection_timeout_seconds() == doctest::Approx(30.0));

	SUBCASE("A limit that cannot bound anything is rejected and leaves the property unchanged") {
		ERR_PRINT_OFF;
		server->set_max_request_body_bytes(0);
		server->set_max_header_count(0);
		server->set_max_header_line_bytes(0);
		server->set_max_header_block_bytes(0);
		server->set_max_connections(0);
		server->set_connection_timeout_seconds(-1.0);
		ERR_PRINT_ON;

		CHECK(server->get_max_request_body_bytes() == 1048576);
		CHECK(server->get_max_header_count() == 100);
		CHECK(server->get_max_header_line_bytes() == 8192);
		CHECK(server->get_max_header_block_bytes() == 32768);
		CHECK(server->get_max_connections() == 64);
		CHECK(server->get_connection_timeout_seconds() == doctest::Approx(30.0));
	}

	SUBCASE("A header count past what one parse pass can hold is rejected") {
		ERR_PRINT_OFF;
		server->set_max_header_count(100000);
		ERR_PRINT_ON;
		CHECK(server->get_max_header_count() == 100);
	}

	SUBCASE("A zero timeout is accepted and disables the drop") {
		server->set_connection_timeout_seconds(0.0);
		CHECK(server->get_connection_timeout_seconds() == doctest::Approx(0.0));
	}

	memdelete(server);
}

TEST_CASE("[HTTPServer] A request past a limit is answered with a status") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	EchoRouteHandler *handler = memnew(EchoRouteHandler);
	server->route("POST", "/echo", callable_mp(handler, &EchoRouteHandler::handle));
	server->route("GET", "/echo", callable_mp(handler, &EchoRouteHandler::handle));

	SUBCASE("A body larger than the cap is refused with 413 before it is buffered") {
		server->set_max_request_body_bytes(16);

		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 17\r\n\r\n");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 413);
		CHECK(handler->call_count == 0);
	}

	SUBCASE("A body exactly at the cap is served") {
		server->set_max_request_body_bytes(16);

		const ClientResult result = http_request(server, port, HTTPClient::METHOD_POST, "/echo", "0123456789abcdef");
		CHECK(result.status == 200);
		CHECK(handler->seen_body_size == 16);
	}

	SUBCASE("More header fields than the cap allows are refused with 431") {
		server->set_max_header_count(8);

		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		String request = "GET /echo HTTP/1.1\r\nHost: localhost\r\n";
		for (int i = 0; i < 8; i++) {
			request += "X-Field-" + itos(i) + ": v\r\n";
		}
		send_raw(client, request + "\r\n");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 431);
		CHECK(handler->call_count == 0);
	}

	SUBCASE("Far more header fields than one parse pass can hold are still refused with 431") {
		server->set_max_header_count(8);

		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		String request = "GET /echo HTTP/1.1\r\nHost: localhost\r\n";
		for (int i = 0; i < 64; i++) {
			request += "X-Field-" + itos(i) + ": v\r\n";
		}
		send_raw(client, request + "\r\n");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 431);
		CHECK(handler->call_count == 0);
	}

	SUBCASE("Exactly as many header fields as the cap allows are served") {
		server->set_max_header_count(8);

		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		String request = "GET /echo HTTP/1.1\r\nHost: localhost\r\n";
		for (int i = 0; i < 7; i++) {
			request += "X-Field-" + itos(i) + ": v\r\n";
		}
		send_raw(client, request + "\r\n");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 200);
		CHECK(handler->call_count == 1);
	}

	SUBCASE("A header line longer than the cap is refused with 431") {
		// The default line cap is 8 KiB, and the default block cap is large enough that the block
		// still completes, so the refusal is the line bound and nothing else.
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		String value;
		for (int i = 0; i < 9000; i++) {
			value += "a";
		}
		send_raw(client, "GET /echo HTTP/1.1\r\nHost: localhost\r\nX-Long: " + value + "\r\n\r\n");

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 431);
		CHECK(handler->call_count == 0);
	}

	SUBCASE("A header block that never ends within the cap is refused with 431") {
		server->set_max_header_block_bytes(1024);

		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		String request = "GET /echo HTTP/1.1\r\n";
		for (int i = 0; i < 40; i++) {
			request += "X-Field-" + itos(i) + ": 0123456789012345678901234567890123456789\r\n";
		}
		// Deliberately never terminated: the block can only grow past the cap from here.
		send_raw(client, request);

		String pending;
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 431);
		CHECK(handler->call_count == 0);
	}

	server->stop();
	memdelete(handler);
	memdelete(server);
}

TEST_CASE("[HTTPServer] Keep-alive serves more than one request on one socket") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	RecordingRouteHandler *handler = memnew(RecordingRouteHandler);
	server->route("GET", "/hello", callable_mp(handler, &RecordingRouteHandler::handle));
	EchoRouteHandler *echo = memnew(EchoRouteHandler);
	server->route("POST", "/echo", callable_mp(echo, &EchoRouteHandler::handle));

	SUBCASE("Two requests on one socket are both answered and the socket stays open") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		REQUIRE(server->get_connection_count() == 1);

		String pending;

		send_raw(client, "GET /hello?q=first HTTP/1.1\r\nHost: localhost\r\n\r\n");
		RawResponse first;
		REQUIRE(read_raw_response(server, client, pending, first));
		CHECK(first.status == 200);
		CHECK(first.body == "hi");
		CHECK(first.get_header("connection") == "keep-alive");
		CHECK(handler->seen_query_value == "first");
		CHECK(server->get_connection_count() == 1);

		send_raw(client, "GET /hello?q=second HTTP/1.1\r\nHost: localhost\r\n\r\n");
		RawResponse second;
		REQUIRE(read_raw_response(server, client, pending, second));
		CHECK(second.status == 200);
		CHECK(second.body == "hi");
		CHECK(handler->seen_query_value == "second");
		CHECK(handler->call_count == 2);
		CHECK(server->get_connection_count() == 1);
	}

	SUBCASE("A body on the first request does not bleed into the second") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		String pending;

		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhello");
		RawResponse first;
		REQUIRE(read_raw_response(server, client, pending, first));
		CHECK(first.status == 200);
		CHECK(first.body == "hello");
		CHECK(echo->seen_body_size == 5);

		// A second request with no body at all: anything left over from the first would show up here
		// as a body the client never sent.
		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n");
		RawResponse second;
		REQUIRE(read_raw_response(server, client, pending, second));
		CHECK(second.status == 200);
		CHECK(second.get_header("content-length") == "0");
		CHECK(second.body.is_empty());
		CHECK(echo->seen_body_size == 0);
		CHECK(echo->call_count == 2);
	}

	SUBCASE("Header fields from the first request do not bleed into the second") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		String pending;

		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nX-Request-Id: first\r\nContent-Length: 0\r\n\r\n");
		RawResponse first;
		REQUIRE(read_raw_response(server, client, pending, first));
		CHECK(first.status == 200);
		CHECK(echo->seen_custom_header == "first");

		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n");
		RawResponse second;
		REQUIRE(read_raw_response(server, client, pending, second));
		CHECK(second.status == 200);
		CHECK(echo->seen_custom_header.is_empty());
	}

	SUBCASE("A request asking to close is answered and then closed") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		String pending;
		send_raw(client, "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 200);
		CHECK(response.get_header("connection") == "close");

		pump(server, client, 8);
		CHECK(server->get_connection_count() == 0);
	}

	SUBCASE("An HTTP/1.0 request is closed unless it asks to stay open") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		String pending;
		send_raw(client, "GET /hello HTTP/1.0\r\nHost: localhost\r\n\r\n");
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 200);
		CHECK(response.get_header("connection") == "close");

		pump(server, client, 8);
		CHECK(server->get_connection_count() == 0);
	}

	SUBCASE("An HTTP/1.0 request that asks to stay open is kept") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		String pending;
		send_raw(client, "GET /hello HTTP/1.0\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n");
		RawResponse first;
		REQUIRE(read_raw_response(server, client, pending, first));
		CHECK(first.status == 200);
		CHECK(first.get_header("connection") == "keep-alive");

		send_raw(client, "GET /hello HTTP/1.0\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n");
		RawResponse second;
		REQUIRE(read_raw_response(server, client, pending, second));
		CHECK(second.status == 200);
		CHECK(handler->call_count == 2);
	}

	SUBCASE("Two requests sent as one write are both answered in order") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		// Pipelined: the second request is already in the socket, and may already be in the read
		// buffer, while the first is still being answered. Neither may be lost, and neither may be
		// read as part of the other.
		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhello"
						 "GET /hello?q=second HTTP/1.1\r\nHost: localhost\r\n\r\n");

		String pending;
		RawResponse first;
		REQUIRE(read_raw_response(server, client, pending, first));
		CHECK(first.status == 200);
		CHECK(first.body == "hello");
		CHECK(echo->seen_body_size == 5);

		RawResponse second;
		REQUIRE(read_raw_response(server, client, pending, second));
		CHECK(second.status == 200);
		CHECK(second.body == "hi");
		CHECK(handler->seen_query_value == "second");
	}

	SUBCASE("A refused request is not followed by a second one on the same socket") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		String pending;
		send_raw(client, "POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n");
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 400);
		CHECK(response.get_header("connection") == "close");

		pump(server, client, 8);
		CHECK(server->get_connection_count() == 0);
	}

	server->stop();
	memdelete(echo);
	memdelete(handler);
	memdelete(server);
}

TEST_CASE("[HTTPServer] The connection cap bounds how many sockets the server owns") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	server->set_max_connections(2);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	RecordingRouteHandler *handler = memnew(RecordingRouteHandler);
	server->route("GET", "/hello", callable_mp(handler, &RecordingRouteHandler::handle));

	Ref<StreamPeerTCP> first = connect_raw(server, port);
	REQUIRE(first.is_valid());
	Ref<StreamPeerTCP> second;
	second.instantiate();
	REQUIRE(second->connect_to_host(IPAddress(LOOPBACK), port) == OK);
	Ref<StreamPeerTCP> third;
	third.instantiate();
	REQUIRE(third->connect_to_host(IPAddress(LOOPBACK), port) == OK);

	for (int i = 0; i < 24; i++) {
		server->poll();
		first->poll();
		second->poll();
		third->poll();
		OS::get_singleton()->delay_usec(POLL_SLEEP_USEC);
	}

	// The third socket is connected as far as the operating system is concerned, but the server has
	// not taken it, so it cannot consume a slot or any memory here.
	CHECK(server->get_connection_count() == 2);

	SUBCASE("A pending connection is taken once a slot frees") {
		first->disconnect_from_host();
		pump_server(server, 16);

		String pending;
		send_raw(third, "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n");
		RawResponse response;
		REQUIRE(read_raw_response(server, third, pending, response));
		CHECK(response.status == 200);
		CHECK(server->get_connection_count() <= 2);
	}

	server->stop();
	memdelete(handler);
	memdelete(server);
}

TEST_CASE("[HTTPServer] A connection that stops making progress is dropped") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_port(0);
	server->set_connection_timeout_seconds(0.05);
	REQUIRE(server->listen() == OK);
	const int port = server->get_listening_port();
	REQUIRE(port > 0);

	RecordingRouteHandler *handler = memnew(RecordingRouteHandler);
	server->route("GET", "/hello", callable_mp(handler, &RecordingRouteHandler::handle));

	SUBCASE("A socket that never sends anything is dropped") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		REQUIRE(server->get_connection_count() == 1);

		OS::get_singleton()->delay_usec(120000);
		pump(server, client, 4);
		CHECK(server->get_connection_count() == 0);
	}

	SUBCASE("A request that announces a body and then stalls is dropped") {
		// The slow-loris shape: the framing is valid, so the connection would otherwise wait for the
		// promised bytes for as long as the process runs.
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		send_raw(client, "POST /hello HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4096\r\n\r\nab");
		pump(server, client, 4);
		REQUIRE(server->get_connection_count() == 1);

		OS::get_singleton()->delay_usec(120000);
		pump(server, client, 4);
		CHECK(server->get_connection_count() == 0);
	}

	SUBCASE("A kept-alive socket that goes idle is dropped") {
		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());

		String pending;
		send_raw(client, "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n");
		RawResponse response;
		REQUIRE(read_raw_response(server, client, pending, response));
		CHECK(response.status == 200);

		OS::get_singleton()->delay_usec(120000);
		pump(server, client, 4);
		CHECK(server->get_connection_count() == 0);
	}

	SUBCASE("A zero timeout leaves an idle socket alone") {
		server->set_connection_timeout_seconds(0.0);

		Ref<StreamPeerTCP> client = connect_raw(server, port);
		REQUIRE(client.is_valid());
		REQUIRE(server->get_connection_count() == 1);

		OS::get_singleton()->delay_usec(120000);
		pump(server, client, 4);
		CHECK(server->get_connection_count() == 1);
	}

	server->stop();
	memdelete(handler);
	memdelete(server);
}

} // namespace TestHTTPServer

#endif // MODULE_HTTP_SERVER_ENABLED
