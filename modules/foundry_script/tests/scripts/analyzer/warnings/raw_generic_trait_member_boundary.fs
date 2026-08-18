# A generic trait named without type arguments is raw for exactly the same reason a generic class is:
# nothing bound its parameter, so a member typed by that parameter is unchecked wherever it crosses
# into a typed slot. A trait applied with an argument keeps every one of those crossings silent.
trait Storage[U]:
	abstract func fetch() -> U

	abstract func store(value: U) -> void

	func count() -> int:
		return 0


class IntStorage:
	uses Storage[int]

	var held: int = 0

	func fetch() -> int:
		return held

	func store(value: int) -> void:
		held = value


func want_int(value: int) -> void:
	print(value)


# We don't want to execute it because of errors, just analyze.
func no_exec_raw_trait(storage: Storage) -> void:
	want_int(storage.fetch())
	var _local: int = storage.fetch()
	storage.store(5)
	want_int(storage.count()) # No warning: `count()` names no parameter.


func no_exec_raw_trait_return(storage: Storage) -> int:
	return storage.fetch()


func no_exec_specialized_trait(storage: Storage[int]) -> void:
	want_int(storage.fetch()) # No warning.
	storage.store(5) # No warning.


func no_exec_applied_trait(storage: IntStorage) -> void:
	want_int(storage.fetch()) # No warning.
	storage.store(5) # No warning.


func test():
	pass
