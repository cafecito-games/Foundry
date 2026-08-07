# A `static func` whose signature references `Self`, called through an instance receiver rather than
# the class handle, resolves `Self` against the receiver. The instance names a class exactly as a
# class handle does, so the call dispatches just as `Child.take(...)` would instead of failing for
# want of a receiver.
#
# `Self` binds to the receiver's runtime class, matching how an instance method reached through the
# same receiver resolves it. Analysis substitutes the receiver's *static* type, which is always a
# base of that runtime class, so the static promise stays an upper bound of what the frame sees.
@warning_ignore_start("static_called_on_instance")


class Base:
	static func take(value: Self) -> void:
		print("parameter is Child: %s" % [value is Child])

	static func take_rest(...values: Array[Self]) -> void:
		print("rest tail is Child: %s" % [values.get_typed_script() == Child])

	static func make() -> Self:
		return Self.new()


class Child extends Base:
	pass


func test() -> void:
	var child := Child.new()
	child.take(Child.new())
	child.take_rest(Child.new(), Child.new())
	print("instance factory is Child: %s" % [child.make() is Child])

	# A receiver held through a base-typed local still resolves `Self` against its runtime class.
	var as_base: Base = Child.new()
	as_base.take(Child.new())
	print("base-typed receiver factory is Child: %s" % [as_base.make() is Child])

	# The class-handle form is unchanged.
	Child.take(Child.new())
	print("class handle factory is Child: %s" % [Child.make() is Child])
	Base.take(Base.new())
	print("base class handle factory is Base: %s" % [Base.make() is Base])
