/**************************************************************************/
/*  library_foundry_input.js                                                */
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

/*
 * IME API helper.
 */

const FoundryIME = {
	$FoundryIME__deps: ['$FoundryRuntime', '$FoundryEventListeners'],
	$FoundryIME__postset: 'FoundryOS.atexit(function(resolve, reject) { FoundryIME.clear(); resolve(); });',
	$FoundryIME: {
		ime: null,
		active: false,
		focusTimerIntervalId: -1,

		getModifiers: function (evt) {
			return (evt.shiftKey + 0) + ((evt.altKey + 0) << 1) + ((evt.ctrlKey + 0) << 2) + ((evt.metaKey + 0) << 3);
		},

		ime_active: function (active) {
			function clearFocusTimerInterval() {
				clearInterval(FoundryIME.focusTimerIntervalId);
				FoundryIME.focusTimerIntervalId = -1;
			}

			function focusTimer() {
				if (FoundryIME.ime == null) {
					clearFocusTimerInterval();
					return;
				}
				FoundryIME.ime.focus();
			}

			if (FoundryIME.focusTimerIntervalId > -1) {
				clearFocusTimerInterval();
			}

			if (FoundryIME.ime == null) {
				return;
			}

			FoundryIME.active = active;
			if (active) {
				FoundryIME.ime.style.display = 'block';
				FoundryIME.focusTimerIntervalId = setInterval(focusTimer, 100);
			} else {
				FoundryIME.ime.style.display = 'none';
				FoundryConfig.canvas.focus();
			}
		},

		ime_position: function (x, y) {
			if (FoundryIME.ime == null) {
				return;
			}
			const canvas = FoundryConfig.canvas;
			const rect = canvas.getBoundingClientRect();
			const rw = canvas.width / rect.width;
			const rh = canvas.height / rect.height;
			const clx = (x / rw) + rect.x;
			const cly = (y / rh) + rect.y;

			FoundryIME.ime.style.left = `${clx}px`;
			FoundryIME.ime.style.top = `${cly}px`;
		},

		init: function (ime_cb, key_cb, code, key) {
			function key_event_cb(pressed, evt) {
				const modifiers = FoundryIME.getModifiers(evt);
				FoundryRuntime.stringToHeap(evt.code, code, 32);
				FoundryRuntime.stringToHeap(evt.key, key, 32);
				key_cb(pressed, evt.repeat, modifiers);
				evt.preventDefault();
			}
			function ime_event_cb(event) {
				if (FoundryIME.ime == null) {
					return;
				}
				switch (event.type) {
				case 'compositionstart':
					ime_cb(0, null);
					FoundryIME.ime.innerHTML = '';
					break;
				case 'compositionupdate': {
					const ptr = FoundryRuntime.allocString(event.data);
					ime_cb(1, ptr);
					FoundryRuntime.free(ptr);
				} break;
				case 'compositionend': {
					const ptr = FoundryRuntime.allocString(event.data);
					ime_cb(2, ptr);
					FoundryRuntime.free(ptr);
					FoundryIME.ime.innerHTML = '';
				} break;
				default:
					// Do nothing.
				}
			}

			const ime = document.createElement('div');
			ime.className = 'ime';
			ime.style.background = 'none';
			ime.style.opacity = 0.0;
			ime.style.position = 'fixed';
			ime.style.textAlign = 'left';
			ime.style.fontSize = '1px';
			ime.style.left = '0px';
			ime.style.top = '0px';
			ime.style.width = '100%';
			ime.style.height = '40px';
			ime.style.pointerEvents = 'none';
			ime.style.display = 'none';
			ime.contentEditable = 'true';

			FoundryEventListeners.add(ime, 'compositionstart', ime_event_cb, false);
			FoundryEventListeners.add(ime, 'compositionupdate', ime_event_cb, false);
			FoundryEventListeners.add(ime, 'compositionend', ime_event_cb, false);
			FoundryEventListeners.add(ime, 'keydown', key_event_cb.bind(null, 1), false);
			FoundryEventListeners.add(ime, 'keyup', key_event_cb.bind(null, 0), false);

			ime.onblur = function () {
				this.style.display = 'none';
				FoundryConfig.canvas.focus();
				FoundryIME.active = false;
			};

			FoundryConfig.canvas.parentElement.appendChild(ime);
			FoundryIME.ime = ime;
		},

		clear: function () {
			if (FoundryIME.ime == null) {
				return;
			}
			if (FoundryIME.focusTimerIntervalId > -1) {
				clearInterval(FoundryIME.focusTimerIntervalId);
				FoundryIME.focusTimerIntervalId = -1;
			}
			FoundryIME.ime.remove();
			FoundryIME.ime = null;
		},
	},
};
mergeInto(LibraryManager.library, FoundryIME);

