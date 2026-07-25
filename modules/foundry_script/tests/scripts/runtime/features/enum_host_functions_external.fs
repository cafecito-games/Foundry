#skip-compiled-bytecode
# #1120 removes this sentinel after enum host function tables persist in compiled bytecode.

const _REMOTE_ENUM_FILE = preload("enum_host_functions_external.notest.fs")

func test() -> void:
	print(Issue1119RemoteStatus.INFO.name())
	print(Issue1119RemoteStatus.WARN.name())
	print(Issue1119RemoteStatus.ERROR.name())
	print(Issue1119RemoteStatus.parse("warn").name())
	print(Issue1119RemoteStatus.parse("error").name())
