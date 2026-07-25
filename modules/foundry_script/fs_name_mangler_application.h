/**************************************************************************/
/*  fs_name_mangler_application.h                                         */
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

#pragma once

#include "foundry_script.h"

#include "core/error/error_list.h"
#include "core/templates/rb_map.h"

#ifdef TOOLS_ENABLED

class FSNameManglerApplication {
public:
	struct Diagnostic {
		String surface;
		StringName source_name;
		String message;

		String format() const;
	};

	class Transaction {
	public:
		enum State {
			STATE_UNUSED,
			STATE_ACTIVE,
			STATE_FINISHED,
		};

	private:
		struct Data;

		State state = STATE_UNUSED;
		Data *data = nullptr;

		Error _fail(const String &p_surface, const StringName &p_source_name,
				const String &p_message, Error p_error, Vector<Diagnostic> &r_diagnostics);

	public:
		Error begin(const Vector<Ref<FoundryScript>> &p_scripts,
				const RBMap<StringName, StringName> &p_rename_map,
				Vector<Diagnostic> &r_diagnostics);
		void rollback();
		bool is_active() const;
		State get_state() const;

		Transaction();
		~Transaction();
		Transaction(const Transaction &) = delete;
		Transaction &operator=(const Transaction &) = delete;
		Transaction(Transaction &&) = delete;
		Transaction &operator=(Transaction &&) = delete;
	};
};

#endif // TOOLS_ENABLED
