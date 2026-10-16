#pragma once

#include <unordered_map>

// [Main.Develop.Main] DebugMode: where the game's frame goes, logged every 300 frames together with
// GpuProfiler's GPU times. CPU timers (QueryPerformanceCounter) around the engine's functions
// (NewVegas/Hooks/Profiling.cpp) and NVR's own passes; draw calls per part of the frame, counted at the
// device; and in the world scene, how many passes each object is drawn in and of which kinds, read
// from BSShaderManager::pCurrentRenderPass at each draw. Main thread only: work the game's AI threads
// do (while the frame renders) shows only as the time the main thread waits for them.
class PerfProfiler {
public:
	enum Part { Other, ShadowMaps, Water, Scene, FirstPerson, Effects, PartCount };   // what a draw call belongs to
	enum Timer {
		DrawWorld,      // TESMain::DrawWorld: everything drawn, NVR's passes included
		TShadowMaps,    // NVR's sun cascades and lamp cubemaps
		TWater,         // the water reflection (the scene drawn again)
		TScene,         // TESMain::DrawWorld_DrawScene
		Culling,        // ...its BSShaderUtil::AccumulateScene: culling and gathering what to draw
		Drawing,        // ...its BSShaderUtil::RenderScenePre/PostResolveDepth: submitting the draws
		TFirstPerson,
		LightAssign,    // ShadowSceneNode::PreOnVisible: lights matched to what is visible
		LightQueue,     // ShadowSceneNode::ProcessAllQueuedLights
		Scripts,        // quest, cell and actor scripts, and delayed script actions
		AIWait,         // AILinearTaskThreadManager::WaitForThreads: the main thread idle until AI is done
		TEffects,       // NVR's post-process chain, before and after the tonemap
		TimerCount
	};

	// A CPU timer (the outermost of nested ones of the same kind counts), and optionally the part of the
	// frame draw calls are counted against while it runs.
	class Scope {
	public:
		Scope(Timer aTimer, Part aPart = PartCount, bool Condition = true) : T(aTimer) {
			On = Condition && PerfProfiler::Active();
			if (!On) return;
			if (Depth[T]++ == 0) QueryPerformanceCounter(&Start);
			if (aPart != PartCount) { Previous = CurrentPart; CurrentPart = aPart; Switched = true; }
		}
		~Scope() {
			if (!On) return;
			if (--Depth[T] == 0) {
				LARGE_INTEGER Now;
				QueryPerformanceCounter(&Now);
				Ticks[T] += Now.QuadPart - Start.QuadPart;
			}
			if (Switched) CurrentPart = Previous;
		}
	private:
		Timer T;
		bool On = false, Switched = false;
		Part Previous = Other;
		LARGE_INTEGER Start = {};
	};

	static bool Active() { return Enabled && GetCurrentThreadId() == MainThread; }
	static bool InScene() { return CurrentPart == Scene; }

	// From the device's DrawPrimitive / DrawIndexedPrimitive.
	static void CountDraw(UINT Primitives) {
		if (!Active()) return;
		Draws[CurrentPart]++;
		Primitives_[CurrentPart] += Primitives;
		if (CurrentPart != Scene) return;
		void* Geometry = nullptr;
		UInt16 PassType = 0;
		if (!ReadCurrentPass(&Geometry, &PassType)) return;
		PassTypes[PassType & (PassTypeCount - 1)]++;
		if (Geometry) ObjectPasses[Geometry]++;
	}

	// Once a frame, after GpuProfiler::EndFrame: per-frame tallies, and the log every 300 frames.
	static void EndFrame();

private:
	static constexpr UInt32 PassTypeCount = 1024;

	// BSShaderManager::pCurrentRenderPass (0x11F91E0): BSShaderProperty::RenderPass, pGeometry at 0,
	// usPassEnum at 4. Read defensively: a draw NVR makes itself may find it stale.
	static bool ReadCurrentPass(void** Geometry, UInt16* PassType) {
		__try {
			const BYTE* Pass = *(const BYTE**)0x011F91E0;
			if (!Pass) return false;
			*Geometry = *(void* const*)Pass;
			*PassType = *(const UInt16*)(Pass + 4);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static inline bool Enabled = false;
	static inline DWORD MainThread = 0;
	static inline Part CurrentPart = Other;
	static inline int Depth[TimerCount] = {};
	static inline LONGLONG Ticks[TimerCount] = {};
	static inline UInt64 Draws[PartCount] = {};
	static inline UInt64 Primitives_[PartCount] = {};
	static inline UInt64 PassTypes[PassTypeCount] = {};
	static inline std::unordered_map<void*, UInt32> ObjectPasses;   // this frame's scene objects and their draws
	static inline UInt64 Objects = 0, ObjectsMulti = 0, ObjectsHeavy = 0;
	static inline UInt32 Frames = 0;
	static inline double FrameTimeSum = 0.0;
};
