# A class can extend a self-specialization of a generic base — the F-bounded / CRTP pattern
# `class Sword extends Box[Sword]` — for both a class bound (`RefCounted`) and a trait bound. This
# previously failed with a spurious cyclic-reference error because validating the `Sword` type
# argument against the base's bound re-entered Sword's still-in-progress inheritance resolution.
class Box[T: RefCounted]:
	var value: T


class Node224 extends Box[Node224]:
	var tag := 0


trait Damageable:
	func damage() -> int:
		return 1


class Bounded[T: Damageable]:
	var holder: T


class Sword extends Bounded[Sword]:
	uses Damageable


func test() -> void:
	var node := Node224.new()
	# The inherited `value` member is typed `Node224` (the self argument).
	node.value = Node224.new()
	node.value.tag = 7
	print(node.value.tag)

	var sword := Sword.new()
	print(sword.damage())
	print("self-referential inheritance ok")
