/**************************************************************************/
/*  library_foundry_os.js                                                   */
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

const IDHandler = {
	$IDHandler: {
		_last_id: 0,
		_references: {},

		get: function (p_id) {
			return IDHandler._references[p_id];
		},

		add: function (p_data) {
			const id = ++IDHandler._last_id;
			IDHandler._references[id] = p_data;
			return id;
		},

		remove: function (p_id) {
			delete IDHandler._references[p_id];
		},
	},
};

autoAddDeps(IDHandler, '$IDHandler');
mergeInto(LibraryManager.library, IDHandler);

const FoundryConfig = {
	$FoundryConfig__postset: 'Module["initConfig"] = FoundryConfig.init_config;',
	$FoundryConfig__deps: ['$FoundryRuntime'],
	$FoundryConfig: {
		canvas: null,
		locale: 'en',
		canvas_resize_policy: 2, // Adaptive
		virtual_keyboard: false,
		persistent_drops: false,
		godot_pool_size: 4,
		on_execute: null,
		on_exit: null,

		init_config: function (p_opts) {
			FoundryConfig.canvas_resize_policy = p_opts['canvasResizePolicy'];
			FoundryConfig.canvas = p_opts['canvas'];
			FoundryConfig.locale = p_opts['locale'] || FoundryConfig.locale;
			FoundryConfig.virtual_keyboard = p_opts['virtualKeyboard'];
			FoundryConfig.persistent_drops = !!p_opts['persistentDrops'];
			FoundryConfig.godot_pool_size = p_opts['godotPoolSize'];
			FoundryConfig.on_execute = p_opts['onExecute'];
			FoundryConfig.on_exit = p_opts['onExit'];
			if (p_opts['focusCanvas']) {
				FoundryConfig.canvas.focus();
			}
		},

		locate_file: function (file) {
			return Module['locateFile'](file);
		},
		clear: function () {
			FoundryConfig.canvas = null;
			FoundryConfig.locale = 'en';
			FoundryConfig.canvas_resize_policy = 2;
			FoundryConfig.virtual_keyboard = false;
			FoundryConfig.persistent_drops = false;
			FoundryConfig.on_execute = null;
			FoundryConfig.on_exit = null;
		},
	},

	foundry_js_config_canvas_id_get__proxy: 'sync',
	foundry_js_config_canvas_id_get__sig: 'vii',
	foundry_js_config_canvas_id_get: function (p_ptr, p_ptr_max) {
		FoundryRuntime.stringToHeap(`#${FoundryConfig.canvas.id}`, p_ptr, p_ptr_max);
	},

	foundry_js_config_locale_get__proxy: 'sync',
	foundry_js_config_locale_get__sig: 'vii',
	foundry_js_config_locale_get: function (p_ptr, p_ptr_max) {
		FoundryRuntime.stringToHeap(FoundryConfig.locale, p_ptr, p_ptr_max);
	},
};

autoAddDeps(FoundryConfig, '$FoundryConfig');
mergeInto(LibraryManager.library, FoundryConfig);

