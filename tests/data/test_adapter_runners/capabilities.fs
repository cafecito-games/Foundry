extends ScriptRunner

# Minimal Foundry Test Adapter Protocol runner used by the engine transport tests.
# It only proves that runner arguments arrive unchanged and that a runner-created
# artifact and return value survive the `project test` host: exact argument
# mismatches are reported as protocol exit code 2.

func run(args: PackedStringArray) -> int:
	if args.size() != 4:
		return 2
	if args[0] != "adapter" or args[1] != "capabilities" or args[2] != "--output":
		return 2
	var file := FileAccess.open(args[3], FileAccess.WRITE)
	if file == null:
		return 2
	var document: String = "{\"protocol\":\"foundry-test-adapter\",\"supported_versions\":[1],"
	document += "\"framework\":{\"id\":\"foundry.transport_fixture\","
	document += "\"name\":\"Transport Fixture\",\"version\":\"1.0.0\"},\"extensions\":[]}\n"
	file.store_string(document)
	file.close()
	return 0
