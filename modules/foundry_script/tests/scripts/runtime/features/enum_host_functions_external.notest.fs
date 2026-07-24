enum_name Issue1119RemoteStatus:
	WAITING = 31
	READY = 32

	func describe() -> String:
		return "remote:" + str(self)

	static func parse(ready: bool) -> Self:
		return READY if ready else WAITING
