# A whole-file generic tagged union is reachable by its global name, a namespace-qualified chain,
# an imported namespace alias, and the declaring script handle.
import generic_union_fixture

const Provider = preload("./generic_tagged_union_global_values.notest.fs")

func test():
	var direct: GlobalResult[int, String] = GlobalResult[int, String].Ok(1)
	var qualified = generic_union_fixture.GlobalResult[String, int].Ok("two")
	var imported: GlobalResult[int, String] = GlobalResult[int, String].Err("three")
	var preloaded: GlobalResult[int, String] = GlobalResult[int, String].Ok(4)
	print(direct)
	print(qualified)
	print(imported)
	print(preloaded)
	print(Provider.GlobalResult)
