extends Node

var pending: Coroutine[String]

func schedule(work: Coroutine[String]) -> void:
	pending = work

async func fetch() -> String:
	return "done"

func use_fetch() -> void:
	var work: Coroutine[String] = fetch()
	schedule(work)
