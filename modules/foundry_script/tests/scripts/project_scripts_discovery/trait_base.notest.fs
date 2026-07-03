class_name InventoryBase
extends RefCounted
uses InventoryTestable

trait InventoryTestable:
	abstract func count_items() -> int

func count_items() -> int:
	return 0
