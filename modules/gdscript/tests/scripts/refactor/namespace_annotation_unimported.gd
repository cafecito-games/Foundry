namespace refactor.gameplay
extends Node

func make() -> void:
	var character = refactor.characters.RefactorNsBaseCharacter.new()
	character.free()
