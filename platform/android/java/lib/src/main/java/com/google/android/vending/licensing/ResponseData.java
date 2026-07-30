/**************************************************************************/
/*  ResponseData.java                                                     */
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

import android.text.TextUtils;

import java.util.regex.Pattern;

/**
 * ResponseData from licensing server.
 */
public class ResponseData {
	public int responseCode;
	public int nonce;
	public String packageName;
	public String versionCode;
	public String userId;
	public long timestamp;
	/**
	 * Response-specific data.
	 */
	public String extra;

	/**
	 * Parses response string into ResponseData.
	 *
	 * @param responseData response data string
	 * @throws IllegalArgumentException upon parsing error
	 * @return ResponseData object
	 */
	public static ResponseData parse(String responseData) {
		// Must parse out main response data and response-specific data.
		int index = responseData.indexOf(':');
		String mainData, extraData;
		if (-1 == index) {
			mainData = responseData;
			extraData = "";
		} else {
			mainData = responseData.substring(0, index);
			extraData = index >= responseData.length() ? "" : responseData.substring(index + 1);
		}

		String[] fields = TextUtils.split(mainData, Pattern.quote("|"));
		if (fields.length < 6) {
			throw new IllegalArgumentException("Wrong number of fields.");
		}

		ResponseData data = new ResponseData();
		data.extra = extraData;
		data.responseCode = Integer.parseInt(fields[0]);
		data.nonce = Integer.parseInt(fields[1]);
		data.packageName = fields[2];
		data.versionCode = fields[3];
		// Application-specific user identifier.
		data.userId = fields[4];
		data.timestamp = Long.parseLong(fields[5]);

		return data;
	}

	@Override
	public String toString() {
		return TextUtils.join("|", new Object[] { responseCode, nonce, packageName, versionCode, userId, timestamp });
	}
}
