/**************************************************************************/
/*  FoundryJavaExtensionTest.kt                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
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

package games.cafecito.foundry

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class FoundryJavaExtensionTest {
	private class RecordingPredicate(private val present: Boolean) : (String) -> Boolean {
		val queried = mutableListOf<String>()

		override fun invoke(assetName: String): Boolean {
			queried.add(assetName)
			return present
		}
	}

	@Test
	fun packagedBindingYieldsExactlyTheFixedConfigPath() {
		val predicate = RecordingPredicate(present = true)

		val configFiles = FoundryJavaExtension.configFiles(predicate)

		assertArrayEquals(arrayOf("res://FoundryJava.foundryextension"), configFiles)
		assertEquals(listOf("FoundryJava.foundryextension"), predicate.queried)
	}

	@Test
	fun absentBindingYieldsNoConfigPaths() {
		val predicate = RecordingPredicate(present = false)

		val configFiles = FoundryJavaExtension.configFiles(predicate)

		assertEquals(0, configFiles.size)
		assertEquals(listOf("FoundryJava.foundryextension"), predicate.queried)
	}

	@Test
	fun discoveryQueriesOnlyTheSingleFixedAssetName() {
		// Enumerating or probing anything beyond this one name would be the scanning discovery
		// model the platform seam deliberately does not have.
		for (present in listOf(true, false)) {
			val predicate = RecordingPredicate(present)

			FoundryJavaExtension.configFiles(predicate)

			assertEquals(1, predicate.queried.size)
			assertEquals(FoundryJavaExtension.ASSET_NAME, predicate.queried.single())
		}
	}

	@Test
	fun constantsPinTheAssetNameAndConfigPath() {
		assertEquals("FoundryJava.foundryextension", FoundryJavaExtension.ASSET_NAME)
		assertEquals("res://FoundryJava.foundryextension", FoundryJavaExtension.CONFIG_PATH)
	}
}
