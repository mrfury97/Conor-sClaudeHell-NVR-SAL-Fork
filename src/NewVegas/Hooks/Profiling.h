#pragma once

// [Main.Develop.Main] DebugMode profiling (core/PerfProfiler.h).
void AttachProfilingHooks();         // from AttachHooks, after the main transaction
void EnsureProfilingDeviceHooks();   // once a frame from RenderHook: counts draw calls once the device exists
