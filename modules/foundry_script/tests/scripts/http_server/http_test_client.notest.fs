class_name HTTPTestClient

# A tiny loopback client for the HTTP server integration fixtures. The server is
# poll-driven and the fixtures run without a scene tree, so a request only makes
# progress while something pumps `poll()`. This drives the server's poll loop and
# the client together on the calling thread until one complete exchange finishes,
# mirroring how the native round-trip tests drive it in C++.

var ok: bool = false
var status: int = 0
var body: String = ""
var location: String = ""

# Performs one GET against `server` on `port` for `path` (which may carry a query
# string) and records the client-visible result.
func fetch(server: foundry.http.server.HTTPServer, port: int, path: String) -> void:
	var client := HTTPClient.new()
	if client.connect_to_host("127.0.0.1", port) != OK:
		return

	var deadline := int(Time.get_ticks_msec()) + 5000
	var requested := false
	var received := PackedByteArray()

	while int(Time.get_ticks_msec()) < deadline:
		server.poll()

		# Once the body starts arriving, draining it is what advances the client;
		# polling it in that state would treat the peer's close as an error.
		if client.get_status() == HTTPClient.STATUS_BODY:
			received.append_array(client.read_response_body_chunk())
		else:
			@warning_ignore("return_value_discarded")
			client.poll()

		if client.has_response() and status == 0:
			status = client.get_response_code()
			for line: String in client.get_response_headers():
				var separator := line.find(":")
				if separator < 0:
					continue
				if line.substr(0, separator).strip_edges().to_lower() == "location":
					location = line.substr(separator + 1).strip_edges()

		var current := client.get_status()
		if status == 0 and (current == HTTPClient.STATUS_CANT_RESOLVE \
				or current == HTTPClient.STATUS_CANT_CONNECT \
				or current == HTTPClient.STATUS_CONNECTION_ERROR):
			client.close()
			return

		if not requested:
			if current == HTTPClient.STATUS_CONNECTED:
				if client.request(HTTPClient.METHOD_GET, path, PackedStringArray(), "") != OK:
					client.close()
					return
				requested = true
			continue

		if status != 0 and current != HTTPClient.STATUS_REQUESTING and current != HTTPClient.STATUS_BODY:
			if not received.is_empty():
				body = received.get_string_from_utf8()
			ok = true
			client.close()
			return

		OS.delay_msec(1)

	client.close()
