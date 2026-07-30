/**************************************************************************/
/*  StrictPolicy.java                                                     */
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

import android.util.Log;

import com.google.android.vending.licensing.util.URIQueryDecoder;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.HashMap;
import java.util.Map;

/**
 * Non-caching policy. All requests will be sent to the licensing service,
 * and no local caching is performed.
 * <p>
 * Using a non-caching policy ensures that there is no local preference data
 * for malicious users to tamper with. As a side effect, applications
 * will not be permitted to run while offline. Developers should carefully
 * weigh the risks of using this Policy over one which implements caching,
 * such as ServerManagedPolicy.
 * <p>
 * Access to the application is only allowed if a LICENSED response is.
 * received. All other responses (including RETRY) will deny access.
 */
public class StrictPolicy implements Policy {
	private static final String TAG = "StrictPolicy";

	private int mLastResponse;
	private String mLicensingUrl;

	public StrictPolicy() {
		// Set default policy. This will force the application to check the policy on launch.
		mLastResponse = Policy.RETRY;
		mLicensingUrl = null;
	}

	/**
	 * Process a new response from the license server. Since we aren't
	 * performing any caching, this equates to reading the LicenseResponse.
	 * Any cache-related ResponseData is ignored, but the licensing URL
	 * extra is still extracted in cases where the app is unlicensed.
	 *
	 * @param response the result from validating the server response
	 * @param rawData the raw server response data
	 */
	public void processServerResponse(int response, ResponseData rawData) {
		mLastResponse = response;

		if (response == Policy.NOT_LICENSED) {
			Map<String, String> extras = decodeExtras(rawData);
			mLicensingUrl = extras.get("LU");
		}
	}

	/**
	 * {@inheritDoc}
	 *
	 * This implementation allows access if and only if a LICENSED response
	 * was received the last time the server was contacted.
	 */
	public boolean allowAccess() {
		return (mLastResponse == Policy.LICENSED);
	}

	public String getLicensingUrl() {
		return mLicensingUrl;
	}

	private Map<String, String> decodeExtras(
			com.google.android.vending.licensing.ResponseData rawData) {
		Map<String, String> results = new HashMap<String, String>();
		if (rawData == null) {
			return results;
		}

		try {
			URI rawExtras = new URI("?" + rawData.extra);
			URIQueryDecoder.DecodeQuery(rawExtras, results);
		} catch (URISyntaxException e) {
			Log.w(TAG, "Invalid syntax error while decoding extras data from server.");
		}
		return results;
	}
}
