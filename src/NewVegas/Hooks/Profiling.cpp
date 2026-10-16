// [Main.Develop.Main] DebugMode: the engine-side hooks behind PerfProfiler (core/PerfProfiler.h). Attached
// only when DebugMode is on at startup, each in its own Detours transaction so one that fails (another
// plugin already patched the function) is logged and leaves the rest alone. Addresses and calling
// conventions from the FalloutNV database.
#include "Profiling.h"

#include <algorithm>

typedef void(__cdecl* CameraNodeCullerFn)(void*, void*, void*);
typedef void(__cdecl* CameraAccumulatorFn)(void*, void*);
typedef void(__cdecl* NoArgsFn)();
typedef void(__thiscall* ThisFn)(void*);
typedef void(__thiscall* ThisArgFn)(void*, void*);

static CameraNodeCullerFn AccumulateScene = (CameraNodeCullerFn)0x00B6BEE0;         // BSShaderUtil::AccumulateScene
static CameraAccumulatorFn RenderScenePre = (CameraAccumulatorFn)0x00B6BA20;        // BSShaderUtil::RenderScenePreResolveDepth
static CameraAccumulatorFn RenderScenePost = (CameraAccumulatorFn)0x00B6B930;       // BSShaderUtil::RenderScenePostResolveDepth
static ThisArgFn PreOnVisible = (ThisArgFn)0x00B5E870;                              // ShadowSceneNode::PreOnVisible
static NoArgsFn ProcessAllQueuedLights = (NoArgsFn)0x00B60040;                      // ShadowSceneNode::ProcessAllQueuedLights
static ThisFn RunWorldScripts = (ThisFn)0x00455490;                                 // TES::RunScripts: quests, then the loaded cells' objects
static ThisFn RunActorScripts = (ThisFn)0x00978550;                                 // ProcessLists::RunActorScripts
static NoArgsFn RunDelayedScriptActions = (NoArgsFn)0x005AA720;                     // Script::RunDelayedScriptActionsOnReferences
static ThisFn WaitForAIThreads = (ThisFn)0x008C7990;                                // AILinearTaskThreadManager::WaitForThreads

static void __cdecl AccumulateSceneHook(void* Camera, void* Node, void* Culler) {
	PerfProfiler::Scope Scope(PerfProfiler::Culling, PerfProfiler::PartCount, PerfProfiler::InScene());
	AccumulateScene(Camera, Node, Culler);
}

static void __cdecl RenderScenePreHook(void* Camera, void* Accumulator) {
	const bool Scene = PerfProfiler::InScene();
	PerfProfiler::Scope Scope(PerfProfiler::Drawing, PerfProfiler::PartCount, Scene);
	if (Scene) GpuProfiler::Begin(GpuProfiler::ScenePre);
	RenderScenePre(Camera, Accumulator);
	if (Scene) GpuProfiler::End(GpuProfiler::ScenePre);
}

static void __cdecl RenderScenePostHook(void* Camera, void* Accumulator) {
	const bool Scene = PerfProfiler::InScene();
	PerfProfiler::Scope Scope(PerfProfiler::Drawing, PerfProfiler::PartCount, Scene);
	if (Scene) GpuProfiler::Begin(GpuProfiler::ScenePost);
	RenderScenePost(Camera, Accumulator);
	if (Scene) GpuProfiler::End(GpuProfiler::ScenePost);
}

static void __fastcall PreOnVisibleHook(void* This, void* Edx, void* Culler) {
	PerfProfiler::Scope Scope(PerfProfiler::LightAssign);
	PreOnVisible(This, Culler);
}

static void __cdecl ProcessAllQueuedLightsHook() {
	PerfProfiler::Scope Scope(PerfProfiler::LightQueue);
	ProcessAllQueuedLights();
}

static void __fastcall RunWorldScriptsHook(void* This, void* Edx) {
	PerfProfiler::Scope Scope(PerfProfiler::Scripts);
	RunWorldScripts(This);
}

static void __fastcall RunActorScriptsHook(void* This, void* Edx) {
	PerfProfiler::Scope Scope(PerfProfiler::Scripts);
	RunActorScripts(This);
}

static void __cdecl RunDelayedScriptActionsHook() {
	PerfProfiler::Scope Scope(PerfProfiler::Scripts);
	RunDelayedScriptActions();
}

static void __fastcall WaitForAIThreadsHook(void* This, void* Edx) {
	PerfProfiler::Scope Scope(PerfProfiler::AIWait);
	WaitForAIThreads(This);
}

