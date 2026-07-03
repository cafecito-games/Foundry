extends ScriptTestRunner

signal proceed

async func run(args: PackedStringArray) -> int:
	await proceed
	var x: int = 1
	x /= 0
	return 0
