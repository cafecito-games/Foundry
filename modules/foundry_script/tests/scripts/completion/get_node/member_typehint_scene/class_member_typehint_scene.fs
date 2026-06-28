extends Node

const A := preload("res://completion/class_a.notest.fs")

@onready var test: A = $A

func a():
	test.➡
    pass
