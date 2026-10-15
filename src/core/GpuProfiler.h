#pragma once

// GPU time per section of the frame, from D3D9 timestamp queries (DXVK supports them), averaged and
// logged every 300 frames while [Main.Develop.Main] DebugMode is on. CPU timers can't see GPU cost:
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

	// Once a frame: collects what's ready and logs the averages every 300 frames.
	static void EndFrame() {
		if (!Active()) return;
		for (int s = 0; s < Count; s++)
			for (int i = 0; i < RingSize; i++)
				if (Timers[s].Sets[i].Pending) Collect(Timers[s], Timers[s].Sets[i]);
		FrameTimeSum += TheFrameRateManager->ElapsedTime * 1000.0;
		if (++Frames < 300) return;

		auto Average = [](int s) { return Timers[s].Count ? Timers[s].Sum / Timers[s].Count : 0.0; };
		Logger::Log("[GpuProfile] %s, %u frames: frame %.2f ms; GPU: shadow maps %.2f ms, world scene %.2f ms, lamp contact shadows %.2f ms, post-process %.2f ms",
			TheShaderManager->GameState.isExterior ? "exterior" : "interior", Frames, FrameTimeSum / Frames,
			Average(ShadowMaps), Average(WorldScene), Average(LampContact), Average(Effects));
		for (int s = 0; s < Count; s++) { Timers[s].Sum = 0.0; Timers[s].Count = 0; }
		Frames = 0;
		FrameTimeSum = 0.0;
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
	static inline UInt32 Frames = 0;
	static inline double FrameTimeSum = 0.0;
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
