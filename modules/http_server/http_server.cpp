/**************************************************************************/
/*  http_server.cpp                                                       */
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

#include "http_server.h"

#include "http_file_serve.h"

#include "core/io/stream_peer_tls.h"

void HTTPServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("listen", "tls_options"), &HTTPServer::listen, DEFVAL(Ref<TLSOptions>()));
	ClassDB::bind_method(D_METHOD("stop"), &HTTPServer::stop);
	ClassDB::bind_method(D_METHOD("is_listening"), &HTTPServer::is_listening);
	ClassDB::bind_method(D_METHOD("get_listening_port"), &HTTPServer::get_listening_port);
	ClassDB::bind_method(D_METHOD("route", "method", "path", "handler"), &HTTPServer::route);
	ClassDB::bind_method(D_METHOD("mount_files", "url_prefix", "root_dir"), &HTTPServer::mount_files);
	ClassDB::bind_method(D_METHOD("poll"), &HTTPServer::poll);
	ClassDB::bind_method(D_METHOD("set_port", "port"), &HTTPServer::set_port);
	ClassDB::bind_method(D_METHOD("get_port"), &HTTPServer::get_port);
	ClassDB::bind_method(D_METHOD("set_bind_address", "bind_address"), &HTTPServer::set_bind_address);
	ClassDB::bind_method(D_METHOD("get_bind_address"), &HTTPServer::get_bind_address);
	ClassDB::bind_method(D_METHOD("set_emit_for_all", "emit_for_all"), &HTTPServer::set_emit_for_all);
	ClassDB::bind_method(D_METHOD("is_emitting_for_all"), &HTTPServer::is_emitting_for_all);
	ClassDB::bind_method(D_METHOD("set_max_connections", "max_connections"), &HTTPServer::set_max_connections);
	ClassDB::bind_method(D_METHOD("get_max_connections"), &HTTPServer::get_max_connections);
	ClassDB::bind_method(D_METHOD("set_max_request_body_bytes", "max_request_body_bytes"), &HTTPServer::set_max_request_body_bytes);
	ClassDB::bind_method(D_METHOD("get_max_request_body_bytes"), &HTTPServer::get_max_request_body_bytes);
	ClassDB::bind_method(D_METHOD("set_max_header_count", "max_header_count"), &HTTPServer::set_max_header_count);
	ClassDB::bind_method(D_METHOD("get_max_header_count"), &HTTPServer::get_max_header_count);
	ClassDB::bind_method(D_METHOD("set_max_header_line_bytes", "max_header_line_bytes"), &HTTPServer::set_max_header_line_bytes);
	ClassDB::bind_method(D_METHOD("get_max_header_line_bytes"), &HTTPServer::get_max_header_line_bytes);
	ClassDB::bind_method(D_METHOD("set_max_header_block_bytes", "max_header_block_bytes"), &HTTPServer::set_max_header_block_bytes);
	ClassDB::bind_method(D_METHOD("get_max_header_block_bytes"), &HTTPServer::get_max_header_block_bytes);
	ClassDB::bind_method(D_METHOD("set_connection_timeout_seconds", "connection_timeout_seconds"), &HTTPServer::set_connection_timeout_seconds);
	ClassDB::bind_method(D_METHOD("get_connection_timeout_seconds"), &HTTPServer::get_connection_timeout_seconds);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "port", PROPERTY_HINT_RANGE, "0,65535,1"), "set_port", "get_port");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "bind_address"), "set_bind_address", "get_bind_address");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emit_for_all"), "set_emit_for_all", "is_emitting_for_all");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_connections", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), "set_max_connections", "get_max_connections");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_request_body_bytes", PROPERTY_HINT_RANGE, "1,67108864,1,or_greater"), "set_max_request_body_bytes", "get_max_request_body_bytes");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_header_count", PROPERTY_HINT_RANGE, "1,255,1"), "set_max_header_count", "get_max_header_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_header_line_bytes", PROPERTY_HINT_RANGE, "1,65536,1,or_greater"), "set_max_header_line_bytes", "get_max_header_line_bytes");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_header_block_bytes", PROPERTY_HINT_RANGE, "1,1048576,1,or_greater"), "set_max_header_block_bytes", "get_max_header_block_bytes");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "connection_timeout_seconds", PROPERTY_HINT_RANGE, "0,600,0.1,or_greater"), "set_connection_timeout_seconds", "get_connection_timeout_seconds");

	ADD_SIGNAL(MethodInfo("request_received",
			PropertyInfo(Variant::OBJECT, "request", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "foundry.http.server.HTTPRequest"),
			PropertyInfo(Variant::OBJECT, "response", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "foundry.http.server.HTTPResponse")));
}

