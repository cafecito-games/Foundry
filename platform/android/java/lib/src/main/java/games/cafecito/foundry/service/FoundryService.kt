/**************************************************************************/
/*  FoundryService.kt                                                       */
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

package games.cafecito.foundry.service

import android.app.Service
import android.content.Intent
import android.hardware.display.DisplayManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.os.Process
import android.os.RemoteException
import android.text.TextUtils
import android.util.Log
import android.view.SurfaceControlViewHost
import android.widget.FrameLayout
import android.window.InputTransferToken
import androidx.annotation.CallSuper
import androidx.annotation.RequiresApi
import games.cafecito.foundry.Foundry
import games.cafecito.foundry.FoundryHost
import games.cafecito.foundry.R
import games.cafecito.foundry.utils.getParcelableCompat
import games.cafecito.foundry.utils.getParcelableExtraCompat
import java.lang.ref.WeakReference

/**
 * Specialized [Service] implementation able to host a Foundry engine instance.
 *
 * When used remotely (from another process), this component lacks access to an [android.app.Activity]
 * instance, and as such it does not have full access to the set of Foundry UI capabilities.
 *
 * Limitations: As of version 4.5, use of vulkan + swappy causes [FoundryService] to crash as swappy requires an Activity
 * context. So [FoundryService] should be used with OpenGL or with Vulkan with swappy disabled.
 */
open class FoundryService : Service() {

	companion object {
		private val TAG = FoundryService::class.java.simpleName

		const val EXTRA_MSG_PAYLOAD = "extraMsgPayload"

		// Keys to store / retrieve msg payloads
		const val KEY_COMMAND_LINE_PARAMETERS = "commandLineParameters"
		const val KEY_HOST_TOKEN = "hostToken"
		const val KEY_HOST_INPUT_TRANSFER_TOKEN = "hostInputTransferToken"
		const val KEY_DISPLAY_ID = "displayId"
		const val KEY_WIDTH = "width"
		const val KEY_HEIGHT = "height"
		const val KEY_SURFACE_PACKAGE = "surfacePackage"
		const val KEY_ENGINE_STATUS = "engineStatus"
		const val KEY_ENGINE_ERROR = "engineError"

		// Set of commands from the client to the service
		const val MSG_INIT_ENGINE = 0
		const val MSG_START_ENGINE = MSG_INIT_ENGINE + 1
		const val MSG_STOP_ENGINE = MSG_START_ENGINE + 1
		const val MSG_DESTROY_ENGINE = MSG_STOP_ENGINE + 1

		@RequiresApi(Build.VERSION_CODES.R)
		const val MSG_WRAP_ENGINE_WITH_SCVH = MSG_DESTROY_ENGINE + 1

		// Set of commands from the service to the client
		const val MSG_ENGINE_ERROR = 100
		const val MSG_ENGINE_STATUS_UPDATE = 101
		const val MSG_ENGINE_RESTART_REQUESTED = 102
	}

	enum class EngineStatus {
		INITIALIZED,
		SCVH_CREATED,
		STARTED,
		STOPPED,
		DESTROYED,
	}

	enum class EngineError {
		ALREADY_BOUND,
		INIT_FAILED,
		SCVH_CREATION_FAILED,
	}

	/**
	 * Used to subscribe to engine's updates.
	 */
	private class RemoteListener(val handlerRef: WeakReference<IncomingHandler>, val replyTo: Messenger) {
		fun onEngineError(error: EngineError, extras: Bundle? = null) {
			try {
				replyTo.send(Message.obtain().apply {
					what = MSG_ENGINE_ERROR
					data.putString(KEY_ENGINE_ERROR, error.name)
					if (extras != null && !extras.isEmpty) {
						data.putAll(extras)
					}
				})
			} catch (e: RemoteException) {
				Log.e(TAG, "Unable to send engine error", e)
			}
		}

		fun onEngineStatusUpdate(status: EngineStatus, extras: Bundle? = null) {
			try {
				replyTo.send(Message.obtain().apply {
					what = MSG_ENGINE_STATUS_UPDATE
					data.putString(KEY_ENGINE_STATUS, status.name)
					if (extras != null && !extras.isEmpty) {
						data.putAll(extras)
					}
				})
			} catch (e: RemoteException) {
				Log.e(TAG, "Unable to send engine status update", e)
			}

			if (status == EngineStatus.DESTROYED) {
				val handler = handlerRef.get() ?: return
				if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && handler.viewHost != null) {
					Log.d(TAG, "Releasing SurfaceControlViewHost")
					handler.viewHost?.release()
					handler.viewHost = null
				}
			}
		}

