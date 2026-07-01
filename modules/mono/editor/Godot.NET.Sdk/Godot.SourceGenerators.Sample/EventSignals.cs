namespace Godot.SourceGenerators.Sample;

public partial class EventSignals : FoundryObject
{
    [Signal]
    public delegate void MySignalEventHandler(string str, int num);
}
