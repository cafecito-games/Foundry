# A typed container enforces exactly one element type at runtime, which a set of alternatives
# cannot supply.
type Scalar = int | uint


func test():
	var values: Array[Scalar] = []
	var by_name: Dictionary[String, Scalar] = {}
	var by_key: Dictionary[Scalar, String] = {}
	var inline: Array[int | uint] = []
	prints(values, by_name, by_key, inline)