/*
 * Gamepad API helper.
 */
const FoundryInputGamepads = {
	$FoundryInputGamepads__deps: ['$FoundryRuntime', '$FoundryEventListeners'],
	$FoundryInputGamepads: {
		samples: [],

		get_pads: function () {
			try {
				// Will throw in iframe when permission is denied.
				// Will throw/warn in the future for insecure contexts.
				// See https://github.com/w3c/gamepad/pull/120
				const pads = navigator.getGamepads();
				if (pads) {
					return pads;
				}
				return [];
			} catch (e) {
				return [];
			}
		},

		get_samples: function () {
			return FoundryInputGamepads.samples;
		},

		get_sample: function (index) {
			const samples = FoundryInputGamepads.samples;
			return index < samples.length ? samples[index] : null;
		},

		sample: function () {
			const pads = FoundryInputGamepads.get_pads();
			const samples = [];
			for (let i = 0; i < pads.length; i++) {
				const pad = pads[i];
				if (!pad) {
					samples.push(null);
					continue;
				}
				const s = {
					standard: pad.mapping === 'standard',
					buttons: [],
					axes: [],
					connected: pad.connected,
				};
				for (let b = 0; b < pad.buttons.length; b++) {
					s.buttons.push(pad.buttons[b].value);
				}
				for (let a = 0; a < pad.axes.length; a++) {
					s.axes.push(pad.axes[a]);
				}
				samples.push(s);
			}
			FoundryInputGamepads.samples = samples;
		},

		init: function (onchange) {
			FoundryInputGamepads.samples = [];
			function add(pad) {
				const guid = FoundryInputGamepads.get_guid(pad);
				const c_id = FoundryRuntime.allocString(pad.id);
				const c_guid = FoundryRuntime.allocString(guid);
				onchange(pad.index, 1, c_id, c_guid);
				FoundryRuntime.free(c_id);
				FoundryRuntime.free(c_guid);
			}
			const pads = FoundryInputGamepads.get_pads();
			for (let i = 0; i < pads.length; i++) {
				// Might be reserved space.
				if (pads[i]) {
					add(pads[i]);
				}
			}
			FoundryEventListeners.add(window, 'gamepadconnected', function (evt) {
				if (evt.gamepad) {
					add(evt.gamepad);
				}
			}, false);
			FoundryEventListeners.add(window, 'gamepaddisconnected', function (evt) {
				if (evt.gamepad) {
					onchange(evt.gamepad.index, 0);
				}
			}, false);
		},

		get_guid: function (pad) {
			if (pad.mapping) {
				return pad.mapping;
			}
			const ua = navigator.userAgent;
			let os = 'Unknown';
			if (ua.indexOf('Android') >= 0) {
				os = 'Android';
			} else if (ua.indexOf('Linux') >= 0) {
				os = 'Linux';
			} else if (ua.indexOf('iPhone') >= 0) {
				os = 'iOS';
			} else if (ua.indexOf('Macintosh') >= 0) {
				// Updated iPads will fall into this category.
				os = 'MacOSX';
			} else if (ua.indexOf('Windows') >= 0) {
				os = 'Windows';
			}

			const id = pad.id;
			// Chrom* style: NAME (Vendor: xxxx Product: xxxx).
			const exp1 = /vendor: ([0-9a-f]{4}) product: ([0-9a-f]{4})/i;
			// Firefox/Safari style (Safari may remove leading zeroes).
			const exp2 = /^([0-9a-f]+)-([0-9a-f]+)-/i;
			let vendor = '';
			let product = '';
			if (exp1.test(id)) {
				const match = exp1.exec(id);
				vendor = match[1].padStart(4, '0');
				product = match[2].padStart(4, '0');
			} else if (exp2.test(id)) {
				const match = exp2.exec(id);
				vendor = match[1].padStart(4, '0');
				product = match[2].padStart(4, '0');
			}
			if (!vendor || !product) {
				return `${os}Unknown`;
			}
			return os + vendor + product;
		},
	},
};
mergeInto(LibraryManager.library, FoundryInputGamepads);

