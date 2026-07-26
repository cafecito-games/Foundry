/**************************************************************************/
/*  FoundryHost.java                                                      */
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

import android.app.Activity;

import androidx.annotation.Nullable;

import java.util.Collections;
import java.util.List;
import java.util.Set;

import games.cafecito.foundry.plugin.FoundryPlugin;

/**
 * Denotate a component (e.g: Activity, Fragment) that hosts the {@link Foundry} engine.
 */
public interface FoundryHost {
	/**
	 * Provides a set of command line parameters to setup the {@link Foundry} engine.
	 */
	default List<String> getCommandLine() {
		return Collections.emptyList();
	}

	/**
	 * Invoked on the render thread when setup of the {@link Foundry} engine is complete.
	 */
	default void onFoundrySetupCompleted() {}

	/**
	 * Invoked on the render thread when the {@link Foundry} engine main loop has started.
	 */
	default void onFoundryMainLoopStarted() {}

	/**
	 * Invoked on the render thread to terminate the given {@link Foundry} engine instance.
	 */
	default void onFoundryForceQuit(Foundry instance) {}

	/**
	 * Invoked on the render thread to terminate the {@link Foundry} engine instance with the given id.
	 * @param foundryInstanceId id of the Foundry instance to terminate. See {@code onNewFoundryInstanceRequested}
	 *
	 * @return true if successful, false otherwise.
	 */
	default boolean onFoundryForceQuit(int foundryInstanceId) {
		return false;
	}

	/**
	 * Invoked on the render thread when the Foundry instance wants to be restarted. It's up to the host
	 * to perform the appropriate action(s).
	 */
	default void onFoundryRestartRequested(Foundry instance) {}

	/**
	 * Invoked on the render thread when a new Foundry instance is requested. It's up to the host to
	 * perform the appropriate action(s).
	 *
	 * @param args Arguments used to initialize the new instance.
	 *
	 * @return the id of the new instance. See {@code onFoundryForceQuit}
	 */
	default int onNewFoundryInstanceRequested(String[] args) {
		return -1;
	}

	/**
	 * Provide access to the Activity hosting the {@link Foundry} engine if any.
	 */
	@Nullable
	Activity getActivity();

	/**
	 * Provide access to the hosted {@link Foundry} engine.
	 */
	Foundry getFoundry();

	/**
	 * Returns a set of {@link FoundryPlugin} to be registered with the hosted {@link Foundry} engine.
	 */
	default Set<FoundryPlugin> getHostPlugins(Foundry engine) {
		return Collections.emptySet();
	}

	/**
	 * Returns whether the given feature tag is supported.
	 *
	 * @see <a href="https://docs.godotengine.org/en/stable/tutorials/export/feature_tags.html">Feature tags</a>
	 */
	default boolean supportsFeature(String featureTag) {
		return false;
	}

	/**
	 * Runs the specified action on a host provided thread.
	 */
	default void runOnHostThread(Runnable action) {
		if (action == null) {
			return;
		}

		Activity activity = getActivity();
		if (activity != null) {
			activity.runOnUiThread(action);
		}
	}
}
