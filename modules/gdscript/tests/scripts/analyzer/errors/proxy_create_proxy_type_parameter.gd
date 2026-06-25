# Forwarding an enclosing class's type parameter into create_proxy[T] needs the
# reified runtime bindings that are not available yet (tracked separately). Until
# then this is rejected with a pointer to the dynamic fallback, rather than
# emitting code that cannot recover T at runtime.
class Mock[T]:
	func handle(_method_name: StringName, _args: Array) -> Variant:
		return null

	func build() -> T:
		return create_proxy[T](handle)
