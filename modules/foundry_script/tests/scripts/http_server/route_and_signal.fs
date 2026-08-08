import foundry.http.server

# Proves a Foundry Script program can stand up an HTTP server, register a route,
# and observe the `request_received` signal. A routed request reaches its handler
# and keeps the signal quiet; an unrouted request falls through to the signal,
# which answers it. Both halves run over a real loopback connection.

var route_calls: int = 0
var route_method: String = ""
var route_path: String = ""
var route_query_value: String = ""

var signal_calls: int = 0
var signal_path: String = ""

func _on_hello(request: foundry.http.server.HTTPRequest, response: foundry.http.server.HTTPResponse) -> void:
	route_calls += 1
	route_method = request.get_method()
	route_path = request.get_path()
	route_query_value = str(request.get_query().get("q", ""))
	response.set_header("Content-Type", "text/plain")
	response.send_string("hello")

func _on_request_received(request: foundry.http.server.HTTPRequest, response: foundry.http.server.HTTPResponse) -> void:
	signal_calls += 1
	signal_path = request.get_path()
	response.set_status(404)
	response.send_string("no route")

func test() -> void:
	var server := foundry.http.server.HTTPServer.new()
	# Port 0 asks the operating system for a free loopback port.
	server.set_port(0)
	print(server.listen() == OK)
	var port := server.get_listening_port()

	server.route("GET", "/hello", _on_hello)
	@warning_ignore("return_value_discarded")
	server.connect("request_received", _on_request_received)

	var routed := HTTPTestClient.new()
	routed.fetch(server, port, "/hello?q=world")
	print(routed.ok)
	print(routed.status)
	print(routed.body)
	print(route_calls)
	print(route_method)
	print(route_path)
	print(route_query_value)
	# A routed request must not also wake the signal while emit_for_all is off.
	print(signal_calls)

	var unrouted := HTTPTestClient.new()
	unrouted.fetch(server, port, "/missing")
	print(unrouted.status)
	print(unrouted.body)
	print(signal_calls)
	print(signal_path)

	server.stop()
	server.free()