const FoundryFS = {
	$FoundryFS__deps: ['$FS', '$IDBFS', '$FoundryRuntime'],
	$FoundryFS__postset: [
		'Module["initFS"] = FoundryFS.init;',
		'Module["copyToFS"] = FoundryFS.copy_to_fs;',
	].join(''),
	$FoundryFS: {
		// ERRNO_CODES works every odd version of emscripten, but this will break too eventually.
		ENOENT: 44,
		_idbfs: false,
		_syncing: false,
		_mount_points: [],

		is_persistent: function () {
			return FoundryFS._idbfs ? 1 : 0;
		},

		// Initialize godot file system, setting up persistent paths.
		// Returns a promise that resolves when the FS is ready.
		// We keep track of mount_points, so that we can properly close the IDBFS
		// since emscripten is not doing it by itself. (emscripten GH#12516).
		init: function (persistentPaths) {
			FoundryFS._idbfs = false;
			if (!Array.isArray(persistentPaths)) {
				return Promise.reject(new Error('Persistent paths must be an array'));
			}
			if (!persistentPaths.length) {
				return Promise.resolve();
			}
			FoundryFS._mount_points = persistentPaths.slice();

			function createRecursive(dir) {
				try {
					FS.stat(dir);
				} catch (e) {
					if (e.errno !== FoundryFS.ENOENT) {
						// Let mkdirTree throw in case, we cannot trust the above check.
						FoundryRuntime.error(e);
					}
					FS.mkdirTree(dir);
				}
			}

			FoundryFS._mount_points.forEach(function (path) {
				createRecursive(path);
				FS.mount(IDBFS, {}, path);
			});
			return new Promise(function (resolve, reject) {
				FS.syncfs(true, function (err) {
					if (err) {
						FoundryFS._mount_points = [];
						FoundryFS._idbfs = false;
						FoundryRuntime.print(`IndexedDB not available: ${err.message}`);
					} else {
						FoundryFS._idbfs = true;
					}
					resolve(err);
				});
			});
		},

		// Deinit godot file system, making sure to unmount file systems, and close IDBFS(s).
		deinit: function () {
			FoundryFS._mount_points.forEach(function (path) {
				try {
					FS.unmount(path);
				} catch (e) {
					FoundryRuntime.print('Already unmounted', e);
				}
				if (FoundryFS._idbfs && IDBFS.dbs[path]) {
					IDBFS.dbs[path].close();
					delete IDBFS.dbs[path];
				}
			});
			FoundryFS._mount_points = [];
			FoundryFS._idbfs = false;
			FoundryFS._syncing = false;
		},

		sync: function () {
			if (FoundryFS._syncing) {
				FoundryRuntime.error('Already syncing!');
				return Promise.resolve();
			}
			FoundryFS._syncing = true;
			return new Promise(function (resolve, reject) {
				FS.syncfs(false, function (error) {
					if (error) {
						FoundryRuntime.error(`Failed to save IDB file system: ${error.message}`);
					}
					FoundryFS._syncing = false;
					resolve(error);
				});
			});
		},

		// Copies a buffer to the internal file system. Creating directories recursively.
		copy_to_fs: function (path, buffer) {
			const idx = path.lastIndexOf('/');
			let dir = '/';
			if (idx > 0) {
				dir = path.slice(0, idx);
			}
			try {
				FS.stat(dir);
			} catch (e) {
				if (e.errno !== FoundryFS.ENOENT) {
					// Let mkdirTree throw in case, we cannot trust the above check.
					FoundryRuntime.error(e);
				}
				FS.mkdirTree(dir);
			}
			FS.writeFile(path, new Uint8Array(buffer));
		},
	},
};
mergeInto(LibraryManager.library, FoundryFS);

