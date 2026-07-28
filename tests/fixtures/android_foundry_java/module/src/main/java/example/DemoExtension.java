package example;

import games.cafecito.foundry.annotations.FoundryClass;
import games.cafecito.foundry.annotations.FoundryInitialization;
import games.cafecito.foundry.annotations.FoundryMethod;
import games.cafecito.foundry.annotations.InitializationLevel;
import games.cafecito.foundry.generated.classes.Node;
import games.cafecito.foundry.runtime.FoundryBindingContext;
import games.cafecito.foundry.runtime.ObjectLease;

/** Minimal processor-generated extension used by the Android exporter integration tests. */
@FoundryClass(base = Node.class, name = "DemoExtension")
@FoundryInitialization(InitializationLevel.SCENE)
public final class DemoExtension extends Node {
    public DemoExtension(FoundryBindingContext context, ObjectLease lease) {
        super(context, lease);
    }

    @FoundryMethod(name = "callback_probe")
    public long callbackProbe(long value) {
        return value + 1;
    }
}
