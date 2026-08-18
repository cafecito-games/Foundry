# A `Self` a non-final implementer wrote stays open, but the structure written around it does not. The
# runtime record keeps the composite's shell, arity, and known children and marks only the `Self`
# subtree unknown, so a gradual store agrees with what the analyzer already keeps. Every row routes
# through `Variant` so the analyzer is out of the way and the runtime record answers on its own.
trait OscKeeper[T]:
	func label() -> String:
		return "keeper"


trait OscStoring[T] uses OscKeeper[T]:
	func store_label() -> String:
		return "storing"


class OscPair[A, B]:
	pass


class OscDeclared:
	uses OscKeeper[OscPair[int, Self]]


class OscSupertrait:
	uses OscStoring[OscPair[int, Self]]


class OscArray:
	uses OscKeeper[Array[Self]]


class OscDictionary:
	uses OscKeeper[Dictionary[String, Self]]


class OscBare:
	uses OscKeeper[Self]


class OscConcrete:
	uses OscKeeper[OscPair[int, String]]


class OscRetro:
	pass


extend OscRetro uses OscKeeper[OscPair[int, Self]]:
	pass


func test() -> void:
	var declared: Variant = OscDeclared.new()
	var declared_slot: OscKeeper[OscPair[int, String]] = declared
	print("declared store: ", declared_slot != null)
	print("declared test: ", declared is OscKeeper[OscPair[int, String]])

	var supertrait: Variant = OscSupertrait.new()
	var supertrait_slot: OscKeeper[OscPair[int, String]] = supertrait
	print("supertrait store: ", supertrait_slot != null)
	print("supertrait test: ", supertrait is OscKeeper[OscPair[int, String]])

	var typed_array: Variant = OscArray.new()
	var typed_array_slot: OscKeeper[Array[String]] = typed_array
	print("array store: ", typed_array_slot != null)

	var typed_dictionary: Variant = OscDictionary.new()
	var typed_dictionary_slot: OscKeeper[Dictionary[String, String]] = typed_dictionary
	print("dictionary store: ", typed_dictionary_slot != null)

	var bare: Variant = OscBare.new()
	var bare_slot: OscKeeper[OscPair[int, String]] = bare
	print("bare store: ", bare_slot != null)
	print("bare test: ", bare is OscKeeper[OscPair[int, String]])

	var concrete: Variant = OscConcrete.new()
	var concrete_slot: OscKeeper[OscPair[int, String]] = concrete
	print("concrete store: ", concrete_slot != null)
	print("concrete test: ", concrete is OscKeeper[OscPair[int, String]])

	var retro: Variant = OscRetro.new()
	var retro_slot: OscKeeper[OscPair[int, String]] = retro
	print("retro store: ", retro_slot != null)
	print("retro test: ", retro is OscKeeper[OscPair[int, String]])
