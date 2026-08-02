# A dependent handle bound (`T: Type[U]`) is compared as a handle, not unwrapped to `U`'s own upper
# bound, so a handle argument for a type satisfying `U` is accepted while an instance argument is not.
class Holder[U: Node, T: Type[U]]:
	var value: T


func test() -> void:
	var holder := Holder[Button, Type[Button]].new()
	holder.value = Button
	print(holder.value == Button)
	print("dependent handle bound ok")
