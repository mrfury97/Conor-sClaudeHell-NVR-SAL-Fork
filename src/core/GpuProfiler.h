#pragma once

// GPU time per section of the frame, from D3D9 timestamp queries (DXVK supports them), averaged and
// logged every 300 frames by PerfProfiler while [Main.Develop.Main] DebugMode is on. CPU timers can't see GPU cost:
// D3D9 queues the work and returns.
//
// Each section keeps a ring of query sets and reads a result back only once it is ready (never
// waiting on the GPU), so measuring costs nothing measurable. A set whose disjoint query says the
// GPU clock changed mid-measurement is discarded. Query creation failing (no timestamp support)
// switches the profiler off.
class GpuProfiler {
public:
	enum Section {
		ShadowMaps,     // ShadowManager::RenderShadowMaps: sun cascades outdoors, lamp cubemaps indoors
		WorldScene,     // the world drawn, every object shader's lighting and forward shadows included
		Water,          // the water reflection: the scene drawn again into its own target
		ScenePre,       // the world scene's opaque draws (BSShaderUtil::RenderScenePreResolveDepth)
		ScenePost,      // its draws after the depth resolve (BSShaderUtil::RenderScenePostResolveDepth)
		LampContact,    // the lamps' screen-space contact shadows (interiors, ForwardPointShadows)
		Effects,        // the whole post-process chain
		Count
	};

	static void Begin(Section S) {
		if (!Active()) return;
		Timer& T = Timers[S];
		Set& Q = T.Sets[T.Next];
		if (Q.Pending) Collect(T, Q);
		if (Q.Pending || !Create(Q)) { T.Skip = true; return; }   // the GPU hasn't finished it: skip this frame's measurement
		T.Skip = false;
		Q.Disjoint->Issue(D3DISSUE_BEGIN);
		Q.Start->Issue(D3DISSUE_END);
	}

	static void End(Section S) {
		if (!Active()) return;
		Timer& T = Timers[S];
		if (T.Skip) return;
		Set& Q = T.Sets[T.Next];
		Q.Stop->Issue(D3DISSUE_END);
		Q.Frequency->Issue(D3DISSUE_END);
		Q.Disjoint->Issue(D3DISSUE_END);
		Q.Pending = true;
		T.Next = (T.Next + 1) % RingSize;
	}

	// Once a frame: collects what's ready (PerfProfiler::EndFrame logs it).
	static void EndFrame() {
		if (!Active()) return;
		for (int s = 0; s < Count; s++)
			for (int i = 0; i < RingSize; i++)
				if (Timers[s].Sets[i].Pending) Collect(Timers[s], Timers[s].Sets[i]);
	}

	// A section's average since the last call, in ms, and the average restarted.
	static double TakeAverage(Section S) {
		Timer& T = Timers[S];
		const double Average = T.Count ? T.Sum / T.Count : 0.0;
		T.Sum = 0.0;
		T.Count = 0;
		return Average;
	}

private:
	static constexpr int RingSize = 6;

	struct Set {
		IDirect3DQuery9* Disjoint = nullptr;
		IDirect3DQuery9* Frequency = nullptr;
		IDirect3DQuery9* Start = nullptr;
		IDirect3DQuery9* Stop = nullptr;
		bool Pending = false;
	};
	struct Timer {
		Set Sets[RingSize];
		int Next = 0;
		bool Skip = false;
		double Sum = 0.0;
		UInt32 Count = 0;
	};

	static inline Timer Timers[Count];
	static inline bool Failed = false;

	static bool Active() {
		return !Failed && TheSettingManager->SettingsMain.Develop.DebugMode && TheRenderManager && TheRenderManager->device;
	}

	static bool Create(Set& Q) {
		if (Q.Disjoint) return true;
		IDirect3DDevice9* Device = TheRenderManager->device;
		if (FAILED(Device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &Q.Disjoint)) || FAILED(Device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &Q.Frequency)) ||
			FAILED(Device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &Q.Start)) || FAILED(Device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &Q.Stop))) {
			Logger::Log("[GpuProfile] timestamp queries are not supported here: GPU profiling is off");
			Failed = true;
			return false;
		}
		return true;
	}

	static void Collect(Timer& T, Set& Q) {
		BOOL Disjoint = TRUE;
		UINT64 Frequency = 0, Start = 0, Stop = 0;
		if (Q.Disjoint->GetData(&Disjoint, sizeof(Disjoint), 0) != S_OK) return;   // not ready yet
		if (Q.Frequency->GetData(&Frequency, sizeof(Frequency), 0) != S_OK) return;
		if (Q.Start->GetData(&Start, sizeof(Start), 0) != S_OK) return;
		if (Q.Stop->GetData(&Stop, sizeof(Stop), 0) != S_OK) return;
		Q.Pending = false;
		if (Disjoint || !Frequency || Stop < Start) return;
		T.Sum += (double)(Stop - Start) * 1000.0 / (double)Frequency;
		T.Count++;
	}
};
