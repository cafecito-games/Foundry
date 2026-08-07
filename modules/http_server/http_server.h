/**************************************************************************/
/*  http_server.h                                                         */
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

#include "http_server_connection.h"

#include "core/io/tcp_server.h"
#include "core/templates/local_vector.h"
#include "core/variant/callable.h"
#include "scene/main/node.h"

// Registered under `foundry.http.server`, so it is reachable only through its qualified name.
//
// The server owns a listening `TCPServer` and one `HTTPServerConnection` per accepted socket. All
// work happens on the thread that calls `poll()`: while the node is in the tree that is the main
// thread through internal processing, and a headless caller can drive `poll()` itself.
class HTTPServer : public Node {
	FOUNDRY_CLASS(HTTPServer, Node);

	struct Route {
		String method;
		String path;
		Callable callable;
	};

	int port = 8080;
	String bind_address = "127.0.0.1";

	Ref<TCPServer> tcp_server;
	LocalVector<Ref<HTTPServerConnection>> connections;
	LocalVector<Route> routes;

	void _dispatch(const Ref<HTTPServerConnection> &p_connection);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Port `0` asks the operating system for a free port, which `get_listening_port()` reports back.
	void set_port(int p_port);
	int get_port() const;

	// Defaults to the loopback interface, so the server is not reachable off the machine unless a
	// wider address is set on purpose.
	void set_bind_address(const String &p_bind_address);
	String get_bind_address() const;

	// Binds `bind_address` on `port`.
	Error listen();
	// Closes the listener and every open connection. Safe to call when not listening.
	void stop();
	bool is_listening() const;
	// The bound port while listening, or `-1` otherwise.
	int get_listening_port() const;

	// Registers an exact `method` + `path` handler, invoked with `(request, response)`. The first
	// registration for a given pair wins.
	void route(const String &p_method, const String &p_path, const Callable &p_callable);

	// Accepts, parses, dispatches and writes for one pass. Called automatically while the node is in
	// the tree.
	void poll();

	// The number of connections the server currently owns. Native inspection only.
	int get_connection_count() const;

	HTTPServer();
	~HTTPServer();
};
