trait Requires:
	abstract func handle() -> void

trait Provides:
	final func handle() -> void:
		pass

class Worker:
	uses Requires, Provides
	func ➡