const FoundryOS = {
	$FoundryOS__deps: ['$FoundryRuntime', '$FoundryConfig', '$FoundryFS'],
	$FoundryOS__postset: [
		'Module["request_quit"] = function() { FoundryOS.request_quit() };',
		'Module["onExit"] = FoundryOS.cleanup;',
		'FoundryOS._fs_sync_promise = Promise.resolve();',
	].join(''),
	$FoundryOS: {
		request_quit: function () {},
		_async_cbs: [],
		_fs_sync_promise: null,

		atexit: function (p_promise_cb) {
			FoundryOS._async_cbs.push(p_promise_cb);
		},

		cleanup: function (exit_code) {
			const cb = FoundryConfig.on_exit;
			FoundryFS.deinit();
			FoundryConfig.clear();
			if (cb) {
				cb(exit_code);
			}
		},

		finish_async: function (callback) {
			FoundryOS._fs_sync_promise.then(function (err) {
				const promises = [];
				FoundryOS._async_cbs.forEach(function (cb) {
					promises.push(new Promise(cb));
				});
				return Promise.all(promises);
			}).then(function () {
				return FoundryFS.sync(); // Final FS sync.
			}).then(function (err) {
				// Always deferred.
				setTimeout(function () {
					callback();
				}, 0);
			});
		},
	},

	foundry_js_os_finish_async__proxy: 'sync',
	foundry_js_os_finish_async__sig: 'vi',
	foundry_js_os_finish_async: function (p_callback) {
		const func = FoundryRuntime.get_func(p_callback);
		FoundryOS.finish_async(func);
	},

	foundry_js_os_request_quit_cb__proxy: 'sync',
	foundry_js_os_request_quit_cb__sig: 'vi',
	foundry_js_os_request_quit_cb: function (p_callback) {
		FoundryOS.request_quit = FoundryRuntime.get_func(p_callback);
	},

	foundry_js_os_fs_is_persistent__proxy: 'sync',
	foundry_js_os_fs_is_persistent__sig: 'i',
	foundry_js_os_fs_is_persistent: function () {
		return FoundryFS.is_persistent();
	},

	foundry_js_os_fs_sync__proxy: 'sync',
	foundry_js_os_fs_sync__sig: 'vi',
	foundry_js_os_fs_sync: function (callback) {
		const func = FoundryRuntime.get_func(callback);
		FoundryOS._fs_sync_promise = FoundryFS.sync();
		FoundryOS._fs_sync_promise.then(function (err) {
			func();
		});
	},

	foundry_js_os_has_feature__proxy: 'sync',
	foundry_js_os_has_feature__sig: 'ii',
	foundry_js_os_has_feature: function (p_ftr) {
		const ftr = FoundryRuntime.parseString(p_ftr);
		const ua = navigator.userAgent;
		if (ftr === 'web_macos') {
			return (ua.indexOf('Mac') !== -1) ? 1 : 0;
		}
		if (ftr === 'web_windows') {
			return (ua.indexOf('Windows') !== -1) ? 1 : 0;
		}
		if (ftr === 'web_android') {
			return (ua.indexOf('Android') !== -1) ? 1 : 0;
		}
		if (ftr === 'web_ios') {
			return ((ua.indexOf('iPhone') !== -1) || (ua.indexOf('iPad') !== -1) || (ua.indexOf('iPod') !== -1)) ? 1 : 0;
		}
		if (ftr === 'web_linuxbsd') {
			return ((ua.indexOf('CrOS') !== -1) || (ua.indexOf('BSD') !== -1) || (ua.indexOf('Linux') !== -1) || (ua.indexOf('X11') !== -1)) ? 1 : 0;
		}
		return 0;
	},

	foundry_js_os_execute__proxy: 'sync',
	foundry_js_os_execute__sig: 'ii',
	foundry_js_os_execute: function (p_json) {
		const json_args = FoundryRuntime.parseString(p_json);
		const args = JSON.parse(json_args);
		if (FoundryConfig.on_execute) {
			FoundryConfig.on_execute(args);
			return 0;
		}
		return 1;
	},

	foundry_js_os_shell_open__proxy: 'sync',
	foundry_js_os_shell_open__sig: 'vi',
	foundry_js_os_shell_open: function (p_uri) {
		window.open(FoundryRuntime.parseString(p_uri), '_blank');
	},

	foundry_js_os_hw_concurrency_get__proxy: 'sync',
	foundry_js_os_hw_concurrency_get__sig: 'i',
	foundry_js_os_hw_concurrency_get: function () {
		// TODO Godot core needs fixing to avoid spawning too many threads (> 24).
		const concurrency = navigator.hardwareConcurrency || 1;
		return concurrency < 2 ? concurrency : 2;
	},

	foundry_js_os_thread_pool_size_get__proxy: 'sync',
	foundry_js_os_thread_pool_size_get__sig: 'i',
	foundry_js_os_thread_pool_size_get: function () {
		if (typeof PThread === 'undefined') {
			// Threads aren't supported, so default to `1`.
			return 1;
		}

		return FoundryConfig.godot_pool_size;
	},

	foundry_js_os_download_buffer__proxy: 'sync',
	foundry_js_os_download_buffer__sig: 'viiii',
	foundry_js_os_download_buffer: function (p_ptr, p_size, p_name, p_mime) {
		const buf = FoundryRuntime.heapSlice(HEAP8, p_ptr, p_size);
		const name = FoundryRuntime.parseString(p_name);
		const mime = FoundryRuntime.parseString(p_mime);
		const blob = new Blob([buf], { type: mime });
		const url = window.URL.createObjectURL(blob);
		const a = document.createElement('a');
		a.href = url;
		a.download = name;
		a.style.display = 'none';
		document.body.appendChild(a);
		a.click();
		a.remove();
		window.URL.revokeObjectURL(url);
	},
};

