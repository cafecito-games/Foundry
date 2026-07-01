/**************************************************************************/
/*  library_foundry_webxr.js                                                */
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

const FoundryWebXR = {
	$FoundryWebXR__deps: ['$MainLoop', '$GL', '$FoundryRuntime', '$runtimeKeepalivePush', '$runtimeKeepalivePop'],
	$FoundryWebXR: {
		gl: null,

		session: null,
		gl_binding: null,
		layer: null,
		space: null,
		frame: null,
		pose: null,
		view_count: 1,
		input_sources: new Array(16),
		touches: new Array(5),
		onsimpleevent: null,

		// Monkey-patch the requestAnimationFrame() used by Emscripten for the main
		// loop, so that we can swap it out for XRSession.requestAnimationFrame()
		// when an XR session is started.
		orig_requestAnimationFrame: null,
		requestAnimationFrame: (callback) => {
			if (FoundryWebXR.session && FoundryWebXR.space) {
				const onFrame = function (time, frame) {
					FoundryWebXR.frame = frame;
					FoundryWebXR.pose = frame.getViewerPose(FoundryWebXR.space);
					callback(time);
					FoundryWebXR.frame = null;
					FoundryWebXR.pose = null;
				};
				FoundryWebXR.session.requestAnimationFrame(onFrame);
			} else {
				FoundryWebXR.orig_requestAnimationFrame(callback);
			}
		},
		monkeyPatchRequestAnimationFrame: (enable) => {
			if (FoundryWebXR.orig_requestAnimationFrame === null) {
				FoundryWebXR.orig_requestAnimationFrame = MainLoop.requestAnimationFrame;
			}
			MainLoop.requestAnimationFrame = enable
				? FoundryWebXR.requestAnimationFrame
				: FoundryWebXR.orig_requestAnimationFrame;
		},
		pauseResumeMainLoop: () => {
			// Once both FoundryWebXR.session and FoundryWebXR.space are set or
			// unset, our monkey-patched requestAnimationFrame() should be
			// enabled or disabled. When using the WebXR API Emulator, this
			// gets picked up automatically, however, in the Oculus Browser
			// on the Quest, we need to pause and resume the main loop.
			MainLoop.pause();
			runtimeKeepalivePush();
			window.setTimeout(function () {
				runtimeKeepalivePop();
				MainLoop.resume();
			}, 0);
		},

		getLayer: () => {
			const new_view_count = (FoundryWebXR.pose) ? FoundryWebXR.pose.views.length : 1;
			let layer = FoundryWebXR.layer;

			// If the view count hasn't changed since creating this layer, then
			// we can simply return it.
			if (layer && FoundryWebXR.view_count === new_view_count) {
				return layer;
			}

			if (!FoundryWebXR.session || !FoundryWebXR.gl_binding || !FoundryWebXR.gl_binding.createProjectionLayer) {
				return null;
			}

			const gl = FoundryWebXR.gl;

			layer = FoundryWebXR.gl_binding.createProjectionLayer({
				textureType: new_view_count > 1 ? 'texture-array' : 'texture',
				colorFormat: gl.RGBA8,
				depthFormat: gl.DEPTH_COMPONENT24,
			});
			FoundryWebXR.session.updateRenderState({ layers: [layer] });

			FoundryWebXR.layer = layer;
			FoundryWebXR.view_count = new_view_count;
			return layer;
		},

		getSubImage: () => {
			if (!FoundryWebXR.pose) {
				return null;
			}
			const layer = FoundryWebXR.getLayer();
			if (layer === null) {
				return null;
			}

			// Because we always use "texture-array" for multiview and "texture"
			// when there is only 1 view, it should be safe to only grab the
			// subimage for the first view.
			return FoundryWebXR.gl_binding.getViewSubImage(layer, FoundryWebXR.pose.views[0]);
		},

		getTextureId: (texture) => {
			if (texture.name !== undefined) {
				return texture.name;
			}

			const id = GL.getNewId(GL.textures);
			texture.name = id;
			GL.textures[id] = texture;

			return id;
		},

		addInputSource: (input_source) => {
			let name = -1;
			if (input_source.targetRayMode === 'tracked-pointer' && input_source.handedness === 'left') {
				name = 0;
			} else if (input_source.targetRayMode === 'tracked-pointer' && input_source.handedness === 'right') {
				name = 1;
			} else {
				for (let i = 2; i < 16; i++) {
					if (!FoundryWebXR.input_sources[i]) {
						name = i;
						break;
					}
				}
			}
			if (name >= 0) {
				FoundryWebXR.input_sources[name] = input_source;
				input_source.name = name;

				// Find a free touch index for screen sources.
				if (input_source.targetRayMode === 'screen') {
					let touch_index = -1;
					for (let i = 0; i < 5; i++) {
						if (!FoundryWebXR.touches[i]) {
							touch_index = i;
							break;
						}
					}
					if (touch_index >= 0) {
						FoundryWebXR.touches[touch_index] = input_source;
						input_source.touch_index = touch_index;
					}
				}
			}
			return name;
		},

		removeInputSource: (input_source) => {
			if (input_source.name !== undefined) {
				const name = input_source.name;
				if (name >= 0 && name < 16) {
					FoundryWebXR.input_sources[name] = null;
				}

				if (input_source.touch_index !== undefined) {
					const touch_index = input_source.touch_index;
					if (touch_index >= 0 && touch_index < 5) {
						FoundryWebXR.touches[touch_index] = null;
					}
				}
				return name;
			}
			return -1;
		},

		getInputSourceId: (input_source) => {
			if (input_source !== undefined) {
				return input_source.name;
			}
			return -1;
		},

		getTouchIndex: (input_source) => {
			if (input_source.touch_index !== undefined) {
				return input_source.touch_index;
			}
			return -1;
		},
	},

	foundry_webxr_is_supported__proxy: 'sync',
	foundry_webxr_is_supported__sig: 'i',
	foundry_webxr_is_supported: function () {
		return !!navigator.xr;
	},

	foundry_webxr_is_session_supported__proxy: 'sync',
	foundry_webxr_is_session_supported__sig: 'vii',
	foundry_webxr_is_session_supported: function (p_session_mode, p_callback) {
		const session_mode = FoundryRuntime.parseString(p_session_mode);
		const cb = FoundryRuntime.get_func(p_callback);
		if (navigator.xr) {
			navigator.xr.isSessionSupported(session_mode).then(function (supported) {
				const c_str = FoundryRuntime.allocString(session_mode);
				cb(c_str, supported ? 1 : 0);
				FoundryRuntime.free(c_str);
			});
		} else {
			const c_str = FoundryRuntime.allocString(session_mode);
			cb(c_str, 0);
			FoundryRuntime.free(c_str);
		}
	},

	foundry_webxr_initialize__deps: ['emscripten_webgl_get_current_context'],
	foundry_webxr_initialize__proxy: 'sync',
	foundry_webxr_initialize__sig: 'viiiiiiiii',
	foundry_webxr_initialize: function (p_session_mode, p_required_features, p_optional_features, p_requested_reference_spaces, p_on_session_started, p_on_session_ended, p_on_session_failed, p_on_input_event, p_on_simple_event) {
		FoundryWebXR.monkeyPatchRequestAnimationFrame(true);

		const session_mode = FoundryRuntime.parseString(p_session_mode);
		const required_features = FoundryRuntime.parseString(p_required_features).split(',').map((s) => s.trim()).filter((s) => s !== '');
		const optional_features = FoundryRuntime.parseString(p_optional_features).split(',').map((s) => s.trim()).filter((s) => s !== '');
		const requested_reference_space_types = FoundryRuntime.parseString(p_requested_reference_spaces).split(',').map((s) => s.trim());
		const onstarted = FoundryRuntime.get_func(p_on_session_started);
		const onended = FoundryRuntime.get_func(p_on_session_ended);
		const onfailed = FoundryRuntime.get_func(p_on_session_failed);
		const oninputevent = FoundryRuntime.get_func(p_on_input_event);
		const onsimpleevent = FoundryRuntime.get_func(p_on_simple_event);

		const session_init = {};
		if (required_features.length > 0) {
			session_init['requiredFeatures'] = required_features;
		}
		if (optional_features.length > 0) {
			session_init['optionalFeatures'] = optional_features;
		}

		navigator.xr.requestSession(session_mode, session_init).then(function (session) {
			FoundryWebXR.session = session;

			session.addEventListener('end', function (evt) {
				onended();
			});

			session.addEventListener('inputsourceschange', function (evt) {
				evt.added.forEach(FoundryWebXR.addInputSource);
				evt.removed.forEach(FoundryWebXR.removeInputSource);
			});

			['selectstart', 'selectend', 'squeezestart', 'squeezeend'].forEach((input_event, index) => {
				session.addEventListener(input_event, function (evt) {
					// Since this happens in-between normal frames, we need to
					// grab the frame from the event in order to get poses for
					// the input sources.
					FoundryWebXR.frame = evt.frame;
					oninputevent(index, FoundryWebXR.getInputSourceId(evt.inputSource));
					FoundryWebXR.frame = null;
				});
			});

			session.addEventListener('visibilitychange', function (evt) {
				const c_str = FoundryRuntime.allocString('visibility_state_changed');
				onsimpleevent(c_str);
				FoundryRuntime.free(c_str);
			});

			// Store onsimpleevent so we can use it later.
			FoundryWebXR.onsimpleevent = onsimpleevent;

			const gl_context_handle = _emscripten_webgl_get_current_context();
			const gl = GL.getContext(gl_context_handle).GLctx;
			FoundryWebXR.gl = gl;

			gl.makeXRCompatible().then(function () {
				const throwNoWebXRLayersError = () => {
					throw new Error('This browser doesn\'t support WebXR Layers (which Godot requires) nor is the polyfill in use. If you are the developer of this application, please consider including the polyfill.');
				};

				try {
					FoundryWebXR.gl_binding = new XRWebGLBinding(session, gl);
				} catch (error) {
					// We'll end up here for browsers that don't have XRWebGLBinding at all, or if the browser does support WebXR Layers,
					// but is using the WebXR polyfill, so calling native XRWebGLBinding with the polyfilled XRSession won't work.
					throwNoWebXRLayersError();
				}

				if (!FoundryWebXR.gl_binding.createProjectionLayer) {
					// On other browsers, XRWebGLBinding exists and works, but it doesn't support creating projection layers (which is
					// contrary to the spec, which says this MUST be supported) and so the polyfill is required.
					throwNoWebXRLayersError();
				}

				// This will trigger the layer to get created.
				const layer = FoundryWebXR.getLayer();
				if (!layer) {
					throw new Error('Unable to create WebXR Layer.');
				}

				function onReferenceSpaceSuccess(reference_space, reference_space_type) {
					FoundryWebXR.space = reference_space;

					// Using reference_space.addEventListener() crashes when
					// using the polyfill with the WebXR Emulator extension,
					// so we set the event property instead.
					reference_space.onreset = function (evt) {
						const c_str = FoundryRuntime.allocString('reference_space_reset');
						onsimpleevent(c_str);
						FoundryRuntime.free(c_str);
					};

					// Now that both FoundryWebXR.session and FoundryWebXR.space are
					// set, we need to pause and resume the main loop for the XR
					// main loop to kick in.
					FoundryWebXR.pauseResumeMainLoop();

					// Call in setTimeout() so that errors in the onstarted()
					// callback don't bubble up here and cause Godot to try the
					// next reference space.
					window.setTimeout(function () {
						const reference_space_c_str = FoundryRuntime.allocString(reference_space_type);
						const enabled_features = 'enabledFeatures' in session ? Array.from(session.enabledFeatures) : [];
						const enabled_features_c_str = FoundryRuntime.allocString(enabled_features.join(','));
						const environment_blend_mode = 'environmentBlendMode' in session ? session.environmentBlendMode : '';
						const environment_blend_mode_c_str = FoundryRuntime.allocString(environment_blend_mode);
						onstarted(reference_space_c_str, enabled_features_c_str, environment_blend_mode_c_str);
						FoundryRuntime.free(reference_space_c_str);
						FoundryRuntime.free(enabled_features_c_str);
						FoundryRuntime.free(environment_blend_mode_c_str);
					}, 0);
				}

				function requestReferenceSpace() {
					const reference_space_type = requested_reference_space_types.shift();
					session.requestReferenceSpace(reference_space_type)
						.then((refSpace) => {
							onReferenceSpaceSuccess(refSpace, reference_space_type);
						})
						.catch(() => {
							if (requested_reference_space_types.length === 0) {
								const c_str = FoundryRuntime.allocString('Unable to get any of the requested reference space types');
								onfailed(c_str);
								FoundryRuntime.free(c_str);
							} else {
								requestReferenceSpace();
							}
						});
				}

				requestReferenceSpace();
			}).catch(function (error) {
				const c_str = FoundryRuntime.allocString(`Unable to make WebGL context compatible with WebXR: ${error}`);
				onfailed(c_str);
				FoundryRuntime.free(c_str);
			});
		}).catch(function (error) {
			const c_str = FoundryRuntime.allocString(`Unable to start session: ${error}`);
			onfailed(c_str);
			FoundryRuntime.free(c_str);
		});
	},

	foundry_webxr_uninitialize__proxy: 'sync',
	foundry_webxr_uninitialize__sig: 'v',
	foundry_webxr_uninitialize: function () {
		if (FoundryWebXR.session) {
			FoundryWebXR.session.end()
				// Prevent exception when session has already ended.
				.catch((e) => { });
		}

		FoundryWebXR.session = null;
		FoundryWebXR.gl_binding = null;
		FoundryWebXR.layer = null;
		FoundryWebXR.space = null;
		FoundryWebXR.frame = null;
		FoundryWebXR.pose = null;
		FoundryWebXR.view_count = 1;
		FoundryWebXR.input_sources = new Array(16);
		FoundryWebXR.touches = new Array(5);
		FoundryWebXR.onsimpleevent = null;

		// Disable the monkey-patched window.requestAnimationFrame() and
		// pause/restart the main loop to activate it on all platforms.
		FoundryWebXR.monkeyPatchRequestAnimationFrame(false);
		FoundryWebXR.pauseResumeMainLoop();
	},

	foundry_webxr_get_view_count__proxy: 'sync',
	foundry_webxr_get_view_count__sig: 'i',
	foundry_webxr_get_view_count: function () {
		if (!FoundryWebXR.session || !FoundryWebXR.pose) {
			return 1;
		}
		const view_count = FoundryWebXR.pose.views.length;
		return view_count > 0 ? view_count : 1;
	},

	foundry_webxr_get_render_target_size__proxy: 'sync',
	foundry_webxr_get_render_target_size__sig: 'ii',
	foundry_webxr_get_render_target_size: function (r_size) {
		const subimage = FoundryWebXR.getSubImage();
		if (subimage === null) {
			return false;
		}

		FoundryRuntime.setHeapValue(r_size + 0, subimage.viewport.width, 'i32');
		FoundryRuntime.setHeapValue(r_size + 4, subimage.viewport.height, 'i32');

		return true;
	},

	foundry_webxr_get_transform_for_view__proxy: 'sync',
	foundry_webxr_get_transform_for_view__sig: 'iii',
	foundry_webxr_get_transform_for_view: function (p_view, r_transform) {
		if (!FoundryWebXR.session || !FoundryWebXR.pose) {
			return false;
		}

		const views = FoundryWebXR.pose.views;
		let matrix;
		if (p_view >= 0) {
			matrix = views[p_view].transform.matrix;
		} else {
			// For -1 (or any other negative value) return the HMD transform.
			matrix = FoundryWebXR.pose.transform.matrix;
		}

		for (let i = 0; i < 16; i++) {
			FoundryRuntime.setHeapValue(r_transform + (i * 4), matrix[i], 'float');
		}

		return true;
	},

	foundry_webxr_get_projection_for_view__proxy: 'sync',
	foundry_webxr_get_projection_for_view__sig: 'iii',
	foundry_webxr_get_projection_for_view: function (p_view, r_transform) {
		if (!FoundryWebXR.session || !FoundryWebXR.pose) {
			return false;
		}

		const matrix = FoundryWebXR.pose.views[p_view].projectionMatrix;
		for (let i = 0; i < 16; i++) {
			FoundryRuntime.setHeapValue(r_transform + (i * 4), matrix[i], 'float');
		}

		return true;
	},

	foundry_webxr_get_color_texture__proxy: 'sync',
	foundry_webxr_get_color_texture__sig: 'i',
	foundry_webxr_get_color_texture: function () {
		const subimage = FoundryWebXR.getSubImage();
		if (subimage === null) {
			return 0;
		}
		return FoundryWebXR.getTextureId(subimage.colorTexture);
	},

	foundry_webxr_get_depth_texture__proxy: 'sync',
	foundry_webxr_get_depth_texture__sig: 'i',
	foundry_webxr_get_depth_texture: function () {
		const subimage = FoundryWebXR.getSubImage();
		if (subimage === null) {
			return 0;
		}
		if (!subimage.depthStencilTexture) {
			return 0;
		}
		return FoundryWebXR.getTextureId(subimage.depthStencilTexture);
	},

	foundry_webxr_get_velocity_texture__proxy: 'sync',
	foundry_webxr_get_velocity_texture__sig: 'i',
	foundry_webxr_get_velocity_texture: function () {
		const subimage = FoundryWebXR.getSubImage();
		if (subimage === null) {
			return 0;
		}
		if (!subimage.motionVectorTexture) {
			return 0;
		}
		return FoundryWebXR.getTextureId(subimage.motionVectorTexture);
	},

	foundry_webxr_update_input_source__proxy: 'sync',
	foundry_webxr_update_input_source__sig: 'iiiiiiiiiiiiiii',
	foundry_webxr_update_input_source: function (p_input_source_id, r_target_pose, r_target_ray_mode, r_touch_index, r_has_grip_pose, r_grip_pose, r_has_standard_mapping, r_button_count, r_buttons, r_axes_count, r_axes, r_has_hand_data, r_hand_joints, r_hand_radii) {
		if (!FoundryWebXR.session || !FoundryWebXR.frame) {
			return 0;
		}

		if (p_input_source_id < 0 || p_input_source_id >= FoundryWebXR.input_sources.length || !FoundryWebXR.input_sources[p_input_source_id]) {
			return false;
		}

		const input_source = FoundryWebXR.input_sources[p_input_source_id];
		const frame = FoundryWebXR.frame;
		const space = FoundryWebXR.space;

		// Target pose.
		const target_pose = frame.getPose(input_source.targetRaySpace, space);
		if (!target_pose) {
			// This can mean that the controller lost tracking.
			return false;
		}
		const target_pose_matrix = target_pose.transform.matrix;
		for (let i = 0; i < 16; i++) {
			FoundryRuntime.setHeapValue(r_target_pose + (i * 4), target_pose_matrix[i], 'float');
		}

		// Target ray mode.
		let target_ray_mode = 0;
		switch (input_source.targetRayMode) {
		case 'gaze':
			target_ray_mode = 1;
			break;

		case 'tracked-pointer':
			target_ray_mode = 2;
			break;

		case 'screen':
			target_ray_mode = 3;
			break;

		default:
		}
		FoundryRuntime.setHeapValue(r_target_ray_mode, target_ray_mode, 'i32');

		// Touch index.
		FoundryRuntime.setHeapValue(r_touch_index, FoundryWebXR.getTouchIndex(input_source), 'i32');

		// Grip pose.
		let has_grip_pose = false;
		if (input_source.gripSpace) {
			const grip_pose = frame.getPose(input_source.gripSpace, space);
			if (grip_pose) {
				const grip_pose_matrix = grip_pose.transform.matrix;
				for (let i = 0; i < 16; i++) {
					FoundryRuntime.setHeapValue(r_grip_pose + (i * 4), grip_pose_matrix[i], 'float');
				}
				has_grip_pose = true;
			}
		}
		FoundryRuntime.setHeapValue(r_has_grip_pose, has_grip_pose ? 1 : 0, 'i32');

		// Gamepad data (mapping, buttons and axes).
		let has_standard_mapping = false;
		let button_count = 0;
		let axes_count = 0;
		if (input_source.gamepad) {
			if (input_source.gamepad.mapping === 'xr-standard') {
				has_standard_mapping = true;
			}

			button_count = Math.min(input_source.gamepad.buttons.length, 10);
			for (let i = 0; i < button_count; i++) {
				FoundryRuntime.setHeapValue(r_buttons + (i * 4), input_source.gamepad.buttons[i].value, 'float');
			}

			axes_count = Math.min(input_source.gamepad.axes.length, 10);
			for (let i = 0; i < axes_count; i++) {
				FoundryRuntime.setHeapValue(r_axes + (i * 4), input_source.gamepad.axes[i], 'float');
			}
		}
		FoundryRuntime.setHeapValue(r_has_standard_mapping, has_standard_mapping ? 1 : 0, 'i32');
		FoundryRuntime.setHeapValue(r_button_count, button_count, 'i32');
		FoundryRuntime.setHeapValue(r_axes_count, axes_count, 'i32');

		// Hand tracking data.
		let has_hand_data = false;
		if (input_source.hand && r_hand_joints !== 0 && r_hand_radii !== 0) {
			const hand_joint_array = new Float32Array(25 * 16);
			const hand_radii_array = new Float32Array(25);
			if (frame.fillPoses(input_source.hand.values(), space, hand_joint_array) && frame.fillJointRadii(input_source.hand.values(), hand_radii_array)) {
				FoundryRuntime.heapCopy(HEAPF32, hand_joint_array, r_hand_joints);
				FoundryRuntime.heapCopy(HEAPF32, hand_radii_array, r_hand_radii);
				has_hand_data = true;
			}
		}
		FoundryRuntime.setHeapValue(r_has_hand_data, has_hand_data ? 1 : 0, 'i32');

		return true;
	},

	foundry_webxr_get_visibility_state__proxy: 'sync',
	foundry_webxr_get_visibility_state__sig: 'i',
	foundry_webxr_get_visibility_state: function () {
		if (!FoundryWebXR.session || !FoundryWebXR.session.visibilityState) {
			return 0;
		}

		return FoundryRuntime.allocString(FoundryWebXR.session.visibilityState);
	},

	foundry_webxr_get_bounds_geometry__proxy: 'sync',
	foundry_webxr_get_bounds_geometry__sig: 'ii',
	foundry_webxr_get_bounds_geometry: function (r_points) {
		if (!FoundryWebXR.space || !FoundryWebXR.space.boundsGeometry) {
			return 0;
		}

		const point_count = FoundryWebXR.space.boundsGeometry.length;
		if (point_count === 0) {
			return 0;
		}

		const buf = FoundryRuntime.malloc(point_count * 3 * 4);
		for (let i = 0; i < point_count; i++) {
			const point = FoundryWebXR.space.boundsGeometry[i];
			FoundryRuntime.setHeapValue(buf + ((i * 3) + 0) * 4, point.x, 'float');
			FoundryRuntime.setHeapValue(buf + ((i * 3) + 1) * 4, point.y, 'float');
			FoundryRuntime.setHeapValue(buf + ((i * 3) + 2) * 4, point.z, 'float');
		}
		FoundryRuntime.setHeapValue(r_points, buf, 'i32');

		return point_count;
	},

	foundry_webxr_get_frame_rate__proxy: 'sync',
	foundry_webxr_get_frame_rate__sig: 'i',
	foundry_webxr_get_frame_rate: function () {
		if (!FoundryWebXR.session || FoundryWebXR.session.frameRate === undefined) {
			return 0;
		}
		return FoundryWebXR.session.frameRate;
	},

	foundry_webxr_update_target_frame_rate__proxy: 'sync',
	foundry_webxr_update_target_frame_rate__sig: 'vi',
	foundry_webxr_update_target_frame_rate: function (p_frame_rate) {
		if (!FoundryWebXR.session || FoundryWebXR.session.updateTargetFrameRate === undefined) {
			return;
		}

		FoundryWebXR.session.updateTargetFrameRate(p_frame_rate).then(() => {
			const c_str = FoundryRuntime.allocString('display_refresh_rate_changed');
			FoundryWebXR.onsimpleevent(c_str);
			FoundryRuntime.free(c_str);
		});
	},

	foundry_webxr_get_supported_frame_rates__proxy: 'sync',
	foundry_webxr_get_supported_frame_rates__sig: 'ii',
	foundry_webxr_get_supported_frame_rates: function (r_frame_rates) {
		if (!FoundryWebXR.session || FoundryWebXR.session.supportedFrameRates === undefined) {
			return 0;
		}

		const frame_rate_count = FoundryWebXR.session.supportedFrameRates.length;
		if (frame_rate_count === 0) {
			return 0;
		}

		const buf = FoundryRuntime.malloc(frame_rate_count * 4);
		for (let i = 0; i < frame_rate_count; i++) {
			FoundryRuntime.setHeapValue(buf + (i * 4), FoundryWebXR.session.supportedFrameRates[i], 'float');
		}
		FoundryRuntime.setHeapValue(r_frame_rates, buf, 'i32');

		return frame_rate_count;
	},

};

autoAddDeps(FoundryWebXR, '$FoundryWebXR');
mergeInto(LibraryManager.library, FoundryWebXR);
