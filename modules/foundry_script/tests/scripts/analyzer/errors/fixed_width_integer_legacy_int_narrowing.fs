func test():
	# Code written before the widths existed stored 64-bit values in the legacy spelling. Migrating
	# such a slot to a declared narrow width is where the diagnostic has to appear.
	var old_int_range: long = 2147483648L
	var migrated: uint = old_int_range
	print(migrated)
