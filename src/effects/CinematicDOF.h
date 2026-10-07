#pragma once

// Cinematic depth of field: thin-lens circle of confusion, near/far-separated gathered bokeh, and
// autofocus on world depth. See CinematicDOF.fx.hlsl for the technique. Independent of the older
// DepthOfField effect, so the two can be compared; running both at once blurs twice.
class CinematicDOFEffect : public EffectRecord
{
public:
	CinematicDOFEffect() : EffectRecord("CinematicDOF") {};

	struct CinematicDOFSettingsStruct {
		int		Mode;				// 0 always, 1 dialogue only
		float	FocalLength;		// mm, virtual -- independent of the game's FOV
		float	FStop;
		bool	AutoFocus;
		float	FocusDistance;		// units, when AutoFocus is off
		float	FocusSpeed;			// per second
		float	MinFocusDistance;	// units
		float	MaxBlur;			// percent of screen height
		float	HighlightBoost;
		float	WeaponBlur;
		float	NearFocusRange;		// units kept sharp in front of the focus point
		float	NearBlurStrength;	// scale on foreground blur only
		float	TransitionTime;		// seconds to fade in or out
		int		ApertureBlades;		// below 3 is a round aperture
		float	BladeRotation;		// degrees
		float	BladeCurvature;		// 0 straight-sided polygon, 1 round
		float	Anamorphic;			// bokeh height / width
		float	CatsEye;			// 0-1, discs clipped toward the frame edges
		float	RingBrightness;		// -1 to 1, light toward the disc's centre or rim
		float	HighlightThreshold;	// brightness above which HighlightBoost applies
		int		BokehShape;			// 0 aperture, 1 star, 2 donut, 3 heart, 4 cross
		float	ShapeDetail;		// 0-1: star point depth, donut hole size, cross arm width
		int		BokehQuality;		// 0: 48 samples, 1: 96, 2: 160
		float	PostfilterRadius;	// half-res texels between the smoothing filter's taps; 1 is the original filter
		int		WeaponDOF;			// 0 off, 1 hip-fire only, 2 always
		float	WeaponFocusDistance;	// units; the weapon is sharp beyond this
		float	WeaponBlurRange;	// units over which the weapon's blur builds up nearer than that
		float	WeaponMaxBlur;		// percent of screen height
		int		DebugView;
	};
	CinematicDOFSettingsStruct	Settings;

	struct CinematicDOFStruct {
		D3DXVECTOR4	Lens;	// x: lens coefficient, y: max CoC (screen heights), z: highlight boost, w: weapon blur
		D3DXVECTOR4	Focus;	// x: manual focus (units), y: autofocus (0/1), z: focus easing this frame, w: previous focus valid (0/1)
		D3DXVECTOR4	Data;	// x: strength 0-1, y: min focus (units), z: focal length (mm), w: debug view
		D3DXVECTOR4	Near;	// x: near focus range (units), y: near blur strength
		D3DXVECTOR4	Aperture;	// x: blades, y: blade rotation (radians), z: blade curvature, w: anamorphic
		D3DXVECTOR4	Bokeh;	// x: cat's eye, y: ring brightness, z: highlight threshold
		D3DXVECTOR4	Shape;	// x: bokeh shape, y: shape detail, z: postfilter radius
		D3DXVECTOR4	Weapon;	// x: weapon focus distance, y: weapon blur range, z: weapon max CoC, w: weapon DoF strength
	};
	CinematicDOFStruct	Constants;

	struct CinematicDOFTexturesStruct {
		IDirect3DTexture9*	HalfATexture = nullptr;
		IDirect3DSurface9*	HalfASurface = nullptr;
		IDirect3DTexture9*	HalfBTexture = nullptr;
		IDirect3DSurface9*	HalfBSurface = nullptr;
		IDirect3DTexture9*	FocusTexture[2] = { nullptr, nullptr };
		IDirect3DSurface9*	FocusSurface[2] = { nullptr, nullptr };
	};
	CinematicDOFTexturesStruct	Textures;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();

	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer);

private:
	float	blend = 0.0f;			// current strength, eased toward 1 while active and 0 while not
	float	weaponBlend = 0.0f;		// the same for weapon depth of field, which has its own conditions
	bool	focusValid = false;		// the focus texture being read holds a real previous value
	int		focusRead = 0;			// which FocusTexture holds last frame's focus

	// Technique handles, looked up once per loaded Effect rather than by name on every draw.
	enum Technique { TechniqueFocus, TechniquePrefilter, TechniqueBokeh48, TechniqueBokeh96, TechniqueBokeh160, TechniquePostfilter, TechniqueCombine, TechniqueCount };
	D3DXHANDLE	techniques[TechniqueCount] = {};
	UInt32		techniquesGeneration = 0;	// the Effect LoadGeneration the handles belong to; 0 = none yet
	bool	DrawTechnique(Technique technique);
};
