# Custom (non-built-in) annotation usages are preserved by the parser instead of
# being rejected as unknown. Resolution and validation happen later in the analyzer,
# so at this stage the annotations are passive and have no runtime effect.

@suite(name = "Combat System")
@tags("gameplay", "slow")
class Combat:
	@fixture
	var world: int = 0

	@marker
	@timeout(10.0)
	@cases(provider = "crit_rows", count = 3)
	func crit(row: int) -> int:
		return row

@repeatable("a")
@repeatable("b")
@mixed(1, 2, label = "x")
func decorated() -> String:
	return "ok"

func test():
	print(decorated())
	print(Combat.new().crit(7))
