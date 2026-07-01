using System;

namespace Godot.SourceGenerators.Sample
{
    public partial class AllWriteOnly : FoundryObject
    {
        private bool _writeOnlyBackingField = false;
        public bool WriteOnlyProperty { set => _writeOnlyBackingField = value; }
    }
}
