var top_level = (
	1 + 2
)  # top close

var nested_call = consume((
	3 + 4
)  # call close
)

var array_value = [
	(
		5 + 6
	)  # array close
]
var dictionary_value = {
	"key": (
		7 + 8
	)  # dictionary close
}
var tuple_value = (
	(
		9 + 10
	)  # tuple close
	,
	11,
)

var nested_grouping = ((
	12 + 13
)  # inner close
)
var nested_close_comments = ((
	14 + 15
)  # nested inner close
)  # nested outer close

var content_and_close = (
	16 + 17  # content note
)  # content close
var full_line_before_close = (
	18 + 19
	# full line before close
)  # full-line close
var canonical_full_line = (
	20 + 21
	# canonical close fallback
)
var content_and_close_same_line = (
	27 + 28)  # same-line close

var binary_continuation = (
	22 + 23
) + 24  # binary tail
var postfix_continuation = (
	[25, 26]
).size()  # postfix tail
