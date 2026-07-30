/**************************************************************************/
/*  PreferenceObfuscator.java                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.android.vending.licensing;

import android.content.SharedPreferences;
import android.util.Log;

/**
 * An wrapper for SharedPreferences that transparently performs data obfuscation.
 */
public class PreferenceObfuscator {
	private static final String TAG = "PreferenceObfuscator";

	private final SharedPreferences mPreferences;
	private final Obfuscator mObfuscator;
	private SharedPreferences.Editor mEditor;

	/**
	 * Constructor.
	 *
	 * @param sp A SharedPreferences instance provided by the system.
	 * @param o The Obfuscator to use when reading or writing data.
	 */
	public PreferenceObfuscator(SharedPreferences sp, Obfuscator o) {
		mPreferences = sp;
		mObfuscator = o;
		mEditor = null;
	}

	public void putString(String key, String value) {
		if (mEditor == null) {
			mEditor = mPreferences.edit();
			// -- FOUNDRY start --
			mEditor.apply();
			// -- FOUNDRY end --
		}
		String obfuscatedValue = mObfuscator.obfuscate(value, key);
		mEditor.putString(key, obfuscatedValue);
	}

	public String getString(String key, String defValue) {
		String result;
		String value = mPreferences.getString(key, null);
		if (value != null) {
			try {
				result = mObfuscator.unobfuscate(value, key);
			} catch (ValidationException e) {
				// Unable to unobfuscate, data corrupt or tampered
				Log.w(TAG, "Validation error while reading preference: " + key);
				result = defValue;
			}
		} else {
			// Preference not found
			result = defValue;
		}
		return result;
	}

	public void commit() {
		if (mEditor != null) {
			mEditor.commit();
			mEditor = null;
		}
	}
}
