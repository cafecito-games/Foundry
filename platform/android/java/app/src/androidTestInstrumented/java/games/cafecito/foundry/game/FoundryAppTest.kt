/**************************************************************************/
/*  FoundryAppTest.kt                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

package games.cafecito.foundry.game

import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.os.SystemClock
import android.util.Log
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import games.cafecito.foundry.Foundry
import games.cafecito.foundry.FoundryActivity.Companion.EXTRA_COMMAND_LINE_PARAMS
import games.cafecito.foundry.game.test.FoundryAppInstrumentedTestBridge
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * This instrumented test will launch the `instrumented` version of FoundryApp and run a set of tests against it.
 */
@RunWith(AndroidJUnit4::class)
class FoundryAppTest {

	companion object {
		private val TAG = FoundryAppTest::class.java.simpleName

		private const val FOUNDRY_APP_LAUNCHER_CLASS_NAME = "games.cafecito.foundry.game.FoundryAppLauncher"
		private const val FOUNDRY_APP_CLASS_NAME = "games.cafecito.foundry.game.FoundryApp"

		private val TEST_COMMAND_LINE_PARAMS = arrayOf("This is a test")
		private const val ENGINE_EVENT_TIMEOUT_MS = 30_000L
	}

	private fun waitForMainLoopStarted() {
		assertTrue(
			FoundryAppInstrumentedTestBridge.waitForMainLoopStarted(ENGINE_EVENT_TIMEOUT_MS),
			"Timed out waiting for the Foundry main loop to start."
		)
	}

	private fun waitForRunStatus(foundry: Foundry, expected: Foundry.RunStatus, timeoutMillis: Long): Boolean {
		val deadline = SystemClock.elapsedRealtime() + timeoutMillis
		do {
			if (foundry.runStatus == expected) {
				return true
			}
			SystemClock.sleep(10L)
		} while (SystemClock.elapsedRealtime() < deadline)
		return foundry.runStatus == expected
	}

	private fun resetBridge() {
		FoundryAppInstrumentedTestBridge.reset(
			InstrumentationRegistry.getInstrumentation().targetContext
		)
	}

	/**
	 * Boots the runtime without manifest-based Android plugin discovery.
	 */
	@Test
	fun runtimeBootsWithoutLegacyPluginMetadata() {
		resetBridge()
		ActivityScenario.launch(FoundryApp::class.java).use { scenario ->
			scenario.onActivity { activity ->
				waitForMainLoopStarted()

				val metadata = activity.packageManager
					.getApplicationInfo(activity.packageName, PackageManager.GET_META_DATA)
					.metaData
				assertFalse(metadata.keySet().any { it.contains(".plugin.") })
			}
		}
	}

	/**
	 * Runs the JavaClassWrapper tests via the explicit instrumented test bridge.
	 */
	@Test
	fun runJavaClassWrapperTests() {
		resetBridge()
		ActivityScenario.launch(FoundryApp::class.java).use { scenario ->
			scenario.onActivity { activity ->
				Log.d(TAG, "Waiting for the Foundry main loop to start...")
				waitForMainLoopStarted()

				Log.d(TAG, "Running JavaClassWrapper tests...")
				val testLabel = "javaclasswrapper_tests"
				FoundryAppInstrumentedTestBridge.requestTest(testLabel)
				assertTrue(
					FoundryAppInstrumentedTestBridge.waitForTest(testLabel, ENGINE_EVENT_TIMEOUT_MS),
					"Timed out waiting for $testLabel."
				)
				assertEquals(
					0,
					FoundryAppInstrumentedTestBridge.getTestFailures(testLabel),
					FoundryAppInstrumentedTestBridge.getTestFailureMessage(testLabel)
				)
				Log.d(TAG, "Passed ${FoundryAppInstrumentedTestBridge.getTestPasses(testLabel)} tests")
			}
		}
	}

	/**
	 * Runs file access related tests.
	 */
	@Test
	fun runFileAccessTests() {
		resetBridge()
		ActivityScenario.launch(FoundryApp::class.java).use { scenario ->
			scenario.onActivity { activity ->
				Log.d(TAG, "Waiting for the Foundry main loop to start...")
				waitForMainLoopStarted()

				Log.d(TAG, "Running FileAccess tests...")
				val testLabel = "file_access_tests"
				FoundryAppInstrumentedTestBridge.requestTest(testLabel)
				assertTrue(
					FoundryAppInstrumentedTestBridge.waitForTest(testLabel, ENGINE_EVENT_TIMEOUT_MS),
					"Timed out waiting for $testLabel."
				)
				assertEquals(
					0,
					FoundryAppInstrumentedTestBridge.getTestFailures(testLabel),
					FoundryAppInstrumentedTestBridge.getTestFailureMessage(testLabel)
				)
			}
		}
	}

