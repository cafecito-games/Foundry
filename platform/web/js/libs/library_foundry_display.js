/**************************************************************************/
/*  library_foundry_display.js                                              */
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

const FoundryDisplayVK = {

	$FoundryDisplayVK__deps: ['$FoundryRuntime', '$FoundryConfig', '$FoundryEventListeners', '$FoundryInput'],
	$FoundryDisplayVK__postset: 'FoundryOS.atexit(function(resolve, reject) { FoundryDisplayVK.clear(); resolve(); });',
	$FoundryDisplayVK: {
		textinput: null,
		textarea: null,

		available: function () {
			return FoundryConfig.virtual_keyboard && 'ontouchstart' in window;
		},

		init: function (input_cb) {
			function create(what) {
				const elem = document.createElement(what);
				elem.style.display = 'none';
				elem.style.position = 'absolute';
				elem.style.zIndex = '-1';
				elem.style.background = 'transparent';
				elem.style.padding = '0px';
				elem.style.margin = '0px';
				elem.style.overflow = 'hidden';
				elem.style.width = '0px';
				elem.style.height = '0px';
				elem.style.border = '0px';
				elem.style.outline = 'none';
				elem.readonly = true;
				elem.disabled = true;
				FoundryEventListeners.add(elem, 'input', function (evt) {
					const c_str = FoundryRuntime.allocString(elem.value);
					input_cb(c_str, elem.selectionEnd);
					FoundryRuntime.free(c_str);
				}, false);
				if (what === 'input') {
					// Handling the "Enter" key.
					const onKey = (pEvent, pEventName) => {
						if (pEvent.key !== 'Enter') {
							return;
						}
						FoundryInput.onKeyEvent(pEventName === 'keydown', pEvent);
					};
					FoundryEventListeners.add(elem, 'keydown', (pEvent) => onKey(pEvent, 'keydown'), false);
					FoundryEventListeners.add(elem, 'keyup', (pEvent) => onKey(pEvent, 'keyup'), false);
				}
				FoundryEventListeners.add(elem, 'blur', function (evt) {
					elem.style.display = 'none';
					elem.readonly = true;
					elem.disabled = true;
				}, false);
				FoundryConfig.canvas.insertAdjacentElement('beforebegin', elem);
				return elem;
			}
			FoundryDisplayVK.textinput = create('input');
			FoundryDisplayVK.textarea = create('textarea');
			FoundryDisplayVK.updateSize();
		},
		show: function (text, type, start, end) {
			if (!FoundryDisplayVK.textinput || !FoundryDisplayVK.textarea) {
				return;
			}
			if (FoundryDisplayVK.textinput.style.display !== '' || FoundryDisplayVK.textarea.style.display !== '') {
				FoundryDisplayVK.hide();
			}
			FoundryDisplayVK.updateSize();

			let elem = FoundryDisplayVK.textinput;
			switch (type) {
			case 0: // KEYBOARD_TYPE_DEFAULT
				elem.type = 'text';
				elem.inputmode = '';
				break;
			case 1: // KEYBOARD_TYPE_MULTILINE
				elem = FoundryDisplayVK.textarea;
				break;
			case 2: // KEYBOARD_TYPE_NUMBER
				elem.type = 'text';
				elem.inputmode = 'numeric';
				break;
			case 3: // KEYBOARD_TYPE_NUMBER_DECIMAL
				elem.type = 'text';
				elem.inputmode = 'decimal';
				break;
			case 4: // KEYBOARD_TYPE_PHONE
				elem.type = 'tel';
				elem.inputmode = '';
				break;
			case 5: // KEYBOARD_TYPE_EMAIL_ADDRESS
				elem.type = 'email';
				elem.inputmode = '';
				break;
			case 6: // KEYBOARD_TYPE_PASSWORD
				elem.type = 'password';
				elem.inputmode = '';
				break;
			case 7: // KEYBOARD_TYPE_URL
				elem.type = 'url';
				elem.inputmode = '';
				break;
			default:
				elem.type = 'text';
				elem.inputmode = '';
				break;
			}

			elem.readonly = false;
			elem.disabled = false;
			elem.value = text;
			elem.style.display = 'block';
			elem.focus();
			elem.setSelectionRange(start, end);
		},
		hide: function () {
			if (!FoundryDisplayVK.textinput || !FoundryDisplayVK.textarea) {
				return;
			}
			[FoundryDisplayVK.textinput, FoundryDisplayVK.textarea].forEach(function (elem) {
				elem.blur();
				elem.style.display = 'none';
				elem.value = '';
			});
		},
		updateSize: function () {
			if (!FoundryDisplayVK.textinput || !FoundryDisplayVK.textarea) {
				return;
			}
			const rect = FoundryConfig.canvas.getBoundingClientRect();
			function update(elem) {
				elem.style.left = `${rect.left}px`;
				elem.style.top = `${rect.top}px`;
				elem.style.width = `${rect.width}px`;
				elem.style.height = `${rect.height}px`;
			}
			update(FoundryDisplayVK.textinput);
			update(FoundryDisplayVK.textarea);
		},
		clear: function () {
			if (FoundryDisplayVK.textinput) {
				FoundryDisplayVK.textinput.remove();
				FoundryDisplayVK.textinput = null;
			}
			if (FoundryDisplayVK.textarea) {
				FoundryDisplayVK.textarea.remove();
				FoundryDisplayVK.textarea = null;
			}
		},
	},
};
mergeInto(LibraryManager.library, FoundryDisplayVK);

