extends Node
var handler: AsyncCallable[[int],String]
var nullable: AsyncCallable[[int],String]?
var bare: AsyncCallable
async static func fetch() -> String:
	return "ok"
