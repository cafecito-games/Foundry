extends Node

enum:
	UNIT_NEUTRAL = 0
	#<^^^^^^^^^^ enum:unnamed:neutral -> enum:unnamed:neutral
	UNIT_ENEMY = UNIT_NEUTRAL + 1
	#<^^^^^^^^ enum:unnamed:enemy -> enum:unnamed:enemy
	UNIT_ALLY = UNIT_ENEMY + 1
	#<^^^^^^^ enum:unnamed:ally -> enum:unnamed:ally
enum Named:
#    ^^^^^ enum:named -> enum:named
	THING_1 = 0
	#<^^^^^ enum:named:thing1 -> enum:named:thing1
	THING_2 = THING_1 + 1
	#<^^^^^ enum:named:thing2 -> enum:named:thing2
	ANOTHER_THING = -1
	#<^^^^^^^^^^^ enum:named:thing3 -> enum:named:thing3

func f(arg):
	match arg:
		UNIT_ENEMY: print(UNIT_ENEMY)
		#        |        ^^^^^^^^^^ -> enum:unnamed:enemy
		#<^^^^^^^^ -> enum:unnamed:enemy
		Named.THING_2: print(Named.THING_2)
		#!  | |     |        |   | ^^^^^^^ -> enum:named:thing2
		#   | |     |        ^^^^^ -> enum:named
		#!  | ^^^^^^^ -> enum:named:thing2
		#<^^^ -> enum:named
		_: print(UNIT_ENEMY, Named.ANOTHER_THING)
		#!       |        |  |   | ^^^^^^^^^^^^^ -> enum:named:thing3
		#        |        |  ^^^^^ -> enum:named
		#        ^^^^^^^^^^ -> enum:unnamed:enemy
