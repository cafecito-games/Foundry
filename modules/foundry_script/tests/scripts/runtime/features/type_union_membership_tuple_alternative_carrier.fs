# A tuple alternative owes the same carrier guarantee a tuple slot owes standing alone: the value the
# slot keeps is the canonical read-only Array, not the caller's mutable one. Aliasing the source would
# make the guarantee true only at the instant of the store -- an append afterwards would leave the slot
# holding an Array of the wrong arity, which neither alternative describes. The plain tuple parameter
# is called on the identical value so the two cannot drift apart.
func take_plain(pair: (int, int)) -> Variant:
	return pair


func take_union(pair: (int, int) | String) -> Variant:
	return pair


func test() -> void:
	var mutable: Array = [1, 2]
	var carried: Variant = mutable
	var plain_slot: Variant = take_plain(carried)
	var union_slot: Variant = take_union(carried)

	print("plain read only ", plain_slot.is_read_only())
	print("union read only ", union_slot.is_read_only())

	mutable.append(3)
	print("source after mutation ", mutable)
	print("plain slot unchanged ", plain_slot)
	print("union slot unchanged ", union_slot)

	# The non-tuple alternative is unaffected: a String is stored as it arrived.
	print("string alternative ", take_union("text"))
