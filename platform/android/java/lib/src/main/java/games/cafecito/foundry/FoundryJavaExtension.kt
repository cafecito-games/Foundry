/**************************************************************************/
/*  FoundryJavaExtension.kt                                               */
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

package games.cafecito.foundry

/**
 * Discovery of the optional Foundry-Java binding extension.
 *
 * The Foundry-Java Gradle plugin injects the binding configuration into the APK at build time, so
 * it is never a project file and never reaches `res://.foundry/extension_list.cfg`. The Android
 * platform-extension seam is the only place the running engine can learn about it.
 *
 * Discovery is deliberately a single check for one fixed asset name. There is no manifest scanning,
 * class scanning, asset enumeration, or reflection: an application either ships exactly this asset
 * or it ships no binding at all.
 */
internal object FoundryJavaExtension {
	const val ASSET_NAME = "FoundryJava.foundryextension"
	const val CONFIG_PATH = "res://FoundryJava.foundryextension"

	/**
	 * Returns the single binding configuration path when [assetExists] reports that the fixed
	 * asset is packaged, and no paths otherwise.
	 *
	 * [assetExists] is invoked at most once, and only ever with [ASSET_NAME].
	 */
	fun configFiles(assetExists: (String) -> Boolean): Array<String> =
		if (assetExists(ASSET_NAME)) arrayOf(CONFIG_PATH) else emptyArray()
}
