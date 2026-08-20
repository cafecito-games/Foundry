# A tuple alternative owes the same carrier guarantee a tuple slot owes standing alone: the value the
# slot keeps is the canonical read-only Array, not the caller's mutable one. Aliasing the source would
# make the guarantee true only at the instant of the store -- an append afterwards would leave the slot
# holding an Array of the wrong arity, which neither alternative describes. The plain tuple parameter
# is called on the identical value so the two cannot drift apart.
abstract class Pairs:
	abstract func pair() -> (int, int) | String


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

	# A proxy's handler return crosses the same boundary and owes the same carrier, so it is pinned
	# here too: the fast path that hands an already-acceptable value straight back must not skip the
	# canonicalization for a tuple alternative.
	var handler_source: Array = [4, 5]
	var service: Object = create_proxy_dynamic(Pairs, func(_method: StringName, _args: Array) -> Variant:
		return handler_source)
	var proxy_slot: Variant = service.call("pair")
	print("proxy read only ", proxy_slot.is_read_only())
	handler_source.append(6)
	print("proxy slot unchanged ", proxy_slot)
