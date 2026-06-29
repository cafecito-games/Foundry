/**************************************************************************/
/*  webrtc_data_channel_extension.cpp                                     */
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

#include "webrtc_data_channel_extension.h"

void WebRTCDataChannelExtension::_bind_methods() {
	ADD_PROPERTY_DEFAULT("write_mode", WRITE_MODE_BINARY);

	FOUNDRY_VIRTUAL_BIND(_get_packet, "r_buffer", "r_buffer_size");
	FOUNDRY_VIRTUAL_BIND(_put_packet, "p_buffer", "p_buffer_size");
	FOUNDRY_VIRTUAL_BIND(_get_available_packet_count);
	FOUNDRY_VIRTUAL_BIND(_get_max_packet_size);

	FOUNDRY_VIRTUAL_BIND(_poll);
	FOUNDRY_VIRTUAL_BIND(_close);

	FOUNDRY_VIRTUAL_BIND(_set_write_mode, "p_write_mode");
	FOUNDRY_VIRTUAL_BIND(_get_write_mode);

	FOUNDRY_VIRTUAL_BIND(_was_string_packet);
	FOUNDRY_VIRTUAL_BIND(_get_ready_state);
	FOUNDRY_VIRTUAL_BIND(_get_label);
	FOUNDRY_VIRTUAL_BIND(_is_ordered);
	FOUNDRY_VIRTUAL_BIND(_get_id);
	FOUNDRY_VIRTUAL_BIND(_get_max_packet_life_time);
	FOUNDRY_VIRTUAL_BIND(_get_max_retransmits);
	FOUNDRY_VIRTUAL_BIND(_get_protocol);
	FOUNDRY_VIRTUAL_BIND(_is_negotiated);
	FOUNDRY_VIRTUAL_BIND(_get_buffered_amount);
}

Error WebRTCDataChannelExtension::get_packet(const uint8_t **r_buffer, int &r_buffer_size) {
	Error err;
	if (FOUNDRY_VIRTUAL_CALL(_get_packet, r_buffer, &r_buffer_size, err)) {
		return err;
	}
	WARN_PRINT_ONCE("WebRTCDataChannelExtension::_get_packet_native is unimplemented!");
	return FAILED;
}

Error WebRTCDataChannelExtension::put_packet(const uint8_t *p_buffer, int p_buffer_size) {
	Error err;
	if (FOUNDRY_VIRTUAL_CALL(_put_packet, p_buffer, p_buffer_size, err)) {
		return err;
	}
	WARN_PRINT_ONCE("WebRTCDataChannelExtension::_put_packet_native is unimplemented!");
	return FAILED;
}
