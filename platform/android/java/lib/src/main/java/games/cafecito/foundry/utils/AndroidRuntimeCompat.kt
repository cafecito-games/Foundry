/**************************************************************************/
/*  AndroidRuntimeCompat.kt                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

package games.cafecito.foundry.utils

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageInfo
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.os.Parcelable
import android.os.VibrationEffect
import android.os.Vibrator
import android.view.SurfaceView
import android.view.Window
import android.view.WindowManager
import android.window.InputTransferToken
import androidx.annotation.RequiresApi
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import androidx.core.content.pm.PackageInfoCompat
import androidx.core.os.BundleCompat
import java.io.Serializable

fun getDefaultSharedPreferencesCompat(context: Context): SharedPreferences {
	return context.getSharedPreferences("${context.packageName}_preferences", Context.MODE_PRIVATE)
}

fun getVibratorServiceCompat(context: Context): Vibrator? {
	return ContextCompat.getSystemService(context, Vibrator::class.java)
}

fun PackageInfo.getLongVersionCodeCompat(): Long {
	return PackageInfoCompat.getLongVersionCode(this)
}

fun <T : Parcelable> Intent.getParcelableExtraCompat(name: String, clazz: Class<T>): T? {
	return IntentCompat.getParcelableExtra(this, name, clazz)
}

fun <T : Parcelable> Bundle.getParcelableCompat(name: String, clazz: Class<T>): T? {
	return BundleCompat.getParcelable(this, name, clazz)
}

fun <T : Serializable> Bundle.getSerializableCompat(name: String, clazz: Class<T>): T? {
	return BundleCompat.getSerializable(this, name, clazz)
}

fun SurfaceView.getHostInputTransferTokenCompat(): InputTransferToken? {
	return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.BAKLAVA) {
		rootSurfaceControl?.inputTransferToken
	} else {
		null
	}
}

@RequiresApi(Build.VERSION_CODES.R)
fun SurfaceView.getHostTokenCompat(): IBinder? {
	@Suppress("DEPRECATION")
	return hostToken
}

fun Window.addTranslucentSystemBarFlagsCompat() {
	@Suppress("DEPRECATION")
	addFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS)
	@Suppress("DEPRECATION")
	addFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_NAVIGATION)
}

fun Activity.turnScreenOnCompat() {
	if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
		setTurnScreenOn(true)
	} else {
		@Suppress("DEPRECATION")
		window.addFlags(WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON)
	}
}

@SuppressLint("MissingPermission")
fun Vibrator.vibrateCompat(durationMs: Long, amplitude: Int) {
	if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
		vibrate(
			VibrationEffect.createOneShot(
				durationMs,
				if (amplitude <= -1) VibrationEffect.DEFAULT_AMPLITUDE else amplitude
			)
		)
	} else {
		@Suppress("DEPRECATION")
		vibrate(durationMs)
	}
}
