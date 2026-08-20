# A `Self` written as an explicit type argument of a generic union application was written in the
# calling frame, so the payload position it fills is governed by ordinary type equality there and
# admits the frame's own `self`. A `Self` written in the declaration's own payload schema stays the
# construction receiver's contract. The two are the same name once the application has pasted one
# into the other, so the spelling transform runs on the declaration's open schema and the
# application's type arguments are applied after it, keeping the owners per position.
class Receiver:
	enum Box[T]:
		Empty
		Full(value: T, owner: Self)
		Maybe(value: T?)
		Carried(items: T)

	func construct(other: Receiver) -> void:
		match other.Box[Self].Full(self, other):
			Box[Self].Full(value, owner):
				prints("instance base", value == self, owner == other)
			_:
				prints("instance base", "unmatched")
		match Box[Self].Full(self, self):
			Box[Self].Full(value, owner):
				prints("frame", value == self, owner == self)
			_:
				prints("frame", "unmatched")
		var handle_owner := Receiver.new()
		match Receiver.Box[Self].Full(self, handle_owner):
			Box[Self].Full(value, owner):
				prints("class handle", value == self, owner == handle_owner)
			_:
				prints("class handle", "unmatched")
		# Substitution ORs nullability, so `T?` with `T := Self` is a caller-relative `Self?` and keeps
		# the contract's `null` admission.
		match other.Box[Self].Maybe(null):
			Box[Self].Maybe(value):
				prints("nullable", value == null)
			_:
				prints("nullable", "unmatched")
		# A `Self` riding inside a carrier argument is still the caller's, and the literal is typed
		# against the field exactly as an ordinary `Array[Self]` annotation types one.
		match other.Box[Array[Self]].Carried([self]):
			Box[Array[Self]].Carried(items):
				prints("carrier", items.size(), items[0] == self)
			_:
				prints("carrier", "unmatched")


func test() -> void:
	var receiver := Receiver.new()
	receiver.construct(Receiver.new())
