## Global enum documentation.
##
## Additional enum details.
enum_name LspGlobalEnum:
	## Alpha documentation.
	ALPHA = 0
	BETA = ALPHA + 1
	GAMMA = 7

	## Formats a global enum value.
	func label(prefix: String = "") -> String:
		return prefix

	## Parses a global enum value.
	static func parse(text: String) -> LspGlobalEnum:
		return ALPHA

	## Loads a global enum value asynchronously.
	static async func load(text: String) -> String:
		return text
