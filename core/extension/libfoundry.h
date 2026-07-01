/**************************************************************************/
/*  libfoundry.h                                                          */
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

#include "foundry_extension_interface.gen.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Export macros for DLL visibility
#if defined(_MSC_VER) || defined(__MINGW32__)
#define LIBFOUNDRY_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define LIBFOUNDRY_API __attribute__((visibility("default")))
#else
#define LIBFOUNDRY_API
#endif

/**
 * @name libfoundry_create_foundry_instance
 * @since 4.6
 *
 * Creates a new Foundry instance.
 *
 * @param p_argc The number of command line arguments.
 * @param p_argv The C-style array of command line arguments.
 * @param p_init_func FoundryExtension initialization function of the host application.
 *
 * @return A pointer to created \ref FoundryInstance FoundryExtension object or nullptr if there was an error.
 */
LIBFOUNDRY_API FoundryExtensionObjectPtr libfoundry_create_foundry_instance(int p_argc, char *p_argv[], FoundryExtensionInitializationFunction p_init_func);

/**
 * @name libfoundry_destroy_foundry_instance
 * @since 4.6
 *
 * Destroys an existing Foundry instance.
 *
 * @param p_foundry_instance The reference to the FoundryInstance object to destroy.
 *
 */
LIBFOUNDRY_API void libfoundry_destroy_foundry_instance(FoundryExtensionObjectPtr p_foundry_instance);

#ifdef __cplusplus
}
#endif // __cplusplus
