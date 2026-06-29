# Async trait methods carry METHOD_FLAG_ASYNC into the implementer's reflection:
# both a flattened concrete async trait method and the class's implementation of a
# required async trait method report as async, and awaiting them works at runtime.
extends RefCounted
uses RemoteLoadable

trait RemoteLoadable:
	abstract async func fetch() -> String

	async func fetch_twice() -> String:
		return await fetch() + await fetch()

async func fetch() -> String:
	return "x"

func test():
	var signatures: Array[String] = []
	for method in get_method_list():
		if str(method.name) in ["fetch", "fetch_twice"]:
			signatures.append(Utils.get_method_signature(method))
	signatures.sort()
	for signature in signatures:
		print(signature)
	print(await fetch_twice())
