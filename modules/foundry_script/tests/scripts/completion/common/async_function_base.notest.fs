extends Node

async func func_of_async() -> String:
	return ""

static async func func_of_static_async() -> String:
	return ""

func func_of_await_only() -> String:
	await get_tree().process_frame
	return ""
