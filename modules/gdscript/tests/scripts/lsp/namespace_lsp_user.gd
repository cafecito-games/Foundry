namespace lsp.characters
import lsp.characters
class_name LspNamespaceUser
extends Node

var same_namespace: LspBaseCharacter
var imported_child: controllers.LspMyCharacterController
var fully_qualified: lsp.characters.LspBaseCharacter

func make() -> void:
	var same := LspBaseCharacter.new()
	var qualified := lsp.characters.LspBaseCharacter.new()
