namespace refactor.characters
extends Node

func make() -> void:
	var character = RefactorNsBaseCharacter.new()
	character.free()
