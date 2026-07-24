#skip-compiled-bytecode
# #1120 removes this sentinel after enum host function tables persist in compiled bytecode.

enum Status:
	UNKNOWN = 0
	READY = 1
	DONE = 2

	func describe(prefix: String = "") -> String:
		if self == READY:
			return prefix + "ready"
		return prefix + "done"

	static func parse(text: String) -> Self:
		return READY if text == "ready" else DONE


func test() -> void:
	var ready: Status = Status.parse("ready")
	var done: Status = Status.parse("other")
	print(ready.describe("state:"))
	print(done.describe())
	print(Status.keys())
