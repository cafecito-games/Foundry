/**************************************************************************/
/*  IDownloaderService.java                                               */
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

import android.os.Messenger;

import com.google.android.vending.expansion.downloader.impl.DownloaderService;

/**
 * This interface is implemented by the DownloaderService and by the
 * DownloaderServiceMarshaller. It contains functions to control the service.
 * When a client binds to the service, it must call the onClientUpdated
 * function.
 * <p>
 * You can acquire a proxy that implements this interface for your service by
 * calling {@link DownloaderServiceMarshaller#CreateProxy} during the
 * {@link IDownloaderClient#onServiceConnected} callback. At which point, you
 * should immediately call {@link #onClientUpdated}.
 */
public interface IDownloaderService {
	/**
	 * Set this flag in response to the
	 * IDownloaderClient.STATE_PAUSED_NEED_CELLULAR_PERMISSION state and then
	 * call RequestContinueDownload to resume a download
	 */
	public static final int FLAGS_DOWNLOAD_OVER_CELLULAR = 1;

	/**
	 * Request that the service abort the current download. The service should
	 * respond by changing the state to {@link IDownloaderClient.STATE_ABORTED}.
	 */
	void requestAbortDownload();

	/**
	 * Request that the service pause the current download. The service should
	 * respond by changing the state to
	 * {@link IDownloaderClient.STATE_PAUSED_BY_REQUEST}.
	 */
	void requestPauseDownload();

	/**
	 * Request that the service continue a paused download, when in any paused
	 * or failed state, including
	 * {@link IDownloaderClient.STATE_PAUSED_BY_REQUEST}.
	 */
	void requestContinueDownload();

	/**
	 * Set the flags for this download (e.g.
	 * {@link DownloaderService.FLAGS_DOWNLOAD_OVER_CELLULAR}).
	 *
	 * @param flags
	 */
	void setDownloadFlags(int flags);

	/**
	 * Requests that the download status be sent to the client.
	 */
	void requestDownloadStatus();

	/**
	 * Call this when you get {@link
	 * IDownloaderClient.onServiceConnected(Messenger m)} from the
	 * DownloaderClient to register the client with the service. It will
	 * automatically send the current status to the client.
	 *
	 * @param clientMessenger
	 */
	void onClientUpdated(Messenger clientMessenger);
}
