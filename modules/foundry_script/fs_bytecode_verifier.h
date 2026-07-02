/**************************************************************************/
/*  fs_bytecode_verifier.h                                                */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"

class FSFunction;

// Link-time bounds checker for a deserialized `.fsb` opcode stream. The release VM performs no
// bounds checking on the instruction stream (`GET_VARIANT_PTR` is unchecked without `DEBUG_ENABLED`,
// and `GD_ERR_BREAK` compiles to nothing), so a hostile or corrupted `.fsb` that passed the loader's
// structural checks could still make the VM read or write out of bounds at execution time. The
// verifier walks every deserialized function's opcode stream once — mirroring the exact per-opcode
// operand layout the VM uses to advance `ip` — and rejects any instruction whose length overruns the
// code, any operand address outside its address space (stack/constant/member), any jump target
// outside the code (and, more strictly, not landing on an instruction boundary), and any table index
// (operators, setters/getters, keyed/indexed setters/getters, builtin methods, constructors,
// utilities, script utilities, method binds, lambdas, global names, the language global array) that
// is out of range for its table. It runs in all builds and has no `DEBUG_ENABLED` dependency.
class FSBytecodeVerifier {
public:
	// `p_member_address_count` is the size of the member address space the function may reference —
	// the owning class's flattened member count. It is an upper bound: a witness dispatched without
	// an instance has an empty member space, but verifying against the class member count never
	// rejects a well-formed function and keeps the check independent of any concrete instance.
	static Error verify_function(const FSFunction *p_function, int p_member_address_count, const String &p_script_path);
};
