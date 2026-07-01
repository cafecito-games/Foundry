using Godot;

namespace NamespaceA
{
    partial class SameName : FoundryObject
    {
        private int _field;
    }
}

// SameName again but different namespace
namespace NamespaceB
{
    partial class {|GD0003:SameName|} : FoundryObject
    {
        private int _field;
    }
}
