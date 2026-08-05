namespace generic_union_fixture.consumer
import generic_union_fixture

const Provider = preload("./generic_tagged_union_global_values.notest.fs")

func test():
	var imported: GlobalResult[int, String] = GlobalResult[int, String].Ok(10)
	var qualified = generic_union_fixture.GlobalResult[int, String].Err("eleven")
	var preloaded: GlobalResult[int, String] = GlobalResult[int, String].Ok(12)
	print(imported)
	print(qualified)
	print(preloaded)
	print(Provider.GlobalResult)
