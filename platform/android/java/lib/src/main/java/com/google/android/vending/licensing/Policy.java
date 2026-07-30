/**************************************************************************/
/*  Policy.java                                                           */
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

/**
 * Policy used by {@link LicenseChecker} to determine whether a user should have
 * access to the application.
 */
public interface Policy {
	/**
	 * Change these values to make it more difficult for tools to automatically
	 * strip LVL protection from your APK.
	 */

	/**
	 * LICENSED means that the server returned back a valid license response
	 */
	public static final int LICENSED = 0x0100;
	/**
	 * NOT_LICENSED means that the server returned back a valid license response
	 * that indicated that the user definitively is not licensed
	 */
	public static final int NOT_LICENSED = 0x0231;
	/**
	 * RETRY means that the license response was unable to be determined ---
	 * perhaps as a result of faulty networking
	 */
	public static final int RETRY = 0x0123;

	/**
	 * Provide results from contact with the license server. Retry counts are
	 * incremented if the current value of response is RETRY. Results will be
	 * used for any future policy decisions.
	 *
	 * @param response the result from validating the server response
	 * @param rawData the raw server response data, can be null for RETRY
	 */
	void processServerResponse(int response, ResponseData rawData);

	/**
	 * Check if the user should be allowed access to the application.
	 */
	boolean allowAccess();

	/**
	 * Gets the licensing URL returned by the server that can enable access for unlicensed apps (e.g.
	 * buy app on the Play Store).
	 */
	String getLicensingUrl();
}
