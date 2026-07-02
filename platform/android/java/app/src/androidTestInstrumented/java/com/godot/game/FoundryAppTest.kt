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

package com.godot.game

import android.content.ComponentName
import android.content.Intent
import android.util.Log
import androidx.test.core.app.ActivityScenario
import androidx.test.espresso.Espresso
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.godot.game.test.FoundryAppInstrumentedTestPlugin
import games.cafecito.foundry.Foundry
import games.cafecito.foundry.FoundryActivity.Companion.EXTRA_COMMAND_LINE_PARAMS
import games.cafecito.foundry.plugin.FoundryPluginRegistry
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.test.assertEquals
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

		private const val FOUNDRY_APP_LAUNCHER_CLASS_NAME = "com.godot.game.FoundryAppLauncher"
		private const val FOUNDRY_APP_CLASS_NAME = "com.godot.game.FoundryApp"

		private val TEST_COMMAND_LINE_PARAMS = arrayOf("This is a test")
	}

	private fun getTestPlugin(): FoundryAppInstrumentedTestPlugin? {
		return FoundryPluginRegistry.getPluginRegistry()
			.getPlugin("FoundryAppInstrumentedTestPlugin") as FoundryAppInstrumentedTestPlugin?
	}

	/**
	 * Runs the JavaClassWrapper tests via the FoundryAppInstrumentedTestPlugin.
	 */
	@Test
	fun runJavaClassWrapperTests() {
		ActivityScenario.launch(FoundryApp::class.java).use { scenario ->
			scenario.onActivity { activity ->
				val testPlugin = getTestPlugin()
				assertNotNull(testPlugin)

				Log.d(TAG, "Waiting for the Foundry main loop to start...")
				testPlugin.waitForFoundryMainLoopStarted()

				Log.d(TAG, "Running JavaClassWrapper tests...")
				val result = testPlugin.runJavaClassWrapperTests()
				assertNotNull(result)
				result.exceptionOrNull()?.let { throw it }
				assertTrue(result.isSuccess)
				Log.d(TAG, "Passed ${result.getOrNull()} tests")
			}
		}
	}

	/**
	 * Runs file access related tests.
	 */
	@Test
	fun runFileAccessTests() {
		ActivityScenario.launch(FoundryApp::class.java).use { scenario ->
			scenario.onActivity { activity ->
				val testPlugin = getTestPlugin()
				assertNotNull(testPlugin)

				Log.d(TAG, "Waiting for the Foundry main loop to start...")
				testPlugin.waitForFoundryMainLoopStarted()

				Log.d(TAG, "Running FileAccess tests...")
				val result = testPlugin.runFileAccessTests()
				assertNotNull(result)
				result.exceptionOrNull()?.let { throw it }
				assertTrue(result.isSuccess)
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
		ActivityScenario.launch(FoundryApp::class.java).use { scenario ->
			val testPlugin = getTestPlugin()
			assertNotNull(testPlugin)

			Log.d(TAG, "Waiting for the Foundry main loop to start...")
			testPlugin.waitForFoundryMainLoopStarted()

			// Disable 'quit_on_go_back'.
			testPlugin.updateQuitOnGoBack(false)

			// Trigger the back press event.
			Espresso.pressBackUnconditionally()

			Log.d(TAG, "Waiting for the engine to terminate...")
			testPlugin.waitForEngineTermination(5_000L)

			val foundry = Foundry.getInstance(InstrumentationRegistry.getInstrumentation().targetContext)
			assertTrue { foundry.runStatus != Foundry.RunStatus.TERMINATING }
		}
	}

	/**
	 * Validate that the back press event quits the game when 'quit_on_go_back' is enabled.
	 */
	@Test
	fun testGameQuittingOnBackPress() {
		ActivityScenario.launch(FoundryApp::class.java).use { scenario ->
			val testPlugin = getTestPlugin()
			assertNotNull(testPlugin)

			Log.d(TAG, "Waiting for the Foundry main loop to start...")
			testPlugin.waitForFoundryMainLoopStarted()

			// Enable 'quit_on_go_back'.
			testPlugin.updateQuitOnGoBack(true)

			// Trigger the back press event.
			Espresso.pressBackUnconditionally()

			Log.d(TAG, "Waiting for the engine to terminate...")
			testPlugin.waitForEngineTermination(5_000L)

			val foundry = Foundry.getInstance(InstrumentationRegistry.getInstrumentation().targetContext)
			assertTrue { foundry.runStatus == Foundry.RunStatus.TERMINATING }
		}
	}
}
