class_name LspTraitSignatureHelp

trait Damageable:
	func describe(amount: int, label: String) -> String:
		return label

class Base:
	uses Damageable

class Player extends Base:
	pass

trait ShadowDamageable:
	func describe_shadow(amount: int) -> int:
		return amount

class ShadowBase:
	func describe_shadow(amount: int) -> int:
		return amount

class ShadowPlayer extends ShadowBase:
	uses ShadowDamageable

func run(player: Player, shadow_player: ShadowPlayer) -> void:
	player.describe(1, "hit")
	shadow_player.describe_shadow(1)
