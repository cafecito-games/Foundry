using System;
using System.Runtime.InteropServices;
using Godot.NativeInterop;

namespace Godot
{
    public static class Dispatcher
    {
        internal static FoundryTaskScheduler DefaultGodotTaskScheduler;

        internal static void InitializeDefaultGodotTaskScheduler()
        {
            DefaultGodotTaskScheduler?.Dispose();
            DefaultGodotTaskScheduler = new FoundryTaskScheduler();
        }

        public static FoundrySynchronizationContext SynchronizationContext => DefaultGodotTaskScheduler.Context;
    }
}
