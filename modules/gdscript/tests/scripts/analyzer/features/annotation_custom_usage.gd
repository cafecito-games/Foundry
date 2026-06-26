# Custom annotations declared and used in the same namespace resolve without an import and
# accept positional, named, default, variadic, stacked, and repeated arguments.
namespace cafecito.usage

annotation suite(name: String = "") targets CLASS
annotation test targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation tags(...names: String) targets METHOD, CLASS
annotation fixture targets VARIABLE

@suite(name = "Combat")
@tags("gameplay")
class CombatTests:
	@fixture
	var world: int

	@test
	@timeout(10.0)
	@tags("slow", "integration")
	@tags("flaky")
	func crit_table() -> void:
		pass

func test() -> void:
	pass
