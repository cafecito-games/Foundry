# A generic union whose payload names the union itself keeps its specialization through every level:
# the nested value binds as the same applied specialization rather than degrading to the open
# declaration, so a recursive walk sees concrete leaf values.
enum TokenTree[T]:
	Leaf(value: T)
	Branch(children: Array[TokenTree[T]])

func sum_leaves(tree: TokenTree[int]) -> int:
	match tree:
		TokenTree[int].Leaf(var value):
			return value
		TokenTree[int].Branch(var children):
			var total := 0
			for child in children:
				total += sum_leaves(child)
			return total
	return -1

func render(tree: TokenTree[String]) -> String:
	match tree:
		TokenTree[String].Leaf(var value):
			return value.to_upper()
		TokenTree[String].Branch(var children):
			var parts: Array[String] = []
			for child in children:
				parts.append(render(child))
			return "(" + ", ".join(parts) + ")"
	return "unreachable"

func test():
	var leaf := TokenTree[int].Leaf(3)
	var nested := TokenTree[int].Branch([leaf, TokenTree[int].Branch([TokenTree[int].Leaf(4), TokenTree[int].Leaf(5)])])
	print(sum_leaves(leaf))
	print(sum_leaves(nested))

	var words := TokenTree[String].Branch([TokenTree[String].Leaf("a"), TokenTree[String].Leaf("b")])
	print(render(words))
