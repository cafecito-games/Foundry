extends ScriptTestRunner

signal proceed

async func run(args: PackedStringArray) -> int:
	await proceed
	return 1
