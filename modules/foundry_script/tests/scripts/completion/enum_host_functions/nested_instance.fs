extends Object

enum Status:
	READY = 1
	DONE = 2

	func label(prefix: String = "") -> String:
		return prefix

	async func refresh() -> String:
		return ""

	static func parse(value: String) -> Status:
		return READY

	static async func load(value: String) -> Status:
		return DONE

func test(status: Status) -> void:
	status.➡
	pass
