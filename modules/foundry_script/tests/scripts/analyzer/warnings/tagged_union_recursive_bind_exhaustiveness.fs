# A value bound from a self-referential payload field carries the union's complete case set, so a
# non-exhaustive nested "match" over it is reported exactly like one over a declared parameter.
enum Chain:
	End
	Link(next: Chain)
	Branch(children: Array[Chain])

func from_parameter(node: Chain) -> int:
	match node:
		Chain.End:
			return 1
	return 0

func from_direct_bind(node: Chain) -> int:
	match node:
		Chain.Link(var next):
			match next:
				Chain.End:
					return 1
	return 0

func from_collection_bind(node: Chain) -> int:
	match node:
		Chain.Branch(var children):
			match children[0]:
				Chain.End:
					return 1
	return 0

func from_nested_bind(node: Chain) -> int:
	match node:
		Chain.Link(var next):
			match next:
				Chain.Link(var deeper):
					match deeper:
						Chain.End:
							return 1
	return 0

func test():
	print(from_parameter(Chain.End))
	print(from_direct_bind(Chain.Link(Chain.End)))
	print(from_collection_bind(Chain.Branch([Chain.End])))
	print(from_nested_bind(Chain.Link(Chain.Link(Chain.End))))