/*
 * Display server cursor helper.
 * Keeps track of cursor status and custom shapes.
 */
const FoundryDisplayCursor = {
	$FoundryDisplayCursor__deps: ['$FoundryOS', '$FoundryConfig'],
	$FoundryDisplayCursor__postset: 'FoundryOS.atexit(function(resolve, reject) { FoundryDisplayCursor.clear(); resolve(); });',
	$FoundryDisplayCursor: {
		shape: 'default',
		visible: true,
		cursors: {},
		set_style: function (style) {
			FoundryConfig.canvas.style.cursor = style;
		},
		set_shape: function (shape) {
			FoundryDisplayCursor.shape = shape;
			let css = shape;
			if (shape in FoundryDisplayCursor.cursors) {
				const c = FoundryDisplayCursor.cursors[shape];
				css = `url("${c.url}") ${c.x} ${c.y}, default`;
			}
			if (FoundryDisplayCursor.visible) {
				FoundryDisplayCursor.set_style(css);
			}
		},
		clear: function () {
			FoundryDisplayCursor.set_style('');
			FoundryDisplayCursor.shape = 'default';
			FoundryDisplayCursor.visible = true;
			Object.keys(FoundryDisplayCursor.cursors).forEach(function (key) {
				URL.revokeObjectURL(FoundryDisplayCursor.cursors[key]);
				delete FoundryDisplayCursor.cursors[key];
			});
		},
		lockPointer: function () {
			const canvas = FoundryConfig.canvas;
			if (canvas.requestPointerLock) {
				canvas.requestPointerLock();
			}
		},
		releasePointer: function () {
			if (document.exitPointerLock) {
				document.exitPointerLock();
			}
		},
		isPointerLocked: function () {
			return document.pointerLockElement === FoundryConfig.canvas;
		},
	},
};
mergeInto(LibraryManager.library, FoundryDisplayCursor);