template <typename Function>
static void AttachOne(Function& Target, void* Hook, const char* Name) {
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	LONG Result = DetourAttach(&(PVOID&)Target, Hook);
	if (Result == NO_ERROR) Result = DetourTransactionCommit();
	else DetourTransactionAbort();
	if (Result != NO_ERROR) Logger::Log("[Perf] could not hook %s (error %ld): it will read 0", Name, Result);
}

void AttachProfilingHooks() {
	if (!TheSettingManager->SettingsMain.Develop.DebugMode) return;
	AttachOne(AccumulateScene, AccumulateSceneHook, "BSShaderUtil::AccumulateScene");
	AttachOne(RenderScenePre, RenderScenePreHook, "BSShaderUtil::RenderScenePreResolveDepth");
	AttachOne(RenderScenePost, RenderScenePostHook, "BSShaderUtil::RenderScenePostResolveDepth");
	AttachOne(PreOnVisible, PreOnVisibleHook, "ShadowSceneNode::PreOnVisible");
	AttachOne(ProcessAllQueuedLights, ProcessAllQueuedLightsHook, "ShadowSceneNode::ProcessAllQueuedLights");
	AttachOne(RunWorldScripts, RunWorldScriptsHook, "TES::RunScripts");
	AttachOne(RunActorScripts, RunActorScriptsHook, "ProcessLists::RunActorScripts");
	AttachOne(RunDelayedScriptActions, RunDelayedScriptActionsHook, "Script::RunDelayedScriptActionsOnReferences");
	AttachOne(WaitForAIThreads, WaitForAIThreadsHook, "AILinearTaskThreadManager::WaitForThreads");
	Logger::Log("[Perf] profiling hooks attached (DebugMode): a [Perf] block is logged every 300 frames");
}

