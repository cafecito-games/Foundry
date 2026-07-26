package games.cafecito.foundry.plugin;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class FoundryPluginProtocolInstrumentedTest {
	@Test
	public void canonicalPluginProtocolIsAvailableToAndroidConsumers() {
		assertEquals(
				"Example",
				FoundryPluginRegistry.getPluginNameFromMetadata(
						"games.cafecito.foundry.plugin.v1.Example"));
		assertNull(
				FoundryPluginRegistry.getPluginNameFromMetadata(
						"org.godotengine.plugin.v1.Example"));
	}
}
