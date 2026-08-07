# A type parameter may be bounded by another type parameter. Member resolution follows the bound
# chain to its first non-parameter link, so a `V`-typed receiver resolves against `FrmChainMessage`
# even though `V`'s own bound is `U` and `U`'s bound is `T`. Both method calls and member reads
# follow the same chain; a member that `FrmChainMessage` does not have stays on the soft path.
class FrmChainMessage:
	var label: String = "message"

	func describe() -> String:
		return "message"


class Box[T: FrmChainMessage, U: T, V: U]:
	var direct: T
	var one_step: U
	var two_steps: V

	func describe_direct() -> String:
		return direct.describe()

	func describe_one_step() -> String:
		return one_step.describe()

	func describe_two_steps() -> String:
		return two_steps.describe()

	func read_through_chain() -> String:
		return two_steps.label

	func poke() -> void:
		two_steps.missing_method()


var box: Box[FrmChainMessage, FrmChainMessage, FrmChainMessage]


func test():
	print(box)
	print("dependent type parameter bound chain ok")