const FoundryDisplayScreen = {
	$FoundryDisplayScreen__deps: ['$FoundryConfig', '$FoundryOS', '$GL', 'emscripten_webgl_get_current_context'],
	$FoundryDisplayScreen: {
		desired_size: [0, 0],
		hidpi: true,
		getPixelRatio: function () {
			return FoundryDisplayScreen.hidpi ? window.devicePixelRatio || 1 : 1;
		},
		isFullscreen: function () {
			const elem = document.fullscreenElement || document.mozFullscreenElement
				|| document.webkitFullscreenElement || document.msFullscreenElement;
			if (elem) {
				return elem === FoundryConfig.canvas;
			}
			// But maybe knowing the element is not supported.
			return document.fullscreen || document.mozFullScreen
				|| document.webkitIsFullscreen;
		},
		hasFullscreen: function () {
			return document.fullscreenEnabled || document.mozFullScreenEnabled
				|| document.webkitFullscreenEnabled;
		},
		requestFullscreen: function () {
			if (!FoundryDisplayScreen.hasFullscreen()) {
				return 1;
			}
			const canvas = FoundryConfig.canvas;
			try {
				const promise = (canvas.requestFullscreen || canvas.msRequestFullscreen
					|| canvas.mozRequestFullScreen || canvas.mozRequestFullscreen
					|| canvas.webkitRequestFullscreen
				).call(canvas);
				// Some browsers (Safari) return undefined.
				// For the standard ones, we need to catch it.
				if (promise) {
					promise.catch(function () {
						// nothing to do.
					});
				}
			} catch (e) {
				return 1;
			}
			return 0;
		},
		exitFullscreen: function () {
			if (!FoundryDisplayScreen.isFullscreen()) {
				return 0;
			}
			try {
				const promise = document.exitFullscreen();
				if (promise) {
					promise.catch(function () {
						// nothing to do.
					});
				}
			} catch (e) {
				return 1;
			}
			return 0;
		},
		_updateGL: function () {
			const gl_context_handle = _emscripten_webgl_get_current_context();
			const gl = GL.getContext(gl_context_handle);
			if (gl) {
				GL.resizeOffscreenFramebuffer(gl);
			}
		},
		updateSize: function () {
			const isFullscreen = FoundryDisplayScreen.isFullscreen();
			const wantsFullWindow = FoundryConfig.canvas_resize_policy === 2;
			const noResize = FoundryConfig.canvas_resize_policy === 0;
			const dWidth = FoundryDisplayScreen.desired_size[0];
			const dHeight = FoundryDisplayScreen.desired_size[1];
			const canvas = FoundryConfig.canvas;
			let width = dWidth;
			let height = dHeight;
			if (noResize) {
				// Don't resize canvas, just update GL if needed.
				if (canvas.width !== width || canvas.height !== height) {
					FoundryDisplayScreen.desired_size = [canvas.width, canvas.height];
					FoundryDisplayScreen._updateGL();
					return 1;
				}
				return 0;
			}
			const scale = FoundryDisplayScreen.getPixelRatio();
			if (isFullscreen || wantsFullWindow) {
				// We need to match screen size.
				width = Math.floor(window.innerWidth * scale);
				height = Math.floor(window.innerHeight * scale);
			}
			const csw = `${Math.floor(width / scale)}px`;
			const csh = `${Math.floor(height / scale)}px`;
			if (canvas.style.width !== csw || canvas.style.height !== csh || canvas.width !== width || canvas.height !== height) {
				// Size doesn't match.
				// Resize canvas, set correct CSS pixel size, update GL.
				canvas.width = width;
				canvas.height = height;
				canvas.style.width = csw;
				canvas.style.height = csh;
				FoundryDisplayScreen._updateGL();
				return 1;
			}
			return 0;
		},
	},
};
mergeInto(LibraryManager.library, FoundryDisplayScreen);

/**
 * Display server interface.
 *
 * Exposes all the functions needed by DisplayServer implementation.
 */
