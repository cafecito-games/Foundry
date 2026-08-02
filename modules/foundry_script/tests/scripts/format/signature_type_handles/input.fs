signal registered( name : String,factory : Type[ Node ] )

var construct : Callable[ [ Type[Node] ],Node ]
var nested : Callable[[Array[ Type[Node] ]],void]
var handler : Signal[ [ Type[ Node ] ] ]


func identity( factory : Type[Node] ) -> Type[ Node ] :
	return factory