autoAddDeps(FoundryOS, '$FoundryOS');
mergeInto(LibraryManager.library, FoundryOS);

/*
 * Godot event listeners.
 * Keeps track of registered event listeners so it can remove them on shutdown.
 */
const FoundryEventListeners = {
	$FoundryEventListeners__deps: ['$FoundryOS'],
	$FoundryEventListeners__postset: 'FoundryOS.atexit(function(resolve, reject) { FoundryEventListeners.clear(); resolve(); });',
	$FoundryEventListeners: {
		handlers: [],

		has: function (target, event, method, capture) {
			return FoundryEventListeners.handlers.findIndex(function (e) {
				return e.target === target && e.event === event && e.method === method && e.capture === capture;
			}) !== -1;
		},

		add: function (target, event, method, capture) {
			if (FoundryEventListeners.has(target, event, method, capture)) {
				return;
			}
			function Handler(p_target, p_event, p_method, p_capture) {
				this.target = p_target;
				this.event = p_event;
				this.method = p_method;
				this.capture = p_capture;
			}
			FoundryEventListeners.handlers.push(new Handler(target, event, method, capture));
			target.addEventListener(event, method, capture);
		},

		clear: function () {
			FoundryEventListeners.handlers.forEach(function (h) {
				h.target.removeEventListener(h.event, h.method, h.capture);
			});
			FoundryEventListeners.handlers.length = 0;
		},
	},
};
mergeInto(LibraryManager.library, FoundryEventListeners);

const FoundryPWA = {

	$FoundryPWA__deps: ['$FoundryRuntime', '$FoundryEventListeners'],
	$FoundryPWA: {
		hasUpdate: false,

		updateState: function (cb, reg) {
			if (!reg) {
				return;
			}
			if (!reg.active) {
				return;
			}
			if (reg.waiting) {
				FoundryPWA.hasUpdate = true;
				cb();
			}
			FoundryEventListeners.add(reg, 'updatefound', function () {
				const installing = reg.installing;
				FoundryEventListeners.add(installing, 'statechange', function () {
					if (installing.state === 'installed') {
						FoundryPWA.hasUpdate = true;
						cb();
					}
				});
			});
		},
	},

	foundry_js_pwa_cb__proxy: 'sync',
	foundry_js_pwa_cb__sig: 'vi',
	foundry_js_pwa_cb: function (p_update_cb) {
		if ('serviceWorker' in navigator) {
			try {
				const cb = FoundryRuntime.get_func(p_update_cb);
				navigator.serviceWorker.getRegistration().then(FoundryPWA.updateState.bind(null, cb));
			} catch (e) {
				FoundryRuntime.error('Failed to assign PWA callback', e);
			}
		}
	},

	foundry_js_pwa_update__proxy: 'sync',
	foundry_js_pwa_update__sig: 'i',
	foundry_js_pwa_update: function () {
		if ('serviceWorker' in navigator && FoundryPWA.hasUpdate) {
			try {
				navigator.serviceWorker.getRegistration().then(function (reg) {
					if (!reg || !reg.waiting) {
						return;
					}
					reg.waiting.postMessage('update');
				});
			} catch (e) {
				FoundryRuntime.error(e);
				return 1;
			}
			return 0;
		}
		return 1;
	},
};

autoAddDeps(FoundryPWA, '$FoundryPWA');
mergeInto(LibraryManager.library, FoundryPWA);
