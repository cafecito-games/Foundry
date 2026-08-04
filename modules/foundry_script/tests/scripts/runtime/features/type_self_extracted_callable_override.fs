# Only the receiver context travels with an extracted callable, never the selection. An unqualified
# reference inside an inherited static body resolves to the declaring class's function, so extracting
# it through a receiver that declares its own function of the same name still means the declaring
# one -- exactly what an unqualified direct call in the same position means. A qualified extraction
# off the receiver names the receiver's own function, again matching the direct call.
class Base:
	static func spawn() -> Base:
		return Base.new()

	static func call_direct() -> Base:
		return spawn()

	static func extract() -> Callable:
		return spawn


class Derived:
	extends Base

	static func spawn() -> Base:
		return Derived.new()


func test() -> void:
	print(Derived.call_direct() is Derived, " ", Derived.extract().call() is Derived)
	print(Derived.spawn() is Derived, " ", (Derived.spawn as Callable).call() is Derived)
