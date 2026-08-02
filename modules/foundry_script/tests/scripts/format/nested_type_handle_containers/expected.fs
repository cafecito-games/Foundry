var handles: Array[Type[Node]] = [Node, Button]
var registry: Dictionary[String, Type[Node]] = {"node": Node}
var keyed: Dictionary[Type[Node], String] = {Node: "node"}
var deep: Array[Dictionary[String, Type[Node]]] = []


func lookup(name: String) -> Type[Node]:
	return registry[name]