void HTTPServer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			poll();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			// Nothing polls the server outside the tree, so leaving it listening would strand the
			// socket and every open connection.
			stop();
		} break;
	}
}

void HTTPServer::set_port(int p_port) {
	ERR_FAIL_COND_MSG(p_port < 0 || p_port > 65535, "Port must be between 0 and 65535.");
	port = p_port;
}

int HTTPServer::get_port() const {
	return port;
}

void HTTPServer::set_bind_address(const String &p_bind_address) {
	ERR_FAIL_COND_MSG(p_bind_address.is_empty(), "The bind address cannot be empty.");
	bind_address = p_bind_address;
}

String HTTPServer::get_bind_address() const {
	return bind_address;
}

void HTTPServer::set_emit_for_all(bool p_emit_for_all) {
	emit_for_all = p_emit_for_all;
}

bool HTTPServer::is_emitting_for_all() const {
	return emit_for_all;
}

void HTTPServer::set_max_connections(int p_max_connections) {
	ERR_FAIL_COND_MSG(p_max_connections < 1, "The connection cap must be at least 1.");
	max_connections = p_max_connections;
}

int HTTPServer::get_max_connections() const {
	return max_connections;
}

void HTTPServer::set_max_request_body_bytes(int p_max_request_body_bytes) {
	ERR_FAIL_COND_MSG(p_max_request_body_bytes < 1, "The request body cap must be at least 1 byte.");
	limits.max_request_body_bytes = p_max_request_body_bytes;
}

int HTTPServer::get_max_request_body_bytes() const {
	return limits.max_request_body_bytes;
}

void HTTPServer::set_max_header_count(int p_max_header_count) {
	ERR_FAIL_COND_MSG(p_max_header_count < 1 || p_max_header_count > HTTPServerConnection::MAX_HEADER_FIELD_CEILING,
			vformat("The header field cap must be between 1 and %d.", HTTPServerConnection::MAX_HEADER_FIELD_CEILING));
	limits.max_header_count = p_max_header_count;
}

int HTTPServer::get_max_header_count() const {
	return limits.max_header_count;
}

void HTTPServer::set_max_header_line_bytes(int p_max_header_line_bytes) {
	ERR_FAIL_COND_MSG(p_max_header_line_bytes < 1, "The header line cap must be at least 1 byte.");
	limits.max_header_line_bytes = p_max_header_line_bytes;
}

int HTTPServer::get_max_header_line_bytes() const {
	return limits.max_header_line_bytes;
}

void HTTPServer::set_max_header_block_bytes(int p_max_header_block_bytes) {
	ERR_FAIL_COND_MSG(p_max_header_block_bytes < 1, "The header block cap must be at least 1 byte.");
	limits.max_header_block_bytes = p_max_header_block_bytes;
}

int HTTPServer::get_max_header_block_bytes() const {
	return limits.max_header_block_bytes;
}

void HTTPServer::set_connection_timeout_seconds(double p_connection_timeout_seconds) {
	ERR_FAIL_COND_MSG(p_connection_timeout_seconds < 0.0, "The connection timeout cannot be negative.");
	limits.timeout_seconds = p_connection_timeout_seconds;
}

