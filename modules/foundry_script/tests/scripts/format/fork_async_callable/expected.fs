extends Node
var handler: AsyncCallable[[int], String]
var nullable: AsyncCallable[[int], String]?
var bare: AsyncCallable


static async func fetch() -> String:
	return "ok"
