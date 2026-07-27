package example;

import games.cafecito.foundry.runtime.FoundryModuleDescriptor;
import games.cafecito.foundry.runtime.FoundryModuleProvider;
import java.util.List;

/** Minimal reflection-free provider used by the Android exporter integration tests. */
public final class DemoExtension implements FoundryModuleProvider {
    public static final FoundryModuleProvider PROVIDER = new DemoExtension();

    private static final FoundryModuleDescriptor DESCRIPTOR =
            new FoundryModuleDescriptor(
                    FoundryModuleDescriptor.CURRENT_FORMAT,
                    "demo",
                    "example.DemoExtension",
                    "85e91174c1a8a48629223d6459bb2ef595ad1da405b2ce88435c24fe221aec51",
                    "1",
                    "1",
                    "1",
                    List.of());

    private DemoExtension() {}

    @Override
    public FoundryModuleDescriptor descriptor() {
        return DESCRIPTOR;
    }
}
