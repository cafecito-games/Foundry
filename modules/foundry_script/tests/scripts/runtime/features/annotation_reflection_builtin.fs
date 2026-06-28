# godot.reflection surfaces Godot's built-in annotations (@export, @export_range, @onready,
# @rpc, @tool, ...) as FSAnnotation metadata alongside custom annotations, distinguished
# by the is_builtin flag and preserving source order.
@tool
namespace cafecito.builtin_reflect_demo
extends Node

annotation marker targets METHOD, VARIABLE

@export_range(0, 100) var ranged: int = 5

@export var exported: String = "x"

@marker
@onready var hybrid_var: Node = self

@rpc("any_peer", "reliable")
@marker
func networked() -> void:
	pass

func test() -> void:
	# Built-in variable annotation carries its positional arguments and is tagged is_builtin.
	var ranged_annotations := godot.reflection.get_variable_annotations(self, "ranged")
	print(ranged_annotations.size())
	print(ranged_annotations[0].name)
	print(ranged_annotations[0].qualified_name)
	print(ranged_annotations[0].builtin)
	print(ranged_annotations[0].args[0])
	print(ranged_annotations[0].args[1])
	print(ranged_annotations[0].kwargs.size())

	# A custom and a built-in annotation coexist on one variable, in source order.
	var hybrid := godot.reflection.get_variable_annotations(self, "hybrid_var")
	print(hybrid.size())
	print(hybrid[0].name, " ", hybrid[0].builtin)
	print(hybrid[1].name, " ", hybrid[1].builtin)

	# Method annotations: built-in @rpc then custom @marker, in source order.
	var method_annotations := godot.reflection.get_method_annotations(self, "networked")
	print(method_annotations.size())
	print(method_annotations[0].name, " ", method_annotations[0].builtin)
	print(method_annotations[1].name, " ", method_annotations[1].builtin)
	print(method_annotations[0].args.size())

	# Script-configuration annotations (@tool, @icon, @static_unload) are applied by the parser and
	# not retained on the AST, so they are intentionally not surfaced as class annotations.
	print(godot.reflection.get_class_annotations(self).size())

	# has_annotation / get_annotation match built-in names like custom ones.
	print(godot.reflection.has_annotation(self, "exported", "export", "variable"))
	print(godot.reflection.get_annotation(self, "ranged", "export_range", "variable").builtin)

	# Built-in annotations are embedded in property descriptors.
	for property in godot.reflection.get_properties(self):
		if str(property["name"]) == "exported":
			var annotations: Array = property["annotations"]
			print(annotations.size())
			print(annotations[0].name, " ", annotations[0].builtin)
