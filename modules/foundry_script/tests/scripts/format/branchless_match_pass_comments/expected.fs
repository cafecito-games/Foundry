func single(value):
	match value:
		# why
		pass  # note


func multiple(value):
	match value:
		# one
		pass  # two


func bare(value):
	match value:
		pass


func trailing(value):
	match value:
		pass  # note
		# after


func with_branches(value):
	match value:
		pass  # lead
		1:
			return 1
		_:
			return 0
