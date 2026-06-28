async func immediate() -> int:
	return 1

func test():
	@warning_ignore("return_value_discarded")
	immediate()
