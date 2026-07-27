enum Message:
 Quit
 Move(x:int,y:int)
 Write(text:String)

 func describe()->String:
  return "message"

enum Small:
 Empty
 Single(value:int,)

enum Multiline:
 Big(
  x:int,
  # y comment
  y:int,
 )  # closing comment
 Small
