# An alias of a script class binds the instance type as an explicit type argument, not the class
# handle: `resolve_type_alias` returns the expansion with meta-type flags intact, so the use site
# has to demote it exactly as the general identifier fallback demotes a bare class name.
class Widget:
	var name: String = "widget"


type WidgetAlias = Widget


class Box[T]:
	var value: T


func test():
	var box := Box[WidgetAlias].new()
	box.value = Widget.new()
	print(box.value.name)
