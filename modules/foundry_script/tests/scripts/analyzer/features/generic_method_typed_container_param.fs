# A generic method parameter typed as a container that (transitively) involves a type parameter
# (`Array[T]`, `Dictionary[K, V]`, `Dictionary[K, int]`, `Array[Array[T]]`) erases to an untyped
# container at runtime, so a concrete typed-container argument is accepted rather than rejected by
# the runtime element-type check.
func first_element[T](items: Array[T]) -> T:
	return items[0]


func key_count[K, V](dict: Dictionary[K, V]) -> int:
	return dict.size()


func value_count[K](dict: Dictionary[K, int]) -> int:
	return dict.size()


func first_row[T](rows: Array[Array[T]]) -> int:
	return rows[0].size()


func test() -> void:
	var numbers: Array[int] = [10, 20]
	print(first_element(numbers))

	var table: Dictionary[String, int] = {"a": 1, "b": 2}
	print(key_count(table))
	print(value_count(table))

	var grid: Array[Array[int]] = [[1, 2, 3]]
	print(first_row(grid))
	print("typed container generic ok")
