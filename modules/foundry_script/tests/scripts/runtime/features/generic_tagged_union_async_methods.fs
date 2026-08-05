signal go

enum AsyncOption[T]:
	None
	Some(value: T)

	static async func load(value: T) -> AsyncOption:
		return AsyncOption.Some(value)

	func is_some() -> bool:
		return self is AsyncOption.Some

	async func describe_after(resume_signal: Signal) -> String:
		await resume_signal
		return "async:" + str(self.is_some())

async func _runner() -> void:
	var loaded: AsyncOption[int] = await AsyncOption[int].load(7)
	print(await loaded.describe_after(go))

func test() -> void:
	@warning_ignore("missing_await")
	_runner()
	print("before resume")
	go.emit()
	print("after resume")
