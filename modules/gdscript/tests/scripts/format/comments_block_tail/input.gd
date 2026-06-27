func f():
	var a = 1
	# indented trailing stays in f
## doc comment for g
func g():
	if cond:
		do_a()
		# stays inside if body
	for i in items:
		do_b()
		# stays inside for body
	do_c()
