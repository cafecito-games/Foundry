# An inherited member's `Self` positions are rebound to the class that inherits them, so an alternative
# naming `Self` means the derived receiver rather than the base that declared the member. A reflective
# write is validated against that rebinding, so a sibling subclass's instance is refused where the
# receiver's own is accepted. Without the rebinding the alternative would stay `Array[Base]` and take
# either one. The values travel through `Variant` so the reflective write is the only thing deciding
# them; a literal written at the call site is answered statically instead.
class Base:
	var link: int | Array[Self] = 0


class Left extends Base:
	var tag = "left"


class Right extends Base:
	var tag = "right"


func test() -> void:
	var left := Left.new()
	var own: Variant = [Left.new()]
	var sibling: Variant = [Right.new()]

	left.set("link", 7)
	print("int alternative ", left.link)
	left.set("link", own)
	var accepted: Variant = left.link
	print("own class accepted ", accepted[0].tag)
	left.set("link", sibling)
	var unchanged: Variant = left.link
	print("sibling refused, slot still holds ", unchanged[0].tag)