double HTTPServer::get_connection_timeout_seconds() const {
	return limits.timeout_seconds;
}

Error HTTPServer::listen(const Ref<TLSOptions> &p_tls_options) {
	ERR_FAIL_COND_V_MSG(tcp_server->is_listening(), ERR_ALREADY_IN_USE, "The server is already listening.");

	if (p_tls_options.is_valid()) {
		ERR_FAIL_COND_V_MSG(!p_tls_options->is_server(), ERR_INVALID_PARAMETER,
				"HTTPS needs server-side TLS options; build them with TLSOptions.server().");
		ERR_FAIL_COND_V_MSG(!StreamPeerTLS::is_available(), ERR_UNAVAILABLE,
				"HTTPS is not available in this build.");
	}

	// `"*"` is the wildcard address rather than a parsed one, so it is accepted even though
	// `IPAddress::is_valid()` reports false for it.
	const IPAddress address = IPAddress(bind_address);
	ERR_FAIL_COND_V_MSG(!address.is_valid() && !address.is_wildcard(), ERR_INVALID_PARAMETER,
			vformat("\"%s\" is not a valid bind address.", bind_address));

	const Error err = tcp_server->listen(static_cast<uint16_t>(port), address);
	if (err != OK) {
		return err;
	}

	// Assigned only once the listener is up, so a failed bind never leaves a stale mode behind and a
	// plaintext `listen()` always clears any options a previous HTTPS run set.
	tls_options = p_tls_options;

	set_process_internal(true);
	return OK;
}

void HTTPServer::stop() {
	for (Ref<HTTPServerConnection> &connection : connections) {
		connection->close();
	}
	connections.clear();
	tcp_server->stop();
	tls_options.unref();
	set_process_internal(false);
}

bool HTTPServer::is_listening() const {
	return tcp_server->is_listening();
}

int HTTPServer::get_listening_port() const {
	return tcp_server->is_listening() ? tcp_server->get_local_port() : -1;
}

void HTTPServer::route(const String &p_method, const String &p_path, const Callable &p_callable) {
	ERR_FAIL_COND_MSG(p_method.is_empty(), "A route needs an HTTP method.");
	ERR_FAIL_COND_MSG(!p_path.begins_with("/"), "A route path must start with \"/\".");
	ERR_FAIL_COND_MSG(!p_callable.is_valid(), "A route needs a valid handler.");

	Route new_route;
	new_route.method = p_method.to_upper();
	new_route.path = p_path;
	new_route.callable = p_callable;
	routes.push_back(new_route);
}

void HTTPServer::mount_files(const String &p_url_prefix, const String &p_root_dir) {
	ERR_FAIL_COND_MSG(!p_url_prefix.begins_with("/"), "A mount prefix must start with \"/\".");

	const String root = HTTPFileServe::canonicalize_directory(p_root_dir);
	ERR_FAIL_COND_MSG(root.is_empty(), vformat("\"%s\" is not a directory that can be served.", p_root_dir));

	FileMount mount;
	mount.prefix = p_url_prefix;
	while (mount.prefix.ends_with("/")) {
		mount.prefix = mount.prefix.left(-1);
	}
	mount.root = root;
	mounts.push_back(mount);
}

int HTTPServer::get_mount_count() const {
	return static_cast<int>(mounts.size());
}

int HTTPServer::get_connection_count() const {
	return static_cast<int>(connections.size());
}

