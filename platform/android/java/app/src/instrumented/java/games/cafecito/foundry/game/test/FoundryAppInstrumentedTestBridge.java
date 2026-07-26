/**************************************************************************/
/*  FoundryAppInstrumentedTestBridge.java                                 */
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

package games.cafecito.foundry.game.test;

import android.content.Context;

import androidx.annotation.Nullable;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Explicit test-only communication bridge between instrumentation and the
 * Foundry Script test runner.
 *
 * <p>The script resolves this exact class through {@code JavaClassWrapper};
 * there is no manifest discovery or native singleton registration.</p>
 */
public final class FoundryAppInstrumentedTestBridge {
	private static final String MAIN_LOOP_STARTED = "main_loop_started";
	private static final String ENGINE_TERMINATING = "engine_terminating";
	private static final String QUIT_ON_GO_BACK_APPLIED = "quit_on_go_back_applied";

	private static final AtomicReference<String> requestedTest = new AtomicReference<>("");
	private static final AtomicReference<Integer> requestedQuitOnGoBack = new AtomicReference<>(-1);
	private static final AtomicReference<Context> applicationContext = new AtomicReference<>();
	private static final ConcurrentHashMap<String, CountDownLatch> latches = new ConcurrentHashMap<>();
	private static final ConcurrentHashMap<String, TestResult> results = new ConcurrentHashMap<>();

	private FoundryAppInstrumentedTestBridge() {}

	public static void reset(Context context) {
		applicationContext.set(context.getApplicationContext());
		requestedTest.set("");
		requestedQuitOnGoBack.set(-1);
		latches.clear();
		results.clear();
		latches.put(MAIN_LOOP_STARTED, new CountDownLatch(1));
		latches.put(ENGINE_TERMINATING, new CountDownLatch(1));
	}

	public static Context getApplicationContext() {
		return applicationContext.get();
	}

	public static void notifyMainLoopStarted() {
		countDown(MAIN_LOOP_STARTED);
	}

	public static boolean waitForMainLoopStarted(long timeoutMillis) throws InterruptedException {
		return await(MAIN_LOOP_STARTED, timeoutMillis);
	}

	public static void notifyEngineTerminating() {
		countDown(ENGINE_TERMINATING);
	}

	public static boolean waitForEngineTermination(long timeoutMillis) throws InterruptedException {
		return await(ENGINE_TERMINATING, timeoutMillis);
	}

	public static void requestTest(String testLabel) {
		results.remove(testLabel);
		latches.put(testLabel, new CountDownLatch(1));
		requestedTest.set(testLabel);
	}

	public static String takeRequestedTest() {
		return requestedTest.getAndSet("");
	}

	public static boolean waitForTest(String testLabel, long timeoutMillis) throws InterruptedException {
		return await(testLabel, timeoutMillis);
	}

	public static void onTestsCompleted(String testLabel, int passes, int failures) {
		results.put(testLabel, new TestResult(passes, failures, null));
		countDown(testLabel);
	}

	public static void onTestsFailed(String testLabel, String failureMessage) {
		results.put(testLabel, new TestResult(0, 1, failureMessage));
		countDown(testLabel);
	}

	public static int getTestPasses(String testLabel) {
		TestResult result = results.get(testLabel);
		return result == null ? 0 : result.passes;
	}

	public static int getTestFailures(String testLabel) {
		TestResult result = results.get(testLabel);
		return result == null ? 1 : result.failures;
	}

	@Nullable
	public static String getTestFailureMessage(String testLabel) {
		TestResult result = results.get(testLabel);
		return result == null ? "No test result was recorded." : result.failureMessage;
	}

	public static void requestQuitOnGoBack(boolean enabled) {
		latches.put(QUIT_ON_GO_BACK_APPLIED, new CountDownLatch(1));
		requestedQuitOnGoBack.set(enabled ? 1 : 0);
	}

	public static int takeRequestedQuitOnGoBack() {
		return requestedQuitOnGoBack.getAndSet(-1);
	}

	public static void notifyQuitOnGoBackApplied() {
		countDown(QUIT_ON_GO_BACK_APPLIED);
	}

	public static boolean waitForQuitOnGoBackApplied(long timeoutMillis) throws InterruptedException {
		return await(QUIT_ON_GO_BACK_APPLIED, timeoutMillis);
	}

	private static boolean await(String key, long timeoutMillis) throws InterruptedException {
		CountDownLatch latch = latches.get(key);
		return latch != null && latch.await(timeoutMillis, TimeUnit.MILLISECONDS);
	}

	private static void countDown(String key) {
		CountDownLatch latch = latches.get(key);
		if (latch != null) {
			latch.countDown();
		}
	}

	private static final class TestResult {
		private final int passes;
		private final int failures;
		@Nullable
		private final String failureMessage;

		private TestResult(int passes, int failures, @Nullable String failureMessage) {
			this.passes = passes;
			this.failures = failures;
			this.failureMessage = failureMessage;
		}
	}
}