/*
 * Drag and drop helper.
 * This is pretty big, but basically detect dropped files on FoundryConfig.canvas,
 * process them one by one (recursively for directories), and copies them to
 * the temporary FS path '/tmp/drop-[random]/' so it can be emitted as a godot
 * event (that requires a string array of paths).
 *
 * NOTE: The temporary files are removed after the callback. This means that
 * deferred callbacks won't be able to access the files.
 */
const FoundryInputDragDrop = {
	$FoundryInputDragDrop__deps: ['$FS', '$FoundryFS'],
	$FoundryInputDragDrop: {
		promises: [],
		pending_files: [],

		add_entry: function (entry) {
			if (entry.isDirectory) {
				FoundryInputDragDrop.add_dir(entry);
			} else if (entry.isFile) {
				FoundryInputDragDrop.add_file(entry);
			} else {
				FoundryRuntime.error('Unrecognized entry...', entry);
			}
		},

		add_dir: function (entry) {
			FoundryInputDragDrop.promises.push(new Promise(function (resolve, reject) {
				const reader = entry.createReader();
				reader.readEntries(function (entries) {
					for (let i = 0; i < entries.length; i++) {
						FoundryInputDragDrop.add_entry(entries[i]);
					}
					resolve();
				});
			}));
		},

		add_file: function (entry) {
			FoundryInputDragDrop.promises.push(new Promise(function (resolve, reject) {
				entry.file(function (file) {
					const reader = new FileReader();
					reader.onload = function () {
						const f = {
							'path': file.relativePath || file.webkitRelativePath,
							'name': file.name,
							'type': file.type,
							'size': file.size,
							'data': reader.result,
						};
						if (!f['path']) {
							f['path'] = f['name'];
						}
						FoundryInputDragDrop.pending_files.push(f);
						resolve();
					};
					reader.onerror = function () {
						FoundryRuntime.print('Error reading file');
						reject();
					};
					reader.readAsArrayBuffer(file);
				}, function (err) {
					FoundryRuntime.print('Error!');
					reject();
				});
			}));
		},

		process: function (resolve, reject) {
			if (FoundryInputDragDrop.promises.length === 0) {
				resolve();
				return;
			}
			FoundryInputDragDrop.promises.pop().then(function () {
				setTimeout(function () {
					FoundryInputDragDrop.process(resolve, reject);
				}, 0);
			});
		},

		_process_event: function (ev, callback) {
			ev.preventDefault();
			if (ev.dataTransfer.items) {
				// Use DataTransferItemList interface to access the file(s)
				for (let i = 0; i < ev.dataTransfer.items.length; i++) {
					const item = ev.dataTransfer.items[i];
					let entry = null;
					if ('getAsEntry' in item) {
						entry = item.getAsEntry();
					} else if ('webkitGetAsEntry' in item) {
						entry = item.webkitGetAsEntry();
					}
					if (entry) {
						FoundryInputDragDrop.add_entry(entry);
					}
				}
			} else {
				FoundryRuntime.error('File upload not supported');
			}
			new Promise(FoundryInputDragDrop.process).then(function () {
				const DROP = `/tmp/drop-${parseInt(Math.random() * (1 << 30), 10)}/`;
				const drops = [];
				const files = [];
				FS.mkdir(DROP.slice(0, -1)); // Without trailing slash
				FoundryInputDragDrop.pending_files.forEach((elem) => {
					const path = elem['path'];
					FoundryFS.copy_to_fs(DROP + path, elem['data']);
					let idx = path.indexOf('/');
					if (idx === -1) {
						// Root file
						drops.push(DROP + path);
					} else {
						// Subdir
						const sub = path.substr(0, idx);
						idx = sub.indexOf('/');
						if (idx < 0 && drops.indexOf(DROP + sub) === -1) {
							drops.push(DROP + sub);
						}
					}
					files.push(DROP + path);
				});
				FoundryInputDragDrop.promises = [];
				FoundryInputDragDrop.pending_files = [];
				callback(drops);
				if (FoundryConfig.persistent_drops) {
					// Delay removal at exit.
					FoundryOS.atexit(function (resolve, reject) {
						FoundryInputDragDrop.remove_drop(files, DROP);
						resolve();
					});
				} else {
					FoundryInputDragDrop.remove_drop(files, DROP);
				}
			});
		},

		remove_drop: function (files, drop_path) {
			const dirs = [drop_path.substr(0, drop_path.length - 1)];
			// Remove temporary files
			files.forEach(function (file) {
				FS.unlink(file);
				let dir = file.replace(drop_path, '');
				let idx = dir.lastIndexOf('/');
				while (idx > 0) {
					dir = dir.substr(0, idx);
					if (dirs.indexOf(drop_path + dir) === -1) {
						dirs.push(drop_path + dir);
					}
					idx = dir.lastIndexOf('/');
				}
			});
			// Remove dirs.
			dirs.sort(function (a, b) {
				const al = (a.match(/\//g) || []).length;
				const bl = (b.match(/\//g) || []).length;
				if (al > bl) {
					return -1;
				} else if (al < bl) {
					return 1;
				}
				return 0;
			}).forEach(function (dir) {
				FS.rmdir(dir);
			});
		},

		handler: function (callback) {
			return function (ev) {
				FoundryInputDragDrop._process_event(ev, callback);
			};
		},
	},
};
mergeInto(LibraryManager.library, FoundryInputDragDrop);

/*
 * Godot exposed input functions.
 */
const FoundryInput = {
	$FoundryInput__deps: ['$FoundryRuntime', '$FoundryConfig', '$FoundryEventListeners', '$FoundryInputGamepads', '$FoundryInputDragDrop', '$FoundryIME'],
	$FoundryInput: {
		inputKeyCallback: null,
		setInputKeyData: null,

		getModifiers: function (evt) {
			return (evt.shiftKey + 0) + ((evt.altKey + 0) << 1) + ((evt.ctrlKey + 0) << 2) + ((evt.metaKey + 0) << 3);
		},

		computePosition: function (evt, rect) {
			const canvas = FoundryConfig.canvas;
			const rw = canvas.width / rect.width;
			const rh = canvas.height / rect.height;
			const x = (evt.clientX - rect.x) * rw;
			const y = (evt.clientY - rect.y) * rh;
			return [x, y];
		},

		onKeyEvent: function (pIsPressed, pEvent) {
			if (FoundryInput.inputKeyCallback == null) {
				throw new TypeError('FoundryInput.onKeyEvent(): FoundryInput.inputKeyCallback is null, cannot process key event.');
			}
			if (FoundryInput.setInputKeyData == null) {
				throw new TypeError('FoundryInput.onKeyEvent(): FoundryInput.setInputKeyData is null, cannot process key event.');
			}

			const modifiers = FoundryInput.getModifiers(pEvent);
			FoundryInput.setInputKeyData(pEvent.code, pEvent.key);
			FoundryInput.inputKeyCallback(pIsPressed ? 1 : 0, pEvent.repeat, modifiers);
			pEvent.preventDefault();
		},
	},

	/*
	 * Mouse API
	 */
	foundry_js_input_mouse_move_cb__proxy: 'sync',
	foundry_js_input_mouse_move_cb__sig: 'vi',
	foundry_js_input_mouse_move_cb: function (callback) {
		const func = FoundryRuntime.get_func(callback);
		const canvas = FoundryConfig.canvas;
		function move_cb(evt) {
			const rect = canvas.getBoundingClientRect();
			const pos = FoundryInput.computePosition(evt, rect);
			// Scale movement
			const rw = canvas.width / rect.width;
			const rh = canvas.height / rect.height;
			const rel_pos_x = evt.movementX * rw;
			const rel_pos_y = evt.movementY * rh;
			const modifiers = FoundryInput.getModifiers(evt);
			func(pos[0], pos[1], rel_pos_x, rel_pos_y, modifiers, evt.pressure);
		}
		FoundryEventListeners.add(window, 'pointermove', move_cb, false);
	},

	foundry_js_input_mouse_wheel_cb__proxy: 'sync',
	foundry_js_input_mouse_wheel_cb__sig: 'vi',
	foundry_js_input_mouse_wheel_cb: function (callback) {
		const func = FoundryRuntime.get_func(callback);
		function wheel_cb(evt) {
			if (func(evt.deltaMode, evt.deltaX ?? 0, evt.deltaY ?? 0)) {
				evt.preventDefault();
			}
		}
		FoundryEventListeners.add(FoundryConfig.canvas, 'wheel', wheel_cb, false);
	},

	foundry_js_input_mouse_button_cb__proxy: 'sync',
	foundry_js_input_mouse_button_cb__sig: 'vi',
	foundry_js_input_mouse_button_cb: function (callback) {
		const func = FoundryRuntime.get_func(callback);
		const canvas = FoundryConfig.canvas;
		function button_cb(p_pressed, evt) {
			const rect = canvas.getBoundingClientRect();
			const pos = FoundryInput.computePosition(evt, rect);
			const modifiers = FoundryInput.getModifiers(evt);
			// Since the event is consumed, focus manually.
			// NOTE: The iframe container may not have focus yet, so focus even when already active.
			if (p_pressed) {
				FoundryConfig.canvas.focus();
			}
			if (func(p_pressed, evt.button, pos[0], pos[1], modifiers)) {
				evt.preventDefault();
			}
		}
		FoundryEventListeners.add(canvas, 'mousedown', button_cb.bind(null, 1), false);
		FoundryEventListeners.add(window, 'mouseup', button_cb.bind(null, 0), false);
	},

	/*
	 * Touch API
	 */
	foundry_js_input_touch_cb__proxy: 'sync',
	foundry_js_input_touch_cb__sig: 'viii',
	foundry_js_input_touch_cb: function (callback, ids, coords) {
		const func = FoundryRuntime.get_func(callback);
		const canvas = FoundryConfig.canvas;
		function touch_cb(type, evt) {
			// Since the event is consumed, focus manually.
			// NOTE: The iframe container may not have focus yet, so focus even when already active.
			if (type === 0) {
				FoundryConfig.canvas.focus();
			}
			const rect = canvas.getBoundingClientRect();
			const touches = evt.changedTouches;
			for (let i = 0; i < touches.length; i++) {
				const touch = touches[i];
				const pos = FoundryInput.computePosition(touch, rect);
				FoundryRuntime.setHeapValue(coords + (i * 2) * 8, pos[0], 'double');
				FoundryRuntime.setHeapValue(coords + (i * 2 + 1) * 8, pos[1], 'double');
				FoundryRuntime.setHeapValue(ids + i * 4, touch.identifier, 'i32');
			}
			func(type, touches.length);
			if (evt.cancelable) {
				evt.preventDefault();
			}
		}
		FoundryEventListeners.add(canvas, 'touchstart', touch_cb.bind(null, 0), false);
		FoundryEventListeners.add(canvas, 'touchend', touch_cb.bind(null, 1), false);
		FoundryEventListeners.add(canvas, 'touchcancel', touch_cb.bind(null, 1), false);
		FoundryEventListeners.add(canvas, 'touchmove', touch_cb.bind(null, 2), false);
	},

	/*
	 * Key API
	 */
	foundry_js_input_key_cb__proxy: 'sync',
	foundry_js_input_key_cb__sig: 'viii',
	foundry_js_input_key_cb: function (pCallback, pCodePtr, pKeyPtr) {
		FoundryInput.inputKeyCallback = FoundryRuntime.get_func(pCallback);
		FoundryInput.setInputKeyData = (pCode, pKey) => {
			FoundryRuntime.stringToHeap(pCode, pCodePtr, 32);
			FoundryRuntime.stringToHeap(pKey, pKeyPtr, 32);
		};
		FoundryEventListeners.add(FoundryConfig.canvas, 'keydown', FoundryInput.onKeyEvent.bind(null, true), false);
		FoundryEventListeners.add(FoundryConfig.canvas, 'keyup', FoundryInput.onKeyEvent.bind(null, false), false);
	},

	/*
	 * IME API
	 */
	foundry_js_set_ime_active__proxy: 'sync',
	foundry_js_set_ime_active__sig: 'vi',
	foundry_js_set_ime_active: function (p_active) {
		FoundryIME.ime_active(p_active);
	},

	foundry_js_set_ime_position__proxy: 'sync',
	foundry_js_set_ime_position__sig: 'vii',
	foundry_js_set_ime_position: function (p_x, p_y) {
		FoundryIME.ime_position(p_x, p_y);
	},

	foundry_js_set_ime_cb__proxy: 'sync',
	foundry_js_set_ime_cb__sig: 'viiii',
	foundry_js_set_ime_cb: function (p_ime_cb, p_key_cb, code, key) {
		const ime_cb = FoundryRuntime.get_func(p_ime_cb);
		const key_cb = FoundryRuntime.get_func(p_key_cb);
		FoundryIME.init(ime_cb, key_cb, code, key);
	},

	foundry_js_is_ime_focused__proxy: 'sync',
	foundry_js_is_ime_focused__sig: 'i',
	foundry_js_is_ime_focused: function () {
		return FoundryIME.active;
	},

	/*
	 * Gamepad API
	 */
	foundry_js_input_gamepad_cb__proxy: 'sync',
	foundry_js_input_gamepad_cb__sig: 'vi',
	foundry_js_input_gamepad_cb: function (change_cb) {
		const onchange = FoundryRuntime.get_func(change_cb);
		FoundryInputGamepads.init(onchange);
	},

	foundry_js_input_gamepad_sample_count__proxy: 'sync',
	foundry_js_input_gamepad_sample_count__sig: 'i',
	foundry_js_input_gamepad_sample_count: function () {
		return FoundryInputGamepads.get_samples().length;
	},

	foundry_js_input_gamepad_sample__proxy: 'sync',
	foundry_js_input_gamepad_sample__sig: 'i',
	foundry_js_input_gamepad_sample: function () {
		FoundryInputGamepads.sample();
		return 0;
	},

	foundry_js_input_gamepad_sample_get__proxy: 'sync',
	foundry_js_input_gamepad_sample_get__sig: 'iiiiiii',
	foundry_js_input_gamepad_sample_get: function (p_index, r_btns, r_btns_num, r_axes, r_axes_num, r_standard) {
		const sample = FoundryInputGamepads.get_sample(p_index);
		if (!sample || !sample.connected) {
			return 1;
		}
		const btns = sample.buttons;
		const btns_len = btns.length < 16 ? btns.length : 16;
		for (let i = 0; i < btns_len; i++) {
			FoundryRuntime.setHeapValue(r_btns + (i << 2), btns[i], 'float');
		}
		FoundryRuntime.setHeapValue(r_btns_num, btns_len, 'i32');
		const axes = sample.axes;
		const axes_len = axes.length < 10 ? axes.length : 10;
		for (let i = 0; i < axes_len; i++) {
			FoundryRuntime.setHeapValue(r_axes + (i << 2), axes[i], 'float');
		}
		FoundryRuntime.setHeapValue(r_axes_num, axes_len, 'i32');
		const is_standard = sample.standard ? 1 : 0;
		FoundryRuntime.setHeapValue(r_standard, is_standard, 'i32');
		return 0;
	},

	/*
	 * Drag/Drop API
	 */
	foundry_js_input_drop_files_cb__proxy: 'sync',
	foundry_js_input_drop_files_cb__sig: 'vi',
	foundry_js_input_drop_files_cb: function (callback) {
		const func = FoundryRuntime.get_func(callback);
		const dropFiles = function (files) {
			const args = files || [];
			if (!args.length) {
				return;
			}
			const argc = args.length;
			const argv = FoundryRuntime.allocStringArray(args);
			func(argv, argc);
			FoundryRuntime.freeStringArray(argv, argc);
		};
		const canvas = FoundryConfig.canvas;
		FoundryEventListeners.add(canvas, 'dragover', function (ev) {
			// Prevent default behavior (which would try to open the file(s))
			ev.preventDefault();
		}, false);
		FoundryEventListeners.add(canvas, 'drop', FoundryInputDragDrop.handler(dropFiles));
	},

	/* Paste API */
	foundry_js_input_paste_cb__proxy: 'sync',
	foundry_js_input_paste_cb__sig: 'vi',
	foundry_js_input_paste_cb: function (callback) {
		const func = FoundryRuntime.get_func(callback);
		FoundryEventListeners.add(window, 'paste', function (evt) {
			const text = evt.clipboardData.getData('text');
			const ptr = FoundryRuntime.allocString(text);
			func(ptr);
			FoundryRuntime.free(ptr);
		}, false);
	},

	foundry_js_input_vibrate_handheld__proxy: 'sync',
	foundry_js_input_vibrate_handheld__sig: 'vi',
	foundry_js_input_vibrate_handheld: function (p_duration_ms) {
		if (typeof navigator.vibrate !== 'function') {
			FoundryRuntime.print('This browser does not support vibration.');
		} else {
			navigator.vibrate(p_duration_ms);
		}
	},
};

autoAddDeps(FoundryInput, '$FoundryInput');
mergeInto(LibraryManager.library, FoundryInput);
