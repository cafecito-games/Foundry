# A class that inherits an inner class also inherits that inner class's lexical outer scope, and that
# scope outranks the class's own outer scope. Both same-named `Token` declarations and both
# same-named aliases are in play, so an emitted binding that skipped the inherited outer would print
# "target-outer" instead of failing loudly. An ordinary method of the derived class resolves the same
# names through the same compiler path as the witness.
class BaseOuter:
	class Token:
		func label() -> String:
			return "inherited-outer"

	const TokenAlias = Token

	class Middle:
		class Inner:
			pass


class TargetOuter:
	class Token:
		func label() -> String:
			return "target-outer"

	const TokenAlias = Token

	class Derived extends BaseOuter.Middle.Inner:
		func from_method() -> String:
			return Token.new().label() + "/" + TokenAlias.new().label()


trait Taggable:
	abstract func tag() -> String
	abstract func aliased_tag() -> String


extend TargetOuter.Derived uses Taggable:
	func tag() -> String:
		return Token.new().label()

	func aliased_tag() -> String:
		return TokenAlias.new().label()


func test() -> void:
	var tagged: Taggable = TargetOuter.Derived.new()
	print(tagged.tag())
	print(tagged.aliased_tag())
	print(TargetOuter.Derived.new().from_method())
