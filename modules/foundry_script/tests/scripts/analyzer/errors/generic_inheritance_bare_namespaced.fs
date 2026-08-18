# A namespaced generic base is named through its qualified chain and is rejected identically.
namespace generic_inheritance_demo.consumer
import generic_inheritance_demo.library

class Bad extends generic_inheritance_demo.library.NamespacedBox:
	pass


func test() -> void:
	print("unreachable")
