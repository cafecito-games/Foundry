enum FormatCaseMessage:
 Quit
 Move(x:int,y:int)

func test():
 var message:FormatCaseMessage=FormatCaseMessage.Quit
 if message is FormatCaseMessage.Move( x , _ ):
  print(x)
 while message is FormatCaseMessage.Move(a,b):
  print(a+b)
  break
 print(message is FormatCaseMessage.Quit)
 print(message is FormatCaseMessage)
