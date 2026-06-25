# A class type parameter forwarded into create_proxy[T] is reified onto the
# instance, so it is supported. A *method* type parameter is not reified at
# runtime, so forwarding it into create_proxy[T] is still rejected with a pointer
# to the dynamic fallback.
class Factory:
	func handle(_method_name: StringName, _args: Array) -> Variant:
		return null

	func build[T]() -> T:
		return create_proxy[T](handle)
