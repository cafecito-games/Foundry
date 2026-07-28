@warning_ignore("mixed_namespace_directory")
class_name TestExportStorageUnaffectedByTupleTaggedUnionGuard

tuple Vec2(x: float, y: float)

enum Message:
	Quit
	Move(x: int, y: int)

@export_storage var position: Vec2 = Vec2(1.0, 2.0)
@export_storage var last_message: Message
@export_storage var history: Array[Message] = []

func test():
	for property in get_property_list():
		if str(property.name).begins_with("position") or str(property.name).begins_with("last_message") or str(property.name).begins_with("history"):
			Utils.print_property_extended_info(property)
