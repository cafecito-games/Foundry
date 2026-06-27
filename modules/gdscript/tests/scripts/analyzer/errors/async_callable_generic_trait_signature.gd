# A generic trait method requiring an AsyncCallable signature is not satisfied by a synchronous
# Callable implementation of the same shape: the async marker is part of alpha-equivalence.
trait Listener:
	abstract func on[U](handler: AsyncCallable[[U], void]) -> void


class Impl:
	uses Listener

	func on[V](handler: Callable[[V], void]) -> void:
		pass
