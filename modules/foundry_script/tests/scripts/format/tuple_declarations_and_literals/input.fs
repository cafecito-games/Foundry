# Tuple declarations, tuple types and tuple values in every printer position.
tuple Vec2(x:float,y:float) # canonical pair

tuple Unnamed( int , String )

tuple Wide(
	first:int,
	# a leading comment inside the field list
	second:String,
	third:Vec2,
)


func take(pair:(int,String),point:Vec2)->(int,String):
	var literal:=(1,"two")
	var nested := ( ( 1 , 2 ) , point )
	var wide:Wide=Wide(1,"two",point)
	var indexed:=literal.0+nested.0.1
	var named:=point.x+point.y
	print(pair.0,pair.1,wide.third.x,indexed,named)
	return literal


func multiline() -> (int, String):
	return (
		1,
		"two",
	)
