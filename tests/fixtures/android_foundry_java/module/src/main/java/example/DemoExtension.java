/**************************************************************************/
/*  DemoExtension.java                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

package example;

import games.cafecito.foundry.annotations.FoundryClass;
import games.cafecito.foundry.annotations.FoundryInitialization;
import games.cafecito.foundry.annotations.FoundryMethod;
import games.cafecito.foundry.annotations.InitializationLevel;
import games.cafecito.foundry.generated.classes.Node;
import games.cafecito.foundry.runtime.FoundryBindingContext;
import games.cafecito.foundry.runtime.ObjectLease;

/**
 * Minimal processor-generated extension used by the Android exporter integration tests.
 */
@FoundryClass(base = Node.class, name = "DemoExtension")
@FoundryInitialization(InitializationLevel.SCENE)
public final class DemoExtension extends Node {
	public DemoExtension(FoundryBindingContext context, ObjectLease lease) {
		super(context, lease);
	}

	@FoundryMethod(name = "callback_probe")
	public long callbackProbe(long value) {
		return value + 1;
	}
}
