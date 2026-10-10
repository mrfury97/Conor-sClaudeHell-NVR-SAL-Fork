#pragma once

class RenderPass;

// Bounce lighting from light probes (interiors). A grid of probes fitted to the interior when it is
// entered; each frame a few of them (nearest the camera first, then the oldest round it) render a
// small cubemap of the room lit by its lamps (Shaders/Shadows/ProbeCapture: the surfaces' colour
// times the lamps' light, through the lamps' shadow maps, plus the light the probes already hold,
// so light bounces more than once over time), which is boiled down to a colour and a direction
// (Shaders/Shadows/ProbeReduce) in two atlas textures. The object shaders take their ambient from
// the probes round them (Shaders/Includes/BounceLighting.hlsl): lamp-lit ceilings and walls light
// the room, colours bleed, corners and the insides of shelves see less of it.
//
// It has no shaders in the game's own sense, only its own capture and reduction shaders.
class BounceLightingShaders : public ShaderCollection
{
public:
	BounceLightingShaders() : ShaderCollection("BounceLighting") {};

	static const int GridX = 24, GridY = 24, GridZ = 6;   // probes per axis
	static const int ProbeCount = GridX * GridY * GridZ;
	static const int CaptureSize = 8;                      // texels on a side of a probe's cubemap faces

	struct BounceSettings {
		float	Strength;         // how far the probes replace the flat ambient (0-1)
		float	AmbientFloor;     // the flat ambient kept on top of the bounce, so nothing goes black
		float	Intensity;        // the bounce light's brightness
		float	Bounces;          // light the probes already hold, fed back in (0: one bounce only)
		float	Spacing;          // the probes' least spacing, units (larger interiors space them wider)
		float	Range;            // how far a probe sees, units
		int		ProbesPerFrame;   // probes captured a frame
		int		Debug;            // 1: the bounce light x 32, 2: the probes' validity, 3: the bounce against the flat ambient, 4: as 1, the probes capturing every surface white and unlit
	};
	BounceSettings	Settings;

	struct BounceStruct {
		D3DXVECTOR4		Grid;       // xyz minus the first probe (camera relative) over the spacing, w 1 / the atlas's width
		D3DXVECTOR4		GridScale;  // xyz 1 / the spacing on each axis, w 1 / the atlas's height
		D3DXVECTOR4		GridSize;   // xyz probes per axis, w the atlas's width in texels
		D3DXVECTOR4		Lighting;   // x strength (0: off), y ambient floor, z debug view (0, 1, 2), w intensity
		D3DXVECTOR4		Capture;    // x 1 linear lighting, y light scale, z bounces, w 0
		D3DXVECTOR4		Reduce;     // x 0: the colour atlas, 1: the direction atlas
		D3DXVECTOR4		CaptureDebug;   // x 1: surfaces captured white and unlit (Debug 4)
	};
	BounceStruct	Constants;

	// The lamps lighting this frame's probes (Shaders/Shadows/ProbeCapture): the nearest of every
	// lamp in the scene, whatever the camera sees -- the contact shadows' list leaves out lamps
	// behind the camera, and the probes there lost their light as you turned.
	static const int ProbeLampsMax = 24;
	D3DXVECTOR4		LampPosition[ProbeLampsMax];   // world, w radius (0 past the last)
	D3DXVECTOR4		LampColor[ProbeLampsMax];      // rgb colour x dimmer, w shadow info: slot * 2 + fade, -1 none
	D3DXVECTOR4		LampAnchor[ProbeLampsMax];     // its shadow slot's anchor (world), w the slot's radius

	// The probes' own pass (BounceLighting.cpp, ProbeCapturePass): static lit meshes, each with its
	// diffuse texture, both faces.
	RenderPass*				CapturePass = nullptr;

