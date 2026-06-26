namespace refactor.gameplay
@abstract
extends Node

func make() -> void:
	var character = refactor.characters.RefactorNsBaseCharacter.new()
	character.free()
