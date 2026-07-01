using Godot;
using Godot.Collections;

public partial class ExportDiagnostics_GD0107_OK : Node
{
    [Export]
    public Node NodeField;

    [Export]
    public Node[] SystemArrayOfNodesField;

    [Export]
    public Array<Node> FoundryArrayOfNodesField;

    [Export]
    public Dictionary<Node, string> FoundryDictionaryWithNodeAsKeyField;

    [Export]
    public Dictionary<string, Node> FoundryDictionaryWithNodeAsValueField;

    [Export]
    public Node NodeProperty { get; set; }

    [Export]
    public Node[] SystemArrayOfNodesProperty { get; set; }

    [Export]
    public Array<Node> FoundryArrayOfNodesProperty { get; set; }

    [Export]
    public Dictionary<Node, string> FoundryDictionaryWithNodeAsKeyProperty { get; set; }

    [Export]
    public Dictionary<string, Node> FoundryDictionaryWithNodeAsValueProperty { get; set; }
}

public partial class ExportDiagnostics_GD0107_KO : Resource
{
    [Export]
    public Node {|GD0107:NodeField|};

    [Export]
    public Node[] {|GD0107:SystemArrayOfNodesField|};

    [Export]
    public Array<Node> {|GD0107:FoundryArrayOfNodesField|};

    [Export]
    public Dictionary<Node, string> {|GD0107:FoundryDictionaryWithNodeAsKeyField|};

    [Export]
    public Dictionary<string, Node> {|GD0107:FoundryDictionaryWithNodeAsValueField|};

    [Export]
    public Node {|GD0107:NodeProperty|} { get; set; }

    [Export]
    public Node[] {|GD0107:SystemArrayOfNodesProperty|} { get; set; }

    [Export]
    public Array<Node> {|GD0107:FoundryArrayOfNodesProperty|} { get; set; }

    [Export]
    public Dictionary<Node, string> {|GD0107:FoundryDictionaryWithNodeAsKeyProperty|} { get; set; }

    [Export]
    public Dictionary<string, Node> {|GD0107:FoundryDictionaryWithNodeAsValueProperty|} { get; set; }
}
