using Godot;

// This works because it inherits from FoundryObject.
[GlobalClass]
public partial class CustomGlobalClass1 : FoundryObject
{

}

// This works because it inherits from an object that inherits from FoundryObject
[GlobalClass]
public partial class CustomGlobalClass2 : Node
{

}

// This raises a GD0401 diagnostic error: global classes must inherit from FoundryObject
[GlobalClass]
public partial class {|GD0401:CustomGlobalClass3|}
{

}
