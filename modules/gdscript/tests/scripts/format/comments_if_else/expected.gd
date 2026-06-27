func test():
	if a:
		x()
	# between if-body and elif
	elif b:  # inline on elif
		y()
	# between elif-body and else
	else:  # inline on else
		# between else: and first statement
		z()
	if c:
		p()
	# between if-body and else (no elif)
	else:
		q()
