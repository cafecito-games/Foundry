# A `Dictionary[String, ulong]`'s value slot rides the same `Variant::UINT` carrier as a `uint` slot,
# so only `ContainerType::numeric_type` tells them apart. A script-facing diagnostic that falls
# through to the carrier's name instead of the declared width reports the wrong container type.
func get_value() -> Variant:
	return 1.5

func test():
	var m: Dictionary[String, ulong] = {}
	m["k"] = get_value()
