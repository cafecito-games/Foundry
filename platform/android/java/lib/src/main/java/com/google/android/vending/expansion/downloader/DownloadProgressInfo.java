/**************************************************************************/
/*  DownloadProgressInfo.java                                             */
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
 * Copyright (C) 2012 The Android Open Source Project
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

package com.google.android.vending.expansion.downloader;

import android.os.Parcel;
import android.os.Parcelable;

/**
 * This class contains progress information about the active download(s).
 *
 * When you build the Activity that initiates a download and tracks the
 * progress by implementing the {@link IDownloaderClient} interface, you'll
 * receive a DownloadProgressInfo object in each call to the {@link
 * IDownloaderClient#onDownloadProgress} method. This allows you to update
 * your activity's UI with information about the download progress, such
 * as the progress so far, time remaining and current speed.
 */
public class DownloadProgressInfo implements Parcelable {
	public long mOverallTotal;
	public long mOverallProgress;
	public long mTimeRemaining; // time remaining
	public float mCurrentSpeed; // speed in KB/S

	@Override
	public int describeContents() {
		return 0;
	}

	@Override
	public void writeToParcel(Parcel p, int i) {
		p.writeLong(mOverallTotal);
		p.writeLong(mOverallProgress);
		p.writeLong(mTimeRemaining);
		p.writeFloat(mCurrentSpeed);
	}

	public DownloadProgressInfo(Parcel p) {
		mOverallTotal = p.readLong();
		mOverallProgress = p.readLong();
		mTimeRemaining = p.readLong();
		mCurrentSpeed = p.readFloat();
	}

	public DownloadProgressInfo(long overallTotal, long overallProgress,
			long timeRemaining,
			float currentSpeed) {
		this.mOverallTotal = overallTotal;
		this.mOverallProgress = overallProgress;
		this.mTimeRemaining = timeRemaining;
		this.mCurrentSpeed = currentSpeed;
	}

	public static final Creator<DownloadProgressInfo> CREATOR = new Creator<DownloadProgressInfo>() {
		@Override
		public DownloadProgressInfo createFromParcel(Parcel parcel) {
			return new DownloadProgressInfo(parcel);
		}

		@Override
		public DownloadProgressInfo[] newArray(int i) {
			return new DownloadProgressInfo[i];
		}
	};
}
