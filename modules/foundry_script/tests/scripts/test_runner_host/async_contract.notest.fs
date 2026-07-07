extends ScriptRunner

signal go

async func run(args: PackedStringArray) -> int:
	await go
	return 0
