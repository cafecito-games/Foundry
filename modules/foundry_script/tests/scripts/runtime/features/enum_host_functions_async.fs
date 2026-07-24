#skip-compiled-bytecode
# #1120 removes this sentinel after enum host function tables persist in compiled bytecode.

enum AsyncStatus:
	READY = 41

	async func describe() -> String:
		return "async-instance:" + str(self)

	static async func load() -> Self:
		return READY


func test() -> void:
	var loaded: AsyncStatus = await AsyncStatus.load()
	print(loaded)
	print(await loaded.describe())
