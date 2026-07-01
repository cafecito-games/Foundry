using System;

namespace Godot.SourceGenerators.Sample;

public partial class NestedClass : FoundryObject
{
    public partial class NestedClass2 : FoundryObject
    {
        public partial class NestedClass3 : FoundryObject
        {
            [Signal]
            public delegate void MySignalEventHandler(string str, int num);

            [Export] private String _fieldString = "foo";
            [Export] private String PropertyString { get; set; } = "foo";

            private void Method()
            {
            }
        }
    }
}