void HTTPServer::poll() {
	if (!tcp_server->is_listening()) {
		return;
	}

	// A connection past the cap is deliberately left in the listen queue rather than taken and
	// refused: it then costs nothing here, and it is served as soon as a slot frees, which the
	// connection timeout guarantees will happen.
	while (connections.size() < uint32_t(max_connections) && tcp_server->is_connection_available()) {
		Ref<StreamPeerTCP> peer = tcp_server->take_connection();
		if (peer.is_null()) {
			break;
		}
		peer->set_no_delay(true);

		Ref<HTTPServerConnection> connection;
		connection.instantiate();
		connection->accept(peer, limits, tls_options);
		connections.push_back(connection);
	}

	// A handler runs script code and may call `stop()`, which clears `connections`. Iterating a
	// copy keeps every connection of this pass alive and keeps the indices valid.
	const LocalVector<Ref<HTTPServerConnection>> pass = connections;
	for (const Ref<HTTPServerConnection> &connection : pass) {
		if (connection->is_closed()) {
			continue;
		}
		connection->poll();
		if (connection->get_state() == HTTPServerConnection::STATE_READY) {
			_dispatch(connection);
		}
	}

	uint32_t index = connections.size();
	while (index > 0) {
		index--;
		if (connections[index]->is_closed()) {
			connections.remove_at(index);
		}
	}
}

void HTTPServer::_dispatch(const Ref<HTTPServerConnection> &p_connection) {
	const Ref<HTTPServerRequest> request = p_connection->get_request();
	ERR_FAIL_COND(request.is_null());

	Ref<HTTPResponse> response;
	response.instantiate();

	// Resolution ladder: static-file mount, then route, then the `request_received` signal, then a
	// default `404`. Mounts sit ahead of routes, so a mount can never be shadowed by a route.
	//
	// The first mount whose prefix matches claims the path; when it holds no file there the request
	// carries on down the ladder with the response still uncommitted, and a refusal ends here.
	bool resolved_by_mount = false;
	for (const FileMount &mount : mounts) {
		String relative;
		if (request->get_path() == mount.prefix) {
			relative = "/";
		} else if (request->get_path().begins_with(mount.prefix + "/")) {
			relative = request->get_path().substr(mount.prefix.length());
		} else {
			continue;
		}

		resolved_by_mount = HTTPFileServe::serve(mount.root, relative, request, response) == HTTPFileServe::RESULT_RESOLVED;
		break;
	}

	// The match is copied out before the handler runs, because a handler is free to register more
	// routes and reallocate the route storage under an iterator.
	Callable handler;
	String matched_method;
	String matched_path;
	if (!resolved_by_mount) {
		for (const Route &candidate : routes) {
			if (candidate.method == request->get_method() && candidate.path == request->get_path()) {
				handler = candidate.callable;
				matched_method = candidate.method;
				matched_path = candidate.path;
				break;
			}
		}
	}

	const bool matched_route = handler.is_valid();
	if (matched_route) {
		const Variant request_argument = request;
		const Variant response_argument = response;
		const Variant *arguments[2] = { &request_argument, &response_argument };
		Variant result;
		Callable::CallError call_error;
		handler.callp(arguments, 2, result, call_error);
		if (call_error.error != Callable::CallError::CALL_OK) {
			ERR_PRINT(vformat("Route handler for \"%s %s\" failed: %s", matched_method, matched_path,
					Variant::get_callable_error_text(handler, arguments, 2, call_error)));
		}

		if (p_connection->is_closed()) {
			// The handler closed the server, and with it this connection. There is nothing left to
			// write, and the poll pass drops the connection on its next prune.
			return;
		}
	}

	if ((!resolved_by_mount && !matched_route) || emit_for_all) {
		// The catch-all step. Connected scripts see every unclaimed request, and every request at all
		// once `emit_for_all` is set, which is the observer case: a routed or mounted response has
		// already committed its body by then, so a receiver's own send is refused.
		emit_signal(SNAME("request_received"), request, response);

		if (p_connection->is_closed()) {
			// A receiver is script code just like a route handler, so it may have stopped the server.
			return;
		}
	}

	if (!response->is_sent()) {
		// Nothing claimed the request, or a handler returned without committing a body.
		response->set_status(404);
		response->send_string("Not Found");
	}

	p_connection->begin_response(response);
}

HTTPServer::HTTPServer() {
	tcp_server.instantiate();
}

HTTPServer::~HTTPServer() {
	stop();
}