// The device's draw calls, counted: IDirect3DDevice9 vtable slots 81 (DrawPrimitive) and 82
// (DrawIndexedPrimitive). Patched once, the first DebugMode frame with a device.
typedef HRESULT(__stdcall* DrawPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT(__stdcall* DrawIndexedPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
static DrawPrimitiveFn OriginalDrawPrimitive = nullptr;
static DrawIndexedPrimitiveFn OriginalDrawIndexedPrimitive = nullptr;

static HRESULT __stdcall DrawPrimitiveHook(IDirect3DDevice9* Device, D3DPRIMITIVETYPE Type, UINT Start, UINT Count) {
	PerfProfiler::CountDraw(Count);
	return OriginalDrawPrimitive(Device, Type, Start, Count);
}

static HRESULT __stdcall DrawIndexedPrimitiveHook(IDirect3DDevice9* Device, D3DPRIMITIVETYPE Type, INT Base, UINT MinIndex, UINT Vertices, UINT Start, UINT Count) {
	PerfProfiler::CountDraw(Count);
	return OriginalDrawIndexedPrimitive(Device, Type, Base, MinIndex, Vertices, Start, Count);
}

void EnsureProfilingDeviceHooks() {
	static bool Done = false;
	if (Done || !TheSettingManager->SettingsMain.Develop.DebugMode || !TheRenderManager || !TheRenderManager->device) return;
	Done = true;
	void** Table = *(void***)TheRenderManager->device;
	DWORD Old;
	if (!VirtualProtect(&Table[81], 2 * sizeof(void*), PAGE_READWRITE, &Old)) {
		Logger::Log("[Perf] could not count draw calls (vtable not writable)");
		return;
	}
	OriginalDrawPrimitive = (DrawPrimitiveFn)Table[81];
	OriginalDrawIndexedPrimitive = (DrawIndexedPrimitiveFn)Table[82];
	Table[81] = (void*)DrawPrimitiveHook;
	Table[82] = (void*)DrawIndexedPrimitiveHook;
	VirtualProtect(&Table[81], 2 * sizeof(void*), Old, &Old);
}

// A pass type's name (BSShaderManager::GetPassName); its default case formats into a stack buffer, so
// only a string in the game's read-only data is trusted.
static void PassName(UInt32 Type, char* Out, size_t Size) {
	const char* Name = Pointers::Functions::GetPassDescription(Type);
	if ((UInt32)Name >= 0x00FDF4A4 && (UInt32)Name < 0x01183000) strncpy_s(Out, Size, Name, _TRUNCATE);
	else sprintf_s(Out, Size, "#%u", Type);
}

void PerfProfiler::EndFrame() {
	Enabled = TheSettingManager->SettingsMain.Develop.DebugMode && TheRenderManager && TheRenderManager->device;
	if (!MainThread) MainThread = GetCurrentThreadId();
	if (!Enabled) { ObjectPasses.clear(); return; }

	UInt32 Multi = 0, Heavy = 0;
	for (const auto& Entry : ObjectPasses) {
		if (Entry.second >= 2) Multi++;
		if (Entry.second >= 4) Heavy++;
	}
	Objects += ObjectPasses.size();
	ObjectsMulti += Multi;
	ObjectsHeavy += Heavy;
	ObjectPasses.clear();
	FrameTimeSum += TheFrameRateManager->ElapsedTime * 1000.0;
	if (++Frames < 300) return;

	LARGE_INTEGER Frequency;
	QueryPerformanceFrequency(&Frequency);
	auto Ms = [&](Timer T) { return (double)Ticks[T] * 1000.0 / (double)Frequency.QuadPart / Frames; };
	auto PerFrame = [&](UInt64 Value) { return (double)Value / Frames; };
	auto Gpu = [](GpuProfiler::Section S) { return GpuProfiler::TakeAverage(S); };

	const double Frame = FrameTimeSum / Frames;
	Logger::Log("[Perf] %s, %u frames: frame %.2f ms (%.0f fps); DrawWorld on the CPU %.2f ms, the rest of the frame (game update, AI, present) %.2f ms",
		TheShaderManager->GameState.isExterior ? "exterior" : "interior", Frames, Frame, Frame > 0.0 ? 1000.0 / Frame : 0.0, Ms(DrawWorld), Frame - Ms(DrawWorld));
	Logger::Log("[Perf]   CPU ms: shadow maps %.2f, water reflection %.2f, world scene %.2f (culling %.2f, submitting draws %.2f), first person %.2f, post-process %.2f; "
		"lights: matching %.2f, queued updates %.2f; scripts %.2f; waiting for AI threads %.2f",
		Ms(TShadowMaps), Ms(TWater), Ms(TScene), Ms(Culling), Ms(Drawing), Ms(TFirstPerson), Ms(TEffects), Ms(LightAssign), Ms(LightQueue), Ms(Scripts), Ms(AIWait));
	Logger::Log("[Perf]   GPU ms: shadow maps %.2f, water reflection %.2f, world scene %.2f (opaque %.2f, after the depth resolve %.2f), lamp contact shadows %.2f, post-process %.2f",
		Gpu(GpuProfiler::ShadowMaps), Gpu(GpuProfiler::Water), Gpu(GpuProfiler::WorldScene), Gpu(GpuProfiler::ScenePre), Gpu(GpuProfiler::ScenePost),
		Gpu(GpuProfiler::LampContact), Gpu(GpuProfiler::Effects));
	Logger::Log("[Perf]   draw calls a frame: shadow maps %.0f, water %.0f, world scene %.0f (%.0fk triangles), first person %.0f, post-process %.0f, other %.0f",
		PerFrame(Draws[ShadowMaps]), PerFrame(Draws[Water]), PerFrame(Draws[Scene]), PerFrame(Primitives_[Scene]) / 1000.0,
		PerFrame(Draws[FirstPerson]), PerFrame(Draws[Effects]), PerFrame(Draws[Other]));

	// The scene's pass types by draws, the eight largest.
	std::vector<std::pair<UInt64, UInt32>> Types;
	for (UInt32 t = 0; t < PassTypeCount; t++)
		if (PassTypes[t]) Types.push_back({ PassTypes[t], t });
	std::sort(Types.begin(), Types.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
	std::string Top;
	for (size_t i = 0; i < Types.size() && i < 8; i++) {
		char Name[64], Item[96];
		PassName(Types[i].second, Name, sizeof(Name));
		sprintf_s(Item, "%s%s %.0f", i ? ", " : "", Name, PerFrame(Types[i].first));
		Top += Item;
	}
	const double ObjectsPerFrame = PerFrame(Objects);
	Logger::Log("[Perf]   world scene: %.0f objects a frame, %.2f draws each; drawn 2+ times %.0f, 4+ times %.0f; pass types: %s",
		ObjectsPerFrame, ObjectsPerFrame > 0.0 ? PerFrame(Draws[Scene]) / ObjectsPerFrame : 0.0, PerFrame(ObjectsMulti), PerFrame(ObjectsHeavy), Top.c_str());

	memset(Ticks, 0, sizeof(Ticks));
	memset(Draws, 0, sizeof(Draws));
	memset(Primitives_, 0, sizeof(Primitives_));
	memset(PassTypes, 0, sizeof(PassTypes));
	Objects = ObjectsMulti = ObjectsHeavy = 0;
	Frames = 0;
	FrameTimeSum = 0.0;
}
