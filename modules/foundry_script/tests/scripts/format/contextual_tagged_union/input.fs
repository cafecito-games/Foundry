func consume(value: Variant) -> void:
	print(value)

func run(condition: bool, subject: int) -> void:
	var single =  .Ok( 1 )
	var empty =   .None
	var elements = [ .Ok( 1 ) , .Err( "x" ) ]
	var mapping = { .Ok( 1 ) :  .Err( "x" ) , "key" :  .None }
	var casted =   .Ok( 1 )  as  Variant
	var chosen = .Ok( 1 )  if condition  else .Err( "no" )
	var nested =  .Ok( .Some( 1 ) )
	consume(  .Ok( 1 )  )
	match subject:
		.Ok( payload ):
			print(payload)
		.None:
			pass
		_:
			pass
	if subject is  .Ok( bound ):
		print(bound)
