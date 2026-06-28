trait Locked:
	final func _process(_delta: float) -> void:
		pass

class Base extends Node:
	uses Locked
	func _process(_delta: float) -> void:
		pass

class Derived extends Base:
	func ➡
