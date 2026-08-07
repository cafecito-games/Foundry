# Member resolution follows the bound chain, so the valid `send()` call resolves against `FrmMessage`
# even though `U`'s own bound is `T`. The hard final-bound rejection is narrower: only a directly
# final-bounded parameter is treated as closed, so the missing `deliver()` keeps the soft treatment
# instead of being rejected the way it would be on an `FrmMessage`-typed receiver.
final class FrmMessage:
	func send() -> void:
		pass


class Box[T: FrmMessage, U: T]:
	var value: U

	func poke() -> void:
		value.deliver()

	func nudge() -> void:
		value.send()
