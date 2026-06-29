@warning_ignore("missing_tool")
extends "./non_tool_extends_tool.notest.fs"

@warning_ignore("missing_tool")
class InnerClass extends "./non_tool_extends_tool.notest.fs":
	pass

func test():
	pass
