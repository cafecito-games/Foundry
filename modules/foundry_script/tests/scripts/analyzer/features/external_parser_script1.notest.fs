extends "external_parser_script1_base.notest.fs"

const External2 = preload("external_parser_script2.notest.fs")
const External1c = preload("external_parser_script1c.notest.fs")

@export var e1c: External1c

var array: Array[External2] = [ External2.new() ]
var baz: int

func get_external2() -> External2:
	return External2.new()
