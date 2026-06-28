var a = [
	1,  # inline after item
	# keep this between items
	2,
]
var d = {
	"x": 1,
	# mid dict
	"y": 2,
}  # trailing on close
func call_it():
	foo(
		1,
		# arg note
		2,
	)
