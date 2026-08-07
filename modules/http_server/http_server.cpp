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

void HTTPServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("listen"), &HTTPServer::listen);
	ClassDB::bind_method(D_METHOD("stop"), &HTTPServer::stop);
	ClassDB::bind_method(D_METHOD("is_listening"), &HTTPServer::is_listening);
	ClassDB::bind_method(D_METHOD("get_listening_port"), &HTTPServer::get_listening_port);
	ClassDB::bind_method(D_METHOD("route", "method", "path", "handler"), &HTTPServer::route);
	ClassDB::bind_method(D_METHOD("poll"), &HTTPServer::poll);
	ClassDB::bind_method(D_METHOD("set_port", "port"), &HTTPServer::set_port);
	ClassDB::bind_method(D_METHOD("get_port"), &HTTPServer::get_port);
	ClassDB::bind_method(D_METHOD("set_bind_address", "bind_address"), &HTTPServer::set_bind_address);
	ClassDB::bind_method(D_METHOD("get_bind_address"), &HTTPServer::get_bind_address);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "port", PROPERTY_HINT_RANGE, "0,65535,1"), "set_port", "get_port");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "bind_address"), "set_bind_address", "get_bind_address");
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

Error HTTPServer::listen() {
	ERR_FAIL_COND_V_MSG(tcp_server->is_listening(), ERR_ALREADY_IN_USE, "The server is already listening.");

	// `"*"` is the wildcard address rather than a parsed one, so it is accepted even though
	// `IPAddress::is_valid()` reports false for it.
	const IPAddress address = IPAddress(bind_address);
	ERR_FAIL_COND_V_MSG(!address.is_valid() && !address.is_wildcard(), ERR_INVALID_PARAMETER,
			vformat("\"%s\" is not a valid bind address.", bind_address));

	const Error err = tcp_server->listen(static_cast<uint16_t>(port), address);
	if (err != OK) {
		return err;
	}

	set_process_internal(true);
	return OK;
}

void HTTPServer::stop() {
	for (Ref<HTTPServerConnection> &connection : connections) {
		connection->close();
	}
	connections.clear();
	tcp_server->stop();
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

int HTTPServer::get_connection_count() const {
	return static_cast<int>(connections.size());
}

void HTTPServer::poll() {
	if (!tcp_server->is_listening()) {
		return;
	}

	while (tcp_server->is_connection_available()) {
		Ref<StreamPeerTCP> peer = tcp_server->take_connection();
		if (peer.is_null()) {
			break;
		}
		peer->set_no_delay(true);

		Ref<HTTPServerConnection> connection;
		connection.instantiate();
		connection->accept(peer);
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

	// The match is copied out before the handler runs, because a handler is free to register more
	// routes and reallocate the route storage under an iterator.
	Callable handler;
	String matched_method;
	String matched_path;
	for (const Route &candidate : routes) {
		if (candidate.method == request->get_method() && candidate.path == request->get_path()) {
			handler = candidate.callable;
			matched_method = candidate.method;
			matched_path = candidate.path;
			break;
		}
	}

	if (handler.is_valid()) {
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
	}

	if (p_connection->is_closed()) {
		// The handler closed the server, and with it this connection. There is nothing left to
		// write, and the poll pass drops the connection on its next prune.
		return;
	}

	if (!response->is_sent()) {
		// Nothing claimed the request, or a handler returned without committing a body. The rest of
		// the resolution ladder is added by later work; an unclaimed request is a `404` for now.
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
