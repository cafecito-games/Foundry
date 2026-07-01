using Godot;

public partial class Generic<T> : FoundryObject
{
    private int _field;
}

// Generic again but different generic parameters
public partial class {|GD0003:Generic|}<T, R> : FoundryObject
{
    private int _field;
}

// Generic again but without generic parameters
public partial class {|GD0003:Generic|} : FoundryObject
{
    private int _field;
}
