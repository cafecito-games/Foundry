# A parameter bounded directly by itself remains a genuine cycle; only the owning union's exact open
# vector canonicalizes to its already-published open identity.
enum Invalid[T: T]:
	Leaf
	Node(value: T)
