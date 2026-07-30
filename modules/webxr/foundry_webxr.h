/**************************************************************************/
/*  foundry_webxr.h                                                       */
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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum WebXRInputEvent {
	WEBXR_INPUT_EVENT_SELECTSTART,
	WEBXR_INPUT_EVENT_SELECTEND,
	WEBXR_INPUT_EVENT_SQUEEZESTART,
	WEBXR_INPUT_EVENT_SQUEEZEEND,
};

typedef void (*FoundryWebXRSupportedCallback)(char *p_session_mode, int p_supported);
typedef void (*FoundryWebXRStartedCallback)(char *p_reference_space_type, char *p_enabled_features, char *p_environment_blend_mode);
typedef void (*FoundryWebXREndedCallback)();
typedef void (*FoundryWebXRFailedCallback)(char *p_message);
typedef void (*FoundryWebXRInputEventCallback)(int p_event_type, int p_input_source_id);
typedef void (*FoundryWebXRSimpleEventCallback)(char *p_signal_name);

extern int foundry_webxr_is_supported();
extern void foundry_webxr_is_session_supported(const char *p_session_mode, FoundryWebXRSupportedCallback p_callback);

extern void foundry_webxr_initialize(
		const char *p_session_mode,
		const char *p_required_features,
		const char *p_optional_features,
		const char *p_requested_reference_space_types,
		FoundryWebXRStartedCallback p_on_session_started,
		FoundryWebXREndedCallback p_on_session_ended,
		FoundryWebXRFailedCallback p_on_session_failed,
		FoundryWebXRInputEventCallback p_on_input_event,
		FoundryWebXRSimpleEventCallback p_on_simple_event);
extern void foundry_webxr_uninitialize();

extern int foundry_webxr_get_view_count();
extern bool foundry_webxr_get_render_target_size(int *r_size);
extern bool foundry_webxr_get_transform_for_view(int p_view, float *r_transform);
extern bool foundry_webxr_get_projection_for_view(int p_view, float *r_transform);
extern unsigned int foundry_webxr_get_color_texture();
extern unsigned int foundry_webxr_get_depth_texture();
extern unsigned int foundry_webxr_get_velocity_texture();

extern bool foundry_webxr_update_input_source(
		int p_input_source_id,
		float *r_target_pose,
		int *r_target_ray_mode,
		int *r_touch_index,
		int *r_has_grip_pose,
		float *r_grip_pose,
		int *r_has_standard_mapping,
		int *r_button_count,
		float *r_buttons,
		int *r_axes_count,
		float *r_axes,
		int *r_has_hand_data,
		float *r_hand_joints,
		float *r_hand_radii);

extern char *foundry_webxr_get_visibility_state();
extern int foundry_webxr_get_bounds_geometry(float **r_points);

extern float foundry_webxr_get_frame_rate();
extern void foundry_webxr_update_target_frame_rate(float p_frame_rate);
extern int foundry_webxr_get_supported_frame_rates(float **r_frame_rates);

#ifdef __cplusplus
}
#endif
