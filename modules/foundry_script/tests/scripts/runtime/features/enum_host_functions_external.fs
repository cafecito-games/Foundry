#skip-compiled-bytecode
# #1120 removes this sentinel after enum host function tables persist in compiled bytecode.

const _REMOTE_ENUM_FILE = preload("enum_host_functions_external.notest.fs")

func test() -> void:
	var waiting: Issue1119RemoteStatus = Issue1119RemoteStatus.parse(false)
	var ready: Issue1119RemoteStatus = Issue1119RemoteStatus.parse(true)
	print(waiting.describe())
	print(ready.describe())