const FoundryDisplay = {
	$FoundryDisplay__deps: ['$FoundryConfig', '$FoundryRuntime', '$FoundryDisplayCursor', '$FoundryEventListeners', '$FoundryDisplayScreen', '$FoundryDisplayVK'],
	$FoundryDisplay: {
		window_icon: '',
		getDPI: function () {
			// devicePixelRatio is given in dppx
			// https://drafts.csswg.org/css-values/#resolution
			// > due to the 1:96 fixed ratio of CSS *in* to CSS *px*, 1dppx is equivalent to 96dpi.
			const dpi = Math.round(window.devicePixelRatio * 96);
			return dpi >= 96 ? dpi : 96;
		},
	},

	foundry_js_display_is_swap_ok_cancel__proxy: 'sync',
	foundry_js_display_is_swap_ok_cancel__sig: 'i',
	foundry_js_display_is_swap_ok_cancel: function () {
		const win = (['Windows', 'Win64', 'Win32', 'WinCE']);
		const plat = navigator.platform || '';
		if (win.indexOf(plat) !== -1) {
			return 1;
		}
		return 0;
	},

	foundry_js_tts_is_speaking__proxy: 'sync',
	foundry_js_tts_is_speaking__sig: 'i',
	foundry_js_tts_is_speaking: function () {
		return window.speechSynthesis.speaking;
	},

	foundry_js_tts_is_paused__proxy: 'sync',
	foundry_js_tts_is_paused__sig: 'i',
	foundry_js_tts_is_paused: function () {
		return window.speechSynthesis.paused;
	},

	foundry_js_tts_get_voices__proxy: 'sync',
	foundry_js_tts_get_voices__sig: 'vi',
	foundry_js_tts_get_voices: function (p_callback) {
		const func = FoundryRuntime.get_func(p_callback);
		try {
			const arr = [];
			const voices = window.speechSynthesis.getVoices();
			for (let i = 0; i < voices.length; i++) {
				arr.push(`${voices[i].lang};${voices[i].name}`);
			}
			const c_ptr = FoundryRuntime.allocStringArray(arr);
			func(arr.length, c_ptr);
			FoundryRuntime.freeStringArray(c_ptr, arr.length);
		} catch (e) {
			// Fail graciously.
		}
	},

	foundry_js_tts_speak__proxy: 'sync',
	foundry_js_tts_speak__sig: 'viiiffii',
	foundry_js_tts_speak: function (p_text, p_voice, p_volume, p_pitch, p_rate, p_utterance_id, p_callback) {
		const func = FoundryRuntime.get_func(p_callback);

		function listener_end(evt) {
			evt.currentTarget.cb(1 /* TTS_UTTERANCE_ENDED */, evt.currentTarget.id, 0);
		}

		function listener_start(evt) {
			evt.currentTarget.cb(0 /* TTS_UTTERANCE_STARTED */, evt.currentTarget.id, 0);
		}

		function listener_error(evt) {
			evt.currentTarget.cb(2 /* TTS_UTTERANCE_CANCELED */, evt.currentTarget.id, 0);
		}

		function listener_bound(evt) {
			evt.currentTarget.cb(3 /* TTS_UTTERANCE_BOUNDARY */, evt.currentTarget.id, evt.charIndex);
		}

		const utterance = new SpeechSynthesisUtterance(FoundryRuntime.parseString(p_text));
		utterance.rate = p_rate;
		utterance.pitch = p_pitch;
		utterance.volume = p_volume / 100.0;
		utterance.addEventListener('end', listener_end);
		utterance.addEventListener('start', listener_start);
		utterance.addEventListener('error', listener_error);
		utterance.addEventListener('boundary', listener_bound);
		utterance.id = p_utterance_id;
		utterance.cb = func;
		const voice = FoundryRuntime.parseString(p_voice);
		const voices = window.speechSynthesis.getVoices();
		for (let i = 0; i < voices.length; i++) {
			if (voices[i].name === voice) {
				utterance.voice = voices[i];
				break;
			}
		}
		window.speechSynthesis.resume();
		window.speechSynthesis.speak(utterance);
	},

	foundry_js_tts_pause__proxy: 'sync',
	foundry_js_tts_pause__sig: 'v',
	foundry_js_tts_pause: function () {
		window.speechSynthesis.pause();
	},

	foundry_js_tts_resume__proxy: 'sync',
	foundry_js_tts_resume__sig: 'v',
	foundry_js_tts_resume: function () {
		window.speechSynthesis.resume();
	},

	foundry_js_tts_stop__proxy: 'sync',
	foundry_js_tts_stop__sig: 'v',
	foundry_js_tts_stop: function () {
		window.speechSynthesis.cancel();
		window.speechSynthesis.resume();
	},

	foundry_js_display_alert__proxy: 'sync',
	foundry_js_display_alert__sig: 'vi',
	foundry_js_display_alert: function (p_text) {
		window.alert(FoundryRuntime.parseString(p_text)); // eslint-disable-line no-alert
	},

	foundry_js_display_screen_dpi_get__proxy: 'sync',
	foundry_js_display_screen_dpi_get__sig: 'i',
	foundry_js_display_screen_dpi_get: function () {
		return FoundryDisplay.getDPI();
	},

	foundry_js_display_pixel_ratio_get__proxy: 'sync',
	foundry_js_display_pixel_ratio_get__sig: 'f',
	foundry_js_display_pixel_ratio_get: function () {
		return FoundryDisplayScreen.getPixelRatio();
	},

	foundry_js_display_fullscreen_request__proxy: 'sync',
	foundry_js_display_fullscreen_request__sig: 'i',
	foundry_js_display_fullscreen_request: function () {
		return FoundryDisplayScreen.requestFullscreen();
	},

	foundry_js_display_fullscreen_exit__proxy: 'sync',
	foundry_js_display_fullscreen_exit__sig: 'i',
	foundry_js_display_fullscreen_exit: function () {
		return FoundryDisplayScreen.exitFullscreen();
	},

	foundry_js_display_desired_size_set__proxy: 'sync',
	foundry_js_display_desired_size_set__sig: 'vii',
	foundry_js_display_desired_size_set: function (width, height) {
		FoundryDisplayScreen.desired_size = [width, height];
		FoundryDisplayScreen.updateSize();
	},

	foundry_js_display_size_update__proxy: 'sync',
	foundry_js_display_size_update__sig: 'i',
	foundry_js_display_size_update: function () {
		const updated = FoundryDisplayScreen.updateSize();
		if (updated) {
			FoundryDisplayVK.updateSize();
		}
		return updated;
	},

	foundry_js_display_screen_size_get__proxy: 'sync',
	foundry_js_display_screen_size_get__sig: 'vii',
	foundry_js_display_screen_size_get: function (width, height) {
		const scale = FoundryDisplayScreen.getPixelRatio();
		FoundryRuntime.setHeapValue(width, window.screen.width * scale, 'i32');
		FoundryRuntime.setHeapValue(height, window.screen.height * scale, 'i32');
	},

	foundry_js_display_window_size_get__proxy: 'sync',
	foundry_js_display_window_size_get__sig: 'vii',
	foundry_js_display_window_size_get: function (p_width, p_height) {
		FoundryRuntime.setHeapValue(p_width, FoundryConfig.canvas.width, 'i32');
		FoundryRuntime.setHeapValue(p_height, FoundryConfig.canvas.height, 'i32');
	},

	foundry_js_display_has_webgl__proxy: 'sync',
	foundry_js_display_has_webgl__sig: 'ii',
	foundry_js_display_has_webgl: function (p_version) {
		if (p_version !== 1 && p_version !== 2) {
			return false;
		}
		try {
			return !!document.createElement('canvas').getContext(p_version === 2 ? 'webgl2' : 'webgl');
		} catch (e) { /* Not available */ }
		return false;
	},

	/*
	 * Canvas
	 */
	foundry_js_display_canvas_focus__proxy: 'sync',
	foundry_js_display_canvas_focus__sig: 'v',
	foundry_js_display_canvas_focus: function () {
		FoundryConfig.canvas.focus();
	},

	foundry_js_display_canvas_is_focused__proxy: 'sync',
	foundry_js_display_canvas_is_focused__sig: 'i',
	foundry_js_display_canvas_is_focused: function () {
		return document.activeElement === FoundryConfig.canvas;
	},

	/*
	 * Touchscreen
	 */
	foundry_js_display_touchscreen_is_available__proxy: 'sync',
	foundry_js_display_touchscreen_is_available__sig: 'i',
	foundry_js_display_touchscreen_is_available: function () {
		return 'ontouchstart' in window;
	},

	/*
	 * Clipboard
	 */
	foundry_js_display_clipboard_set__proxy: 'sync',
	foundry_js_display_clipboard_set__sig: 'ii',
	foundry_js_display_clipboard_set: function (p_text) {
		const text = FoundryRuntime.parseString(p_text);
		if (!navigator.clipboard || !navigator.clipboard.writeText) {
			return 1;
		}
		navigator.clipboard.writeText(text).catch(function (e) {
			// Setting OS clipboard is only possible from an input callback.
			FoundryRuntime.error('Setting OS clipboard is only possible from an input callback for the Web platform. Exception:', e);
		});
		return 0;
	},

	foundry_js_display_clipboard_get__proxy: 'sync',
	foundry_js_display_clipboard_get__sig: 'ii',
	foundry_js_display_clipboard_get: function (callback) {
		const func = FoundryRuntime.get_func(callback);
		try {
			navigator.clipboard.readText().then(function (result) {
				const ptr = FoundryRuntime.allocString(result);
				func(ptr);
				FoundryRuntime.free(ptr);
			}).catch(function (e) {
				// Fail graciously.
			});
		} catch (e) {
			// Fail graciously.
		}
	},

	/*
	 * Window
	 */
	foundry_js_display_window_title_set__proxy: 'sync',
	foundry_js_display_window_title_set__sig: 'vi',
	foundry_js_display_window_title_set: function (p_data) {
		document.title = FoundryRuntime.parseString(p_data);
	},

	foundry_js_display_window_icon_set__proxy: 'sync',
	foundry_js_display_window_icon_set__sig: 'vii',
	foundry_js_display_window_icon_set: function (p_ptr, p_len) {
		let link = document.getElementById('-gd-engine-icon');
		const old_icon = FoundryDisplay.window_icon;
		if (p_ptr) {
			if (link === null) {
				link = document.createElement('link');
				link.rel = 'icon';
				link.id = '-gd-engine-icon';
				document.head.appendChild(link);
			}
			const png = new Blob([FoundryRuntime.heapSlice(HEAPU8, p_ptr, p_len)], { type: 'image/png' });
			FoundryDisplay.window_icon = URL.createObjectURL(png);
			link.href = FoundryDisplay.window_icon;
		} else {
			if (link) {
				link.remove();
			}
			FoundryDisplay.window_icon = null;
		}
		if (old_icon) {
			URL.revokeObjectURL(old_icon);
		}
	},

	/*
	 * Cursor
	 */
	foundry_js_display_cursor_set_visible__proxy: 'sync',
	foundry_js_display_cursor_set_visible__sig: 'vi',
	foundry_js_display_cursor_set_visible: function (p_visible) {
		const visible = p_visible !== 0;
		if (visible === FoundryDisplayCursor.visible) {
			return;
		}
		FoundryDisplayCursor.visible = visible;
		if (visible) {
			FoundryDisplayCursor.set_shape(FoundryDisplayCursor.shape);
		} else {
			FoundryDisplayCursor.set_style('none');
		}
	},

	foundry_js_display_cursor_is_hidden__proxy: 'sync',
	foundry_js_display_cursor_is_hidden__sig: 'i',
	foundry_js_display_cursor_is_hidden: function () {
		return !FoundryDisplayCursor.visible;
	},

	foundry_js_display_cursor_set_shape__proxy: 'sync',
	foundry_js_display_cursor_set_shape__sig: 'vi',
	foundry_js_display_cursor_set_shape: function (p_string) {
		FoundryDisplayCursor.set_shape(FoundryRuntime.parseString(p_string));
	},

	foundry_js_display_cursor_set_custom_shape__proxy: 'sync',
	foundry_js_display_cursor_set_custom_shape__sig: 'viiiii',
	foundry_js_display_cursor_set_custom_shape: function (p_shape, p_ptr, p_len, p_hotspot_x, p_hotspot_y) {
		const shape = FoundryRuntime.parseString(p_shape);
		const old_shape = FoundryDisplayCursor.cursors[shape];
		if (p_len > 0) {
			const png = new Blob([FoundryRuntime.heapSlice(HEAPU8, p_ptr, p_len)], { type: 'image/png' });
			const url = URL.createObjectURL(png);
			FoundryDisplayCursor.cursors[shape] = {
				url: url,
				x: p_hotspot_x,
				y: p_hotspot_y,
			};
		} else {
			delete FoundryDisplayCursor.cursors[shape];
		}
		if (shape === FoundryDisplayCursor.shape) {
			FoundryDisplayCursor.set_shape(FoundryDisplayCursor.shape);
		}
		if (old_shape) {
			URL.revokeObjectURL(old_shape.url);
		}
	},

	foundry_js_display_cursor_lock_set__proxy: 'sync',
	foundry_js_display_cursor_lock_set__sig: 'vi',
	foundry_js_display_cursor_lock_set: function (p_lock) {
		if (p_lock) {
			FoundryDisplayCursor.lockPointer();
		} else {
			FoundryDisplayCursor.releasePointer();
		}
	},

	foundry_js_display_cursor_is_locked__proxy: 'sync',
	foundry_js_display_cursor_is_locked__sig: 'i',
	foundry_js_display_cursor_is_locked: function () {
		return FoundryDisplayCursor.isPointerLocked() ? 1 : 0;
	},

	/*
	 * Listeners
	 */
	foundry_js_display_fullscreen_cb__proxy: 'sync',
	foundry_js_display_fullscreen_cb__sig: 'vi',
	foundry_js_display_fullscreen_cb: function (callback) {
		const canvas = FoundryConfig.canvas;
		const func = FoundryRuntime.get_func(callback);
		function change_cb(evt) {
			if (evt.target === canvas) {
				func(FoundryDisplayScreen.isFullscreen());
			}
		}
		FoundryEventListeners.add(document, 'fullscreenchange', change_cb, false);
		FoundryEventListeners.add(document, 'mozfullscreenchange', change_cb, false);
		FoundryEventListeners.add(document, 'webkitfullscreenchange', change_cb, false);
	},

	foundry_js_display_window_blur_cb__proxy: 'sync',
	foundry_js_display_window_blur_cb__sig: 'vi',
	foundry_js_display_window_blur_cb: function (callback) {
		const func = FoundryRuntime.get_func(callback);
		FoundryEventListeners.add(window, 'blur', function () {
			func();
		}, false);
	},

	foundry_js_display_notification_cb__proxy: 'sync',
	foundry_js_display_notification_cb__sig: 'viiiii',
	foundry_js_display_notification_cb: function (callback, p_enter, p_exit, p_in, p_out) {
		const canvas = FoundryConfig.canvas;
		const func = FoundryRuntime.get_func(callback);
		const notif = [p_enter, p_exit, p_in, p_out];
		['mouseover', 'mouseleave', 'focus', 'blur'].forEach(function (evt_name, idx) {
			FoundryEventListeners.add(canvas, evt_name, function () {
				func(notif[idx]);
			}, true);
		});
	},

	foundry_js_display_setup_canvas__proxy: 'sync',
	foundry_js_display_setup_canvas__sig: 'viiii',
	foundry_js_display_setup_canvas: function (p_width, p_height, p_fullscreen, p_hidpi) {
		const canvas = FoundryConfig.canvas;
		FoundryEventListeners.add(canvas, 'contextmenu', function (ev) {
			ev.preventDefault();
		}, false);
		FoundryEventListeners.add(canvas, 'webglcontextlost', function (ev) {
			alert('WebGL context lost, please reload the page'); // eslint-disable-line no-alert
			ev.preventDefault();
		}, false);
		FoundryDisplayScreen.hidpi = !!p_hidpi;
		switch (FoundryConfig.canvas_resize_policy) {
		case 0: // None
			FoundryDisplayScreen.desired_size = [canvas.width, canvas.height];
			break;
		case 1: // Project
			FoundryDisplayScreen.desired_size = [p_width, p_height];
			break;
		default: // Full window
			// Ensure we display in the right place, the size will be handled by updateSize
			canvas.style.position = 'absolute';
			canvas.style.top = 0;
			canvas.style.left = 0;
			break;
		}
		FoundryDisplayScreen.updateSize();
		if (p_fullscreen) {
			FoundryDisplayScreen.requestFullscreen();
		}
	},

	/*
	 * Virtual Keyboard
	 */
	foundry_js_display_vk_show__proxy: 'sync',
	foundry_js_display_vk_show__sig: 'viiii',
	foundry_js_display_vk_show: function (p_text, p_type, p_start, p_end) {
		const text = FoundryRuntime.parseString(p_text);
		const start = p_start > 0 ? p_start : 0;
		const end = p_end > 0 ? p_end : start;
		FoundryDisplayVK.show(text, p_type, start, end);
	},

	foundry_js_display_vk_hide__proxy: 'sync',
	foundry_js_display_vk_hide__sig: 'v',
	foundry_js_display_vk_hide: function () {
		FoundryDisplayVK.hide();
	},

	foundry_js_display_vk_available__proxy: 'sync',
	foundry_js_display_vk_available__sig: 'i',
	foundry_js_display_vk_available: function () {
		return FoundryDisplayVK.available();
	},

	foundry_js_display_tts_available__proxy: 'sync',
	foundry_js_display_tts_available__sig: 'i',
	foundry_js_display_tts_available: function () {
		return 'speechSynthesis' in window;
	},

	foundry_js_display_vk_cb__proxy: 'sync',
	foundry_js_display_vk_cb__sig: 'vi',
	foundry_js_display_vk_cb: function (p_input_cb) {
		const input_cb = FoundryRuntime.get_func(p_input_cb);
		if (FoundryDisplayVK.available()) {
			FoundryDisplayVK.init(input_cb);
		}
	},
};

autoAddDeps(FoundryDisplay, '$FoundryDisplay');
mergeInto(LibraryManager.library, FoundryDisplay);
