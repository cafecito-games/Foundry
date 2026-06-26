namespace refactor.gameplay
extends Node

func make() -> void:
	var refactor: int = 1
	var character = make_character()
	character.free()
	print(refactor)

func make_character() -> refactor.characters.RefactorNsBaseCharacter:
	return refactor.characters.RefactorNsBaseCharacter.new()
