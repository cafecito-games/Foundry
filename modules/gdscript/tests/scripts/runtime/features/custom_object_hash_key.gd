class Key:
	extends RefCounted

	var id: int
	var label: String

	func _init(p_id: int, p_label: String) -> void:
		id = p_id
		label = p_label

	func _equals(other: Variant) -> bool:
		return other is Key and id == other.id and label == other.label

	func _hash_code() -> int:
		return hash("%s:%s" % [id, label])


class CollisionKey:
	extends Key

	func _init(p_id: int, p_label: String) -> void:
		super(p_id, p_label)

	func _hash_code() -> int:
		return 42


func test():
	var typed_lookup: Dictionary[Key, String] = {
		Key.new(1234, "abc"): "Hello",
		Key.new(5678, "def"): "Hola",
	}

	print(typed_lookup[Key.new(1234, "abc")])
	print(typed_lookup.has(Key.new(5678, "def")))
	print(typed_lookup.get(Key.new(0, "missing"), "missing"))
	print(Key.new(1, "same") == Key.new(1, "same"))
	print(Key.new(1, "same") != Key.new(2, "same"))

	var collisions: Dictionary[CollisionKey, String] = {
		CollisionKey.new(1, "one"): "one",
		CollisionKey.new(2, "two"): "two",
	}

	print(collisions[CollisionKey.new(1, "one")])
	print(collisions[CollisionKey.new(2, "two")])

	var identity_key := RefCounted.new()
	var other_identity_key := RefCounted.new()
	var identity_lookup: Dictionary[RefCounted, String] = {
		identity_key: "same",
	}

	print(identity_lookup.has(identity_key))
	print(identity_lookup.has(other_identity_key))
