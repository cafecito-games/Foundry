extends Node

signal selected(node: Node?, callbacks: Array[Callable[[int], void]])

var maybe_node: Node?
var callback: Callable[[Node?], String]
var payloads: Dictionary[String, Array[int]]
var event: Signal[[String]]

func describe(handler: Callable[[Node?], String], values: Dictionary[String, Array[int]]) -> Signal[[String]]:
	return event

func use_describe() -> void:
	describe(callback, payloads)

async func fetch_description() -> String:
	return "ready"
