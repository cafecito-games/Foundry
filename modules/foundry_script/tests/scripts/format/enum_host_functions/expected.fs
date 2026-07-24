enum LogLevel:
	# diagnostic value
	INFO = 1  # inline value

	## Returns a display label.
	@rpc("any_peer")
	func label() -> String:
		return "info"

	static async func parse(text: String) -> int:
		return text.length()

enum Utility:
	## Resets all state.
	@rpc
	static func reset() -> void:
		pass
	# keep this enum-body comment

enum Empty:
	pass



class Host:
	enum Nested:
		READY = 1

		## Loads the ready value.
		static async func load() -> int:
			return READY