	struct ProbeGeometry {
		NiGeometry*	Geo;
		D3DXVECTOR3	Centre;     // world
		float		Radius;
	};

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	void	Capture();            // ShadowManager::RenderShadowMaps, with the lamps' shadow maps drawn

private:
	bool	EnsureResources();
	void	FitGrid(TESObjectCELL* Cell);
	int		CountLoaded(TESObjectCELL* Cell) const;   // the cell's references with their 3D loaded
	void	GatherGeometry(TESObjectCELL* Cell, const std::vector<int>& Probes);
	void	GatherLamps(const D3DXVECTOR3& Middle, float Reach);
	void	CaptureProbe(int Probe);
	void	RenderProbeCube(int Probe);   // its six faces, into CaptureCube
	void	BuildLampRecords();       // this frame's lamps, hashed, for the probes' signatures
	UInt32	ProbeSignatureOf(const D3DXVECTOR3& Position) const;   // the lamps able to reach what a probe sees, hashed
	D3DXVECTOR3	ProbePosition(int Probe) const;

	bool					Failed = false;
	bool					Ready = false;
	ShaderRecordVertex*		CaptureVertex = nullptr;
	ShaderRecordPixel*		CapturePixel = nullptr;
	ShaderRecordPixel*		ReducePixel = nullptr;
	IDirect3DTexture9*		AtlasA = nullptr;     // TESR_ProbeAtlasA: mean light x validity, validity
	IDirect3DSurface9*		AtlasASurface = nullptr;
	IDirect3DTexture9*		AtlasB = nullptr;     // TESR_ProbeAtlasB: direction x validity
	IDirect3DSurface9*		AtlasBSurface = nullptr;
	IDirect3DCubeTexture9*	CaptureCube = nullptr;
	IDirect3DSurface9*		CaptureFace[6] = {};
	IDirect3DSurface9*		CaptureDepth = nullptr;
	IDirect3DVertexBuffer9*	TexelQuad = nullptr;  // a quad over one texel's viewport
	int						MultipleTargets = -1;   // 1: the reduction writes both atlases in one pass (MRT), 0: one each, -1: not asked yet

	TESObjectCELL*			GridCell = nullptr;
	// Fitted on a cell's first frame its references may not have their 3D yet: a fit that found none
	// (the box round the camera) is tried again every 30 frames, and a second second after any fit the
	// loaded references are counted again, the grid refitted if many more have come in since.
	int						FitLoaded = 0;       // references with 3D when it was fitted
	UInt32					FitFrame = 0;
	bool					FitChecked = false;
	D3DXVECTOR3				GridOrigin = D3DXVECTOR3(0.0f, 0.0f, 0.0f);   // the first probe, world
	D3DXVECTOR3				GridSpacing = D3DXVECTOR3(128.0f, 128.0f, 128.0f);   // per axis
	float					FittedSpacing = 0.0f;   // the Spacing setting the grid was fitted with
	std::vector<UInt32>		CapturedFrame;    // per probe: the frame it was last captured in, 0 never
	// A probe settles: after SettleCaptures captures its light has bounced as far as it goes (each one
	// feeds back what the probes held), and with the same lamps round it another capture draws the
	// same picture. It is captured again only when its lamps change (its signature: their place,
	// reach, colour and shadow), or after RefreshFrames as a fallback (doors, moved objects).
	static const int		SettleCaptures = 3;
	static const UInt32		RefreshFrames = 1800;
	std::vector<UInt32>		ProbeSignature;   // per probe: its lamps when last captured
	std::vector<UInt8>		ProbeCaptures;    // per probe: captures since its lamps last changed
	struct LampRecord {
		D3DXVECTOR3	Position;
		float		Reach;    // a probe within Range + this sees what it lights
		UInt32		Hash;
	};
	std::vector<LampRecord>	FrameLamps;
	UInt32					FrameSalt = 0;     // the settings that change what a capture draws, hashed
	int						SignatureCursor = 0;   // settled probes checked a frame: a window of the grid going round
	UInt32					FrameCounter = 0;
	UInt32					CapturedAny = 0;
	std::vector<ProbeGeometry>	FrameGeometry;
	std::vector<std::pair<float, int>>	Candidates;
	std::vector<std::pair<float, ShadowSceneLight*>>	LampCandidates;
	std::vector<NiAVObject*>	WalkScratch;
	UInt32					LogProbes = 0, LogMeshes = 0, LogDraws = 0, LogLamps = 0, LogFrames = 0;   // Develop.DebugMode
};
