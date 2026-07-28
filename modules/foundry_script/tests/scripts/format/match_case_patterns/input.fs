enum FormatPatternMessage:
 Quit
 Move(x:int,y:int)
 Write(text:String)

func test():
 var message:FormatPatternMessage=FormatPatternMessage.Move(1,2)
 match message:
  FormatPatternMessage.Quit:
   print("quit")
  FormatPatternMessage.Move( 0 , y ):
   print(y)
  FormatPatternMessage.Move(x,var y) when x>0:
   print(x+y)
  FormatPatternMessage.Move(_,_):
   print("move")
  FormatPatternMessage.Write(text):
   print(text)
  FormatPatternMessage.Move((0),_):
   print("grouped zero")
