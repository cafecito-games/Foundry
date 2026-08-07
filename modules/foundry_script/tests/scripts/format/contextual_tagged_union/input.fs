func run(condition: bool, subject: int) -> void:
	var single =  .Ok( 1 )
	var empty =   .None
	var elements = [ .Ok( 1 ) , .Err( "x" ) ]
	var chosen = .Ok( 1 )  if condition  else .Err( "no" )
	match subject:
		.Ok( payload ):
			print(payload)
		.None:
			pass
		_:
			pass
	if subject is  .Ok( bound ):
		print(bound)
