namespace cafecito.discovery_demo

annotation suite(name: String = "") targets CLASS
annotation timeout(seconds: float) targets METHOD
annotation inject targets PARAMETER

@suite(name = "Inventory Suite")
class InventorySuite extends RefCounted:
	@timeout(2.5)
	func test_add_item(@inject item: String) -> void:
		pass
