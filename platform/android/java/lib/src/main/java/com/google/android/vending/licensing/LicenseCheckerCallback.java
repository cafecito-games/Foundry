/**************************************************************************/
/*  LicenseCheckerCallback.java                                           */
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
 * Callback for the license checker library.
 * <p>
 * Upon checking with the Market server and conferring with the {@link Policy},
 * the library calls the appropriate callback method to communicate the result.
 * <p>
 * <b>The callback does not occur in the original checking thread.</b> Your
 * application should post to the appropriate handling thread or lock
 * accordingly.
 * <p>
 * The reason that is passed back with allow/dontAllow is the base status handed
 * to the policy for allowed/disallowing the license. Policy.RETRY will call
 * allow or dontAllow depending on other statistics associated with the policy,
 * while in most cases Policy.NOT_LICENSED will call dontAllow and
 * Policy.LICENSED will Allow.
 */
public interface LicenseCheckerCallback {
	/**
	 * Allow use. App should proceed as normal.
	 *
	 * @param reason Policy.LICENSED or Policy.RETRY typically. (although in
	 *            theory the policy can return Policy.NOT_LICENSED here as well)
	 */
	public void allow(int reason);

	/**
	 * Don't allow use. App should inform user and take appropriate action.
	 *
	 * @param reason Policy.NOT_LICENSED or Policy.RETRY. (although in theory
	 *            the policy can return Policy.LICENSED here as well ---
	 *            perhaps the call to the LVL took too long, for example)
	 */
	public void dontAllow(int reason);

	/**
	 * Application error codes.
	 */
	public static final int ERROR_INVALID_PACKAGE_NAME = 1;
	public static final int ERROR_NON_MATCHING_UID = 2;
	public static final int ERROR_NOT_MARKET_MANAGED = 3;
	public static final int ERROR_CHECK_IN_PROGRESS = 4;
	public static final int ERROR_INVALID_PUBLIC_KEY = 5;
	public static final int ERROR_MISSING_PERMISSION = 6;

	/**
	 * Error in application code. Caller did not call or set up license checker
	 * correctly. Should be considered fatal.
	 */
	public void applicationError(int errorCode);
}
