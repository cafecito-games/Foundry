/**************************************************************************/
/*  DeviceLimiter.java                                                    */
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
 * Allows the developer to limit the number of devices using a single license.
 * <p>
 * The LICENSED response from the server contains a user identifier unique to
 * the &lt;application, user&gt; pair. The developer can send this identifier
 * to their own server along with some device identifier (a random number
 * generated and stored once per application installation,
 * {@link android.telephony.TelephonyManager#getDeviceId getDeviceId},
 * {@link android.provider.Settings.Secure#ANDROID_ID ANDROID_ID}, etc).
 * The more sources used to identify the device, the harder it will be for an
 * attacker to spoof.
 * <p>
 * The server can look at the &lt;application, user, device id&gt; tuple and
 * restrict a user's application license to run on at most 10 different devices
 * in a week (for example). We recommend not being too restrictive because a
 * user might legitimately have multiple devices or be in the process of
 * changing phones. This will catch egregious violations of multiple people
 * sharing one license.
 */
public interface DeviceLimiter {
	/**
	 * Checks if this device is allowed to use the given user's license.
	 *
	 * @param userId the user whose license the server responded with
	 * @return LICENSED if the device is allowed, NOT_LICENSED if not, RETRY if an error occurs
	 */
	int isDeviceAllowed(String userId);
}
