extends ScriptTestRunner

func run(args: PackedStringArray) -> int:
	if args.size() != 2:
		return 2
	if args[0] != "--filter" or args[1] != "inventory":
		return 2
	return 0
