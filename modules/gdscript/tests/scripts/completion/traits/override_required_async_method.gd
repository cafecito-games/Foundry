trait Contract:
	abstract func required_action() -> void
	abstract async func required_async() -> void

class Worker:
	uses Contract
	async func ➡
