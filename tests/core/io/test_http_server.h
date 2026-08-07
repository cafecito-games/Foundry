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
#include "modules/http_server/http_server_request.h"

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

} // namespace TestHTTPServer

#endif // MODULE_HTTP_SERVER_ENABLED
