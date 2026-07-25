enum_name CompletionGlobalEnum:
	VALUE_A = 0
	VALUE_B = 7

	## Formats this value.
	func label(prefix: String = "") -> String:
		return prefix

	## Loads the label asynchronously.
	async func refresh() -> String:
		return ""

	## Parses a value.
	static func parse(value: String) -> CompletionGlobalEnum:
		return VALUE_A

	## Loads a value asynchronously.
	static async func load(value: String) -> CompletionGlobalEnum:
		return VALUE_B