	/**
	 * Test implicit launch of the Foundry app, and validates this resolves to the `FoundryAppLauncher` activity alias.
	 */
	@Test
	fun testImplicitFoundryAppLauncherLaunch() {
		val implicitLaunchIntent = Intent().apply {
			setPackage(BuildConfig.APPLICATION_ID)
			action = Intent.ACTION_MAIN
			addCategory(Intent.CATEGORY_LAUNCHER)
			putExtra(EXTRA_COMMAND_LINE_PARAMS, TEST_COMMAND_LINE_PARAMS)
		}
		ActivityScenario.launch<FoundryApp>(implicitLaunchIntent).use { scenario ->
			scenario.onActivity { activity ->
				assertEquals(activity.intent.component?.className, FOUNDRY_APP_LAUNCHER_CLASS_NAME)

				val commandLineParams = activity.intent.getStringArrayExtra(EXTRA_COMMAND_LINE_PARAMS)
				assertNull(commandLineParams)
			}
		}
	}

	/**
	 * Test explicit launch of the Foundry app via its activity-alias launcher, and validates it resolves properly.
	 */
	@Test
	fun testExplicitFoundryAppLauncherLaunch() {
		val explicitIntent = Intent().apply {
			component = ComponentName(BuildConfig.APPLICATION_ID, FOUNDRY_APP_LAUNCHER_CLASS_NAME)
			putExtra(EXTRA_COMMAND_LINE_PARAMS, TEST_COMMAND_LINE_PARAMS)
		}
		ActivityScenario.launch<FoundryApp>(explicitIntent).use { scenario ->
			scenario.onActivity { activity ->
				assertEquals(activity.intent.component?.className, FOUNDRY_APP_LAUNCHER_CLASS_NAME)

				val commandLineParams = activity.intent.getStringArrayExtra(EXTRA_COMMAND_LINE_PARAMS)
				assertNull(commandLineParams)
			}
		}
	}

	/**
	 * Test explicit launch of the `FoundryApp` activity.
	 */
	@Test
	fun testExplicitFoundryAppLaunch() {
		val explicitIntent = Intent().apply {
			component = ComponentName(BuildConfig.APPLICATION_ID, FOUNDRY_APP_CLASS_NAME)
			putExtra(EXTRA_COMMAND_LINE_PARAMS, TEST_COMMAND_LINE_PARAMS)
		}
		ActivityScenario.launch<FoundryApp>(explicitIntent).use { scenario ->
			scenario.onActivity { activity ->
				assertEquals(activity.intent.component?.className, FOUNDRY_APP_CLASS_NAME)

				val commandLineParams = activity.intent.getStringArrayExtra(EXTRA_COMMAND_LINE_PARAMS)
				assertNotNull(commandLineParams)
				assertTrue(commandLineParams.contentEquals(TEST_COMMAND_LINE_PARAMS))
			}
		}
	}

	/**
	 * Validate that the back press does not quit the game when 'quit_on_go_back' is disabled.
	 */
	@Test
	fun testGameNotQuittingOnBackPress() {
		resetBridge()
		ActivityScenario.launch(FoundryApp::class.java).use { scenario ->
			Log.d(TAG, "Waiting for the Foundry main loop to start...")
			waitForMainLoopStarted()

			// Disable 'quit_on_go_back'.
			FoundryAppInstrumentedTestBridge.requestQuitOnGoBack(false)
			assertTrue(FoundryAppInstrumentedTestBridge.waitForQuitOnGoBackApplied(ENGINE_EVENT_TIMEOUT_MS))

			// Trigger the back press event.
			Espresso.pressBackUnconditionally()

			Log.d(TAG, "Waiting for the engine to terminate...")
			assertFalse(FoundryAppInstrumentedTestBridge.waitForEngineTermination(5_000L))

			val foundry = Foundry.getInstance(InstrumentationRegistry.getInstrumentation().targetContext)
			assertTrue { foundry.runStatus != Foundry.RunStatus.TERMINATING }
		}
	}

	/**
	 * Validate that the back press event quits the game when 'quit_on_go_back' is enabled.
	 */
	@Test
	fun testGameQuittingOnBackPress() {
		resetBridge()
		ActivityScenario.launch(FoundryApp::class.java).use { scenario ->
			Log.d(TAG, "Waiting for the Foundry main loop to start...")
			waitForMainLoopStarted()

			// Enable 'quit_on_go_back'.
			FoundryAppInstrumentedTestBridge.requestQuitOnGoBack(true)
			assertTrue(FoundryAppInstrumentedTestBridge.waitForQuitOnGoBackApplied(ENGINE_EVENT_TIMEOUT_MS))

			// Trigger the back press event.
			Espresso.pressBackUnconditionally()

			Log.d(TAG, "Waiting for the engine to terminate...")
			assertTrue(FoundryAppInstrumentedTestBridge.waitForEngineTermination(ENGINE_EVENT_TIMEOUT_MS))

			val foundry = Foundry.getInstance(InstrumentationRegistry.getInstrumentation().targetContext)
			assertTrue(
				waitForRunStatus(foundry, Foundry.RunStatus.TERMINATING, ENGINE_EVENT_TIMEOUT_MS),
				"Timed out waiting for the Foundry host to enter TERMINATING."
			)
		}
	}
}
