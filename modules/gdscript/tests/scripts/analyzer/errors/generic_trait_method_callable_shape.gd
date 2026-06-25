# A type parameter hidden inside a Callable signature is matched by alpha-equivalence: a required
# `Callable[[U], void]` is not satisfied by an implementation `Callable[[int], void]`.
trait Listener:
	@abstract func on[U](handler: Callable[[U], void]) -> void


class Impl:
	uses Listener

	func on[V](handler: Callable[[int], void]) -> void:
		pass
