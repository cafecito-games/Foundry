using Godot;

// This works because it inherits from FoundryObject and it doesn't have any generic type parameter.
[GlobalClass]
public partial class CustomGlobalClass : FoundryObject
{

}

// This raises a GD0402 diagnostic error: global classes can't have any generic type parameter
[GlobalClass]
public partial class {|GD0402:CustomGlobalClass|}<T> : FoundryObject
{

}
