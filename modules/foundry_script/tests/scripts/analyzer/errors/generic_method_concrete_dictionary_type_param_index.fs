# A concretely-keyed dictionary (`Dictionary[int, String]`) indexed by an unrelated method type
# parameter `K` is still rejected: only a dictionary whose key type is itself an erased type parameter
# relaxes the static key-type check.
func lookup[K](data: Dictionary[int, String], key: K) -> String:
	return data[key]
