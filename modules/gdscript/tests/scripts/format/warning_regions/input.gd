extends Node
@warning_ignore_start("unused_private_class_variable")
var _unused = 1
@warning_ignore_restore("unused_private_class_variable")
func f():
	@warning_ignore_start("unused_variable")
	var x = 1
	@warning_ignore_restore("unused_variable")
	return 0
