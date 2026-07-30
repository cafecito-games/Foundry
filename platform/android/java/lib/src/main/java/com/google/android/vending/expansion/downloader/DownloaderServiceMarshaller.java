/**************************************************************************/
/*  DownloaderServiceMarshaller.java                                      */
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

import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;

import com.google.android.vending.expansion.downloader.impl.DownloaderService;

// -- FOUNDRY start --
import java.lang.ref.WeakReference;
// -- FOUNDRY end --

/**
 * This class is used by the client activity to proxy requests to the Downloader
 * Service.
 *
 * Most importantly, you must call {@link #CreateProxy} during the {@link
 * IDownloaderClient#onServiceConnected} callback in your activity in order to instantiate
 * an {@link IDownloaderService} object that you can then use to issue commands to the {@link
 * DownloaderService} (such as to pause and resume downloads).
 */
public class DownloaderServiceMarshaller {
	public static final int MSG_REQUEST_ABORT_DOWNLOAD =
			1;
	public static final int MSG_REQUEST_PAUSE_DOWNLOAD =
			2;
	public static final int MSG_SET_DOWNLOAD_FLAGS =
			3;
	public static final int MSG_REQUEST_CONTINUE_DOWNLOAD =
			4;
	public static final int MSG_REQUEST_DOWNLOAD_STATE =
			5;
	public static final int MSG_REQUEST_CLIENT_UPDATE =
			6;

	public static final String PARAMS_FLAGS = "flags";
	public static final String PARAM_MESSENGER = DownloaderService.EXTRA_MESSAGE_HANDLER;

	private static class Proxy implements IDownloaderService {
		private Messenger mMsg;

		private void send(int method, Bundle params) {
			Message m = Message.obtain(null, method);
			m.setData(params);
			try {
				mMsg.send(m);
			} catch (RemoteException e) {
				e.printStackTrace();
			}
		}

		public Proxy(Messenger msg) {
			mMsg = msg;
		}

		@Override
		public void requestAbortDownload() {
			send(MSG_REQUEST_ABORT_DOWNLOAD, new Bundle());
		}

		@Override
		public void requestPauseDownload() {
			send(MSG_REQUEST_PAUSE_DOWNLOAD, new Bundle());
		}

		@Override
		public void setDownloadFlags(int flags) {
			Bundle params = new Bundle();
			params.putInt(PARAMS_FLAGS, flags);
			send(MSG_SET_DOWNLOAD_FLAGS, params);
		}

		@Override
		public void requestContinueDownload() {
			send(MSG_REQUEST_CONTINUE_DOWNLOAD, new Bundle());
		}

		@Override
		public void requestDownloadStatus() {
			send(MSG_REQUEST_DOWNLOAD_STATE, new Bundle());
		}

		@Override
		public void onClientUpdated(Messenger clientMessenger) {
			Bundle bundle = new Bundle(1);
			bundle.putParcelable(PARAM_MESSENGER, clientMessenger);
			send(MSG_REQUEST_CLIENT_UPDATE, bundle);
		}
	}

	private static class Stub implements IStub {
		private IDownloaderService mItf = null;
		// -- FOUNDRY start --
		private final MessengerHandlerServer mMsgHandler = new MessengerHandlerServer(this);
		final Messenger mMessenger = new Messenger(mMsgHandler);

		private static class MessengerHandlerServer extends Handler {
			private final WeakReference<Stub> mDownloader;
			public MessengerHandlerServer(Stub downloader) {
				mDownloader = new WeakReference<>(downloader);
			}

			@Override
			public void handleMessage(Message msg) {
				Stub downloader = mDownloader.get();
				if (downloader != null) {
					downloader.handleMessage(msg);
				}
			}
		}

		private void handleMessage(Message msg) {
			switch (msg.what) {
				case MSG_REQUEST_ABORT_DOWNLOAD:
					mItf.requestAbortDownload();
					break;
				case MSG_REQUEST_CONTINUE_DOWNLOAD:
					mItf.requestContinueDownload();
					break;
				case MSG_REQUEST_PAUSE_DOWNLOAD:
					mItf.requestPauseDownload();
					break;
				case MSG_SET_DOWNLOAD_FLAGS:
					mItf.setDownloadFlags(msg.getData().getInt(PARAMS_FLAGS));
					break;
				case MSG_REQUEST_DOWNLOAD_STATE:
					mItf.requestDownloadStatus();
					break;
				case MSG_REQUEST_CLIENT_UPDATE:
					mItf.onClientUpdated((Messenger)msg.getData().getParcelable(
							PARAM_MESSENGER));
					break;
			}
		}
		// -- FOUNDRY end --

		public Stub(IDownloaderService itf) {
			mItf = itf;
		}

		@Override
		public Messenger getMessenger() {
			return mMessenger;
		}

		@Override
		public void connect(Context c) {
		}

		@Override
		public void disconnect(Context c) {
		}
	}

	/**
	 * Returns a proxy that will marshall calls to IDownloaderService methods
	 *
	 * @param ctx
	 * @return
	 */
	public static IDownloaderService CreateProxy(Messenger msg) {
		return new Proxy(msg);
	}

	/**
	 * Returns a stub object that, when connected, will listen for marshaled
	 * IDownloaderService methods and translate them into calls to the supplied
	 * interface.
	 *
	 * @param itf An implementation of IDownloaderService that will be called
	 *            when remote method calls are unmarshalled.
	 * @return
	 */
	public static IStub CreateStub(IDownloaderService itf) {
		return new Stub(itf);
	}
}
