# Inside its own declaration a generic tagged union names itself either bare or with its exact open
# parameter vector, directly or through a container, and every form is the same open self type.
enum TokenTree[T]:
	Leaf(value: T)
	Link(next: TokenTree)
	Branch(children: Array[TokenTree])
	Explicit(children: Array[TokenTree[T]])

	static func singleton(value: T) -> TokenTree:
		return TokenTree.Leaf(value)

	static func wrap(node: TokenTree[T]) -> TokenTree:
		return TokenTree.Link(node)
