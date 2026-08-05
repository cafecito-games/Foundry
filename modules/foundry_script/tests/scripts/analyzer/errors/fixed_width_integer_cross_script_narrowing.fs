# A width declared in another file is enforced exactly like one declared inline: narrowing a member,
# a call result, a call argument, a typed container, or a signal parameter is rejected rather than
# silently accepted because the carrier happens to match.
const Producer = preload("fixed_width_integer_cross_script_narrowing_producer.notest.fs")


func on_reported(_value: uint) -> void:
	pass


func test() -> void:
	var producer := Producer.new()
	var narrow_member: uint = producer.wide_unsigned
	var narrow_result: uint = producer.take_wide(1UL)
	var narrow_list: Array[uint] = producer.wide_unsigned_list
	print(narrow_member, narrow_result, narrow_list, producer.take_narrow(producer.wide_unsigned))
	producer.reported.connect(on_reported)
