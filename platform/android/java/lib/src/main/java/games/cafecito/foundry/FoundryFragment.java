/**************************************************************************/
/*  FoundryFragment.java                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
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

package games.cafecito.foundry;

import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Bundle;
import android.text.TextUtils;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.annotation.CallSuper;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import java.util.Collections;
import java.util.List;

/**
 * Base fragment for Android apps intending to use Foundry for part of the app's UI.
 */
public class FoundryFragment extends Fragment implements FoundryHost {
	private static final String TAG = FoundryFragment.class.getSimpleName();

	private FrameLayout foundryContainerLayout;

	@Nullable
	private FoundryHost parentHost;
	private Foundry foundry;

	@Override
	public Foundry getFoundry() {
		return foundry;
	}

	@Override
	public void onAttach(@NonNull Context context) {
		super.onAttach(context);
		if (getParentFragment() instanceof FoundryHost) {
			parentHost = (FoundryHost)getParentFragment();
		} else if (getActivity() instanceof FoundryHost) {
			parentHost = (FoundryHost)getActivity();
		}
	}

	@Override
	public void onDetach() {
		if (foundryContainerLayout != null && foundryContainerLayout.getParent() != null) {
			Log.d(TAG, "Cleaning up Foundry container layout during detach.");
			((ViewGroup)foundryContainerLayout.getParent()).removeView(foundryContainerLayout);
		}

		super.onDetach();
		parentHost = null;
	}

	@CallSuper
	@Override
	public void onConfigurationChanged(Configuration newConfig) {
		super.onConfigurationChanged(newConfig);
		foundry.onConfigurationChanged(newConfig);
	}

	@CallSuper
	@Override
	public void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		foundry.onActivityResult(requestCode, resultCode, data);
	}

	@CallSuper
	@Override
	public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
		super.onRequestPermissionsResult(requestCode, permissions, grantResults);
		foundry.onRequestPermissionsResult(requestCode, permissions, grantResults);
	}

	@Override
	public void onCreate(Bundle icicle) {
		super.onCreate(icicle);

		if (parentHost != null) {
			foundry = parentHost.getFoundry();
		}
		if (foundry == null) {
			foundry = Foundry.getInstance(requireContext());
		}
		performEngineInitialization();
	}

	private void performEngineInitialization() {
		try {
			if (!foundry.initEngine(this, getCommandLine())) {
				throw new IllegalStateException("Unable to initialize Foundry engine");
			}

			foundryContainerLayout = foundry.onInitRenderView(this);
			if (foundryContainerLayout == null) {
				throw new IllegalStateException("Unable to initialize engine render view");
			}
		} catch (IllegalStateException e) {
			Log.e(TAG, "Engine initialization failed", e);
			final String errorMessage = TextUtils.isEmpty(e.getMessage())
					? getString(R.string.error_engine_setup_message)
					: e.getMessage();
			foundry.alert(errorMessage, getString(R.string.text_error_title), foundry::destroyAndKillProcess);
		}
	}

	@Override
	public View onCreateView(@NonNull LayoutInflater inflater, ViewGroup container, Bundle icicle) {
		if (foundryContainerLayout != null && foundryContainerLayout.getParent() != null) {
			Log.w(TAG, "Foundry container layout already has a parent, removing it.");
			((ViewGroup)foundryContainerLayout.getParent()).removeView(foundryContainerLayout);
		}

		return foundryContainerLayout;
	}

	@Override
	public void onDestroy() {
		if (foundryContainerLayout != null && foundryContainerLayout.getParent() != null) {
			Log.w(TAG, "Removing Foundry container layout from parent during destruction.");
			((ViewGroup)foundryContainerLayout.getParent()).removeView(foundryContainerLayout);
		}

		foundry.onDestroy(this);
		super.onDestroy();
	}

	@Override
	public void onPause() {
		super.onPause();

		if (!foundry.isInitialized()) {
			return;
		}

		foundry.onPause(this);
	}

	@Override
	public void onStop() {
		super.onStop();
		if (!foundry.isInitialized()) {
			return;
		}

		foundry.onStop(this);
	}

	@Override
	public void onStart() {
		super.onStart();
		if (!foundry.isInitialized()) {
			return;
		}

		foundry.onStart(this);
	}

	@Override
	public void onResume() {
		super.onResume();
		if (!foundry.isInitialized()) {
			return;
		}

		foundry.onResume(this);
	}

	public void onBackPressed() {
		foundry.onBackPressed();
	}

	@CallSuper
	@Override
	public List<String> getCommandLine() {
		return parentHost != null ? parentHost.getCommandLine() : Collections.emptyList();
	}

	@CallSuper
	@Override
	public void onFoundrySetupCompleted() {
		if (parentHost != null) {
			parentHost.onFoundrySetupCompleted();
		}
	}

	@CallSuper
	@Override
	public void onFoundryMainLoopStarted() {
		if (parentHost != null) {
			parentHost.onFoundryMainLoopStarted();
		}
	}

	@Override
	public void onFoundryForceQuit(Foundry instance) {
		if (parentHost != null) {
			parentHost.onFoundryForceQuit(instance);
		}
	}

	@Override
	public boolean onFoundryForceQuit(int foundryInstanceId) {
		return parentHost != null && parentHost.onFoundryForceQuit(foundryInstanceId);
	}

	@Override
	public void onFoundryRestartRequested(Foundry instance) {
		if (parentHost != null) {
			parentHost.onFoundryRestartRequested(instance);
		}
	}

	@Override
	public int onNewFoundryInstanceRequested(String[] args) {
		if (parentHost != null) {
			return parentHost.onNewFoundryInstanceRequested(args);
		}
		return -1;
	}

	@Override
	public boolean supportsFeature(String featureTag) {
		if (parentHost != null) {
			return parentHost.supportsFeature(featureTag);
		}
		return false;
	}
}
