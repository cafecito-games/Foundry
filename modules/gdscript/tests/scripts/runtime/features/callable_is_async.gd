# Callable.is_async() reports whether the target method is a coroutine, both for
# methods declared `async` and for methods whose body is inferred to be a coroutine.
# Async lambdas and async @rpc method references report as async too. Bind/unbind
# delegation is covered by the C++ test in tests/core/variant/test_callable.h.
# Extends Node so the @rpc annotation is valid (RPC requires a Node).
extends Node

async func declared_async() -> void:
	pass

func body_inferred_async() -> void:
	await declared_async()

func synchronous() -> int:
	return 1

@rpc async func remote_async() -> void:
	pass

@rpc func remote_sync() -> void:
	pass

func test():
	print(Callable(self, "declared_async").is_async())
	print(Callable(self, "body_inferred_async").is_async())
	print(Callable(self, "synchronous").is_async())

	var async_lambda := func(): await declared_async()
	print(async_lambda.is_async())

	var sync_lambda := func(): return 1
	print(sync_lambda.is_async())

	var async_rpc: Callable = remote_async
	print(async_rpc.is_custom(), " ", async_rpc.is_async())

	var sync_rpc: Callable = remote_sync
	print(sync_rpc.is_async())

	print(Callable().is_async())
