extends Node

func use(value: LspGlobalEnum) -> void:
	var parsed = LspGlobalEnum.parse("beta")
	var text = value.label(">")
	var pending = LspGlobalEnum.load("gamma")
