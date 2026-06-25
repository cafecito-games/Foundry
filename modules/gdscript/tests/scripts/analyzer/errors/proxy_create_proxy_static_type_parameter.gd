# A class type parameter is reified per instance, so it cannot be forwarded into
# create_proxy[T] from a static function: there is no instance to read the binding
# from. This is rejected with a pointer to the dynamic fallback.
class Mock[T]:
	static func handle(_method_name: StringName, _args: Array) -> Variant:
		return null

	static func build() -> T:
		return create_proxy[T](handle)
