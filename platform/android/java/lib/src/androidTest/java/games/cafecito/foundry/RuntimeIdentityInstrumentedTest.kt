package games.cafecito.foundry

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class RuntimeIdentityInstrumentedTest {
	@Test
	fun compatibilityIdentityAndCanonicalTypesAreAvailable() {
		assertFalse(BuildConfig.FOUNDRY_BINDINGS_VERSION.isBlank())
		assertFalse(BuildConfig.FOUNDRY_ENGINE_VERSION.isBlank())
		assertEquals(40, BuildConfig.FOUNDRY_ENGINE_REVISION.length)
		assertTrue(BuildConfig.FOUNDRY_JNI_CONTRACT_VERSION > 0)

		val dictionary = Dictionary()
		dictionary["runtime"] = "foundry"
		assertEquals("foundry", dictionary["runtime"])
	}
}