		fun onEngineRestartRequested() {
			try {
				replyTo.send(Message.obtain(null, MSG_ENGINE_RESTART_REQUESTED))
			} catch (e: RemoteException) {
				Log.w(TAG, "Unable to send restart request", e)
			}
		}
	}

	/**
	 * Handler of incoming messages from remote clients.
	 */
	private class IncomingHandler(private val serviceRef: WeakReference<FoundryService>) :
			Handler(Looper.myLooper() ?: Looper.getMainLooper()) {

		var viewHost: SurfaceControlViewHost? = null

		override fun handleMessage(msg: Message) {
			val service = serviceRef.get() ?: return

			Log.d(TAG, "HandleMessage: $msg")

			if (msg.replyTo == null) {
				// Messages for this handler must have a valid 'replyTo' field
				super.handleMessage(msg)
				return
			}

			try {
				val serviceListener = service.listener
				if (serviceListener == null) {
					service.listener = RemoteListener(WeakReference(this), msg.replyTo)
				} else if (serviceListener.replyTo != msg.replyTo) {
					Log.e(TAG, "Engine is already bound to another client")
					msg.replyTo.send(Message.obtain().apply {
						what = MSG_ENGINE_ERROR
						data.putString(KEY_ENGINE_ERROR, EngineError.ALREADY_BOUND.name)
					})
					return
				}

				when (msg.what) {
					MSG_INIT_ENGINE -> service.initEngine(msg.data.getStringArray(KEY_COMMAND_LINE_PARAMETERS))

					MSG_START_ENGINE -> service.startEngine()

					MSG_STOP_ENGINE -> service.stopEngine()

					MSG_DESTROY_ENGINE -> service.destroyEngine()

					MSG_WRAP_ENGINE_WITH_SCVH -> {
						if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
							Log.e(TAG, "SDK version is less than the minimum required (${Build.VERSION_CODES.R})")
							service.listener?.onEngineError(EngineError.SCVH_CREATION_FAILED)
							return
						}

						var currentViewHost = viewHost
							if (currentViewHost != null) {
								Log.i(TAG, "Attached Foundry engine to SurfaceControlViewHost")
								service.listener?.onEngineStatusUpdate(
									EngineStatus.SCVH_CREATED,
									Bundle().apply {
										putParcelable(KEY_SURFACE_PACKAGE, currentViewHost.surfacePackage)
									}
								)
								return
							}

						val msgData = msg.data
						if (msgData.isEmpty) {
							Log.e(TAG, "Invalid message data from binding client.. Aborting")
							service.listener?.onEngineError(EngineError.SCVH_CREATION_FAILED)
							return
							}

							val foundryContainerLayout = service.foundry.containerLayout
							if (foundryContainerLayout == null) {
								Log.e(TAG, "Invalid Foundry layout.. Aborting")
								service.listener?.onEngineError(EngineError.SCVH_CREATION_FAILED)
								return
							}

							val hostToken = msgData.getBinder(KEY_HOST_TOKEN)
							val hostInputTransferToken = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.BAKLAVA) {
								msgData.getParcelableCompat(KEY_HOST_INPUT_TRANSFER_TOKEN, InputTransferToken::class.java)
							} else {
								null
							}
							val width = msgData.getInt(KEY_WIDTH)
							val height = msgData.getInt(KEY_HEIGHT)
							val displayId = msgData.getInt(KEY_DISPLAY_ID)
							val display = service.getSystemService(DisplayManager::class.java)
								.getDisplay(displayId)

							Log.d(TAG, "Setting up SurfaceControlViewHost")
							currentViewHost = if (hostInputTransferToken != null) {
								SurfaceControlViewHost(service, display, hostInputTransferToken)
							} else {
								SurfaceControlViewHost(service, display, hostToken)
							}.apply {
								setView(foundryContainerLayout, width, height)

								Log.i(TAG, "Attached Foundry engine to SurfaceControlViewHost")
								service.listener?.onEngineStatusUpdate(
									EngineStatus.SCVH_CREATED,
									Bundle().apply {
										putParcelable(KEY_SURFACE_PACKAGE, surfacePackage)
									}
								)
							}
						viewHost = currentViewHost
					}

					else -> super.handleMessage(msg)
				}
			} catch (e: RemoteException) {
				Log.e(TAG, "Unable to handle message", e)
			}
		}
	}

	private inner class FoundryServiceHost : FoundryHost {
		override fun getActivity() = null
		override fun getFoundry() = this@FoundryService.foundry
		override fun getCommandLine() = commandLineParams

		override fun runOnHostThread(action: Runnable) {
			if (Thread.currentThread() != handler.looper.thread) {
				handler.post(action)
			} else {
				action.run()
			}
		}

		override fun onFoundryForceQuit(instance: Foundry) {
			if (instance === foundry) {
				Log.d(TAG, "Force quitting Foundry service")
				forceQuitService()
			}
		}

		override fun onFoundryRestartRequested(instance: Foundry) {
			if (instance === foundry) {
				Log.d(TAG, "Restarting Foundry service")
				listener?.onEngineRestartRequested()
			}
		}
	}

	private val commandLineParams = ArrayList<String>()
	private val handler = IncomingHandler(WeakReference(this))
	private val messenger = Messenger(handler)
	private val foundryHost = FoundryServiceHost()

	private val foundry: Foundry by lazy { Foundry.getInstance(applicationContext) }
	private var listener: RemoteListener? = null

	override fun onCreate() {
		Log.d(TAG, "OnCreate")
		super.onCreate()
	}

	override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
		// Dispatch the start payload to the incoming handler
		Log.d(TAG, "Processing start command $intent")
		val msg = intent?.getParcelableExtraCompat(EXTRA_MSG_PAYLOAD, Message::class.java)
		if (msg != null) {
			handler.sendMessage(msg)
		}
		return START_NOT_STICKY
	}

	@CallSuper
	protected open fun updateCommandLineParams(args: List<String>) {
		// Update the list of command line params with the new args
		commandLineParams.clear()
		if (args.isNotEmpty()) {
			commandLineParams.addAll(args)
		}
	}

	private fun performEngineInitialization(): Boolean {
		Log.d(TAG, "Performing engine initialization")
		try {
			// Initialize the Foundry instance
			if (!foundry.initEngine(foundryHost, foundryHost.commandLine, foundryHost.getHostPlugins(foundry))) {
				throw IllegalStateException("Unable to initialize Foundry engine layer")
			}

			if (foundry.onInitRenderView(foundryHost) == null) {
				throw IllegalStateException("Unable to initialize engine render view")
			}
			return true
		} catch (e: IllegalStateException) {
			Log.e(TAG, "Engine initialization failed", e)
			val errorMessage = if (TextUtils.isEmpty(e.message)
			) {
				getString(R.string.error_engine_setup_message)
			} else {
				e.message!!
			}
			foundry.alert(errorMessage, getString(R.string.text_error_title)) { foundry.destroyAndKillProcess() }
			return false
		}
	}

	override fun onDestroy() {
		Log.d(TAG, "OnDestroy")
		super.onDestroy()
		destroyEngine()
	}

	private fun forceQuitService() {
		Log.d(TAG, "Force quitting service")
		stopSelf()
		Process.killProcess(Process.myPid())
		Runtime.getRuntime().exit(0)
	}

	override fun onBind(intent: Intent?): IBinder? = messenger.binder

	override fun onUnbind(intent: Intent?): Boolean {
		stopEngine()
		return false
	}

	private fun initEngine(args: Array<String>?): FrameLayout? {
		if (!foundry.isInitialized()) {
			if (!args.isNullOrEmpty()) {
				updateCommandLineParams(args.asList())
			}

			if (!performEngineInitialization()) {
				Log.e(TAG, "Unable to initialize Foundry engine")
				return null
			} else {
				Log.i(TAG, "Engine initialization complete!")
			}
		}
		val foundryContainerLayout = foundry.containerLayout
		if (foundryContainerLayout == null) {
			listener?.onEngineError(EngineError.INIT_FAILED)
		} else {
			Log.i(TAG, "Initialized Foundry engine")
			listener?.onEngineStatusUpdate(EngineStatus.INITIALIZED)
		}

		return foundryContainerLayout
	}

	private fun startEngine() {
		if (!foundry.isInitialized()) {
			Log.e(TAG, "Attempting to start uninitialized Foundry engine instance")
			return
		}

		Log.d(TAG, "Starting Foundry engine")
		foundry.onStart(foundryHost)
		foundry.onResume(foundryHost)

		listener?.onEngineStatusUpdate(EngineStatus.STARTED)
	}

	private fun stopEngine() {
		if (!foundry.isInitialized()) {
			Log.e(TAG, "Attempting to stop uninitialized Foundry engine instance")
			return
		}

		Log.d(TAG, "Stopping Foundry engine")
		foundry.onPause(foundryHost)
		foundry.onStop(foundryHost)

		listener?.onEngineStatusUpdate(EngineStatus.STOPPED)
	}

	private fun destroyEngine() {
		if (!foundry.isInitialized()) {
			return
		}

		foundry.onDestroy(foundryHost)

		listener?.onEngineStatusUpdate(EngineStatus.DESTROYED)
		listener = null
		forceQuitService()
	}

}
