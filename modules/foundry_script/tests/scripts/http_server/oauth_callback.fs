import foundry.http.server

# Proves the OAuth redirect flow works from Foundry Script: a callback route reads
# the `code` query parameter off the inbound request and answers with a 302 that
# sends the browser on to the next page. The captured code and the emitted redirect
# are both observed over a real loopback connection.

var seen_code: String = ""
var seen_state: String = ""
var reply_status: int = 0
var reply_location: String = ""

func _on_callback(request: foundry.http.server.HTTPRequest, response: foundry.http.server.HTTPResponse) -> void:
	var query := request.get_query()
	seen_code = str(query.get("code", ""))
	seen_state = str(query.get("state", ""))
	response.redirect("/dashboard?code=" + seen_code.uri_encode(), 302)
	reply_status = response.get_status()
	reply_location = response.get_header("Location")

func test() -> void:
	var server := foundry.http.server.HTTPServer.new()
	server.set_port(0)
	print(server.listen() == OK)
	var port := server.get_listening_port()

	server.route("GET", "/callback", _on_callback)

	var client := HTTPTestClient.new()
	client.fetch(server, port, "/callback?code=abc123&state=xyz")

	# What the handler captured off the request.
	print(seen_code)
	print(seen_state)
	# What the handler committed to the response.
	print(reply_status)
	print(reply_location)
	# What the client read back over the wire.
	print(client.ok)
	print(client.status)
	print(client.location)
	print(client.body == "")

	server.stop()
	server.free()
