#pragma once

#include "ShaderTemplate.h"

enum ShaderCompileType {
	AlwaysOff,
	AlwaysOn,
	RecompileChanged,
	RecompileInMenu,
	RecompileAbsent,
};


class ShaderValue {
public:
	ShaderValue() {};
	virtual ~ShaderValue() {};

	const char*			Name;
	UInt32				RegisterIndex;
	UInt32				RegisterCount;
};


class ShaderFloatValue : public ShaderValue {
public:
	ShaderFloatValue() {
		Value = nullptr;
	};
	virtual ~ShaderFloatValue() {};

	void				GetValueFromConstantTable();

	D3DXVECTOR4* Value;
	D3DXPARAMETER_TYPE	Type;
};


class ShaderTextureValue : public ShaderValue {
public:
	ShaderTextureValue() {
		Texture = nullptr;
		TexturePath = "";
	};
	virtual ~ShaderTextureValue() {};

	void				GetSamplerStateString(ID3DXBuffer* ShaderSource, UINT32 Index);
	void				GetTextureRecord();

	std::string			SamplerString;
	std::string			TexturePath;
	TextureRecord*		Texture;
	TextureRecord::TextureRecordType	Type;
};


class ShaderProgram {
public:
	ShaderProgram();
	virtual ~ShaderProgram();

	virtual void			SetCT() = 0;
	virtual void			CreateCT(ID3DXBuffer* ShaderSource, ID3DXConstantTable* ConstantTable) = 0;

	static void				ReportError(HRESULT result);
	static bool				FileExists(const char* path);
	static bool				CheckPreprocessResult(const char* CachedPreprocessPath, ID3DXBuffer* ShaderSource);

	// Drops the cached IDirect3DTexture9* for a named sampler so that SetCT re-resolves it
	// on the next draw. MUST be called on every consumer of a texture that is released and
	// recreated -- see ShaderManager::ClearShaderSamplers. Lives on ShaderProgram rather
	// than EffectRecord because game shaders need it too.
	void					ClearSampler(const char* TextureName, size_t Length);

	ShaderFloatValue*		FloatShaderValues;
	UInt32					FloatShaderValuesCount;
	ShaderTextureValue*		TextureShaderValues;
	UInt32					TextureShaderValuesCount;
};

class ShaderRecord : public ShaderProgram {
public:
	ShaderRecord();
	virtual ~ShaderRecord();

	virtual void			SetCT();
	virtual void			CreateCT(ID3DXBuffer* ShaderSource, ID3DXConstantTable* ConstantTable);
	virtual void			SetShaderConstantF(UInt32 RegisterIndex, D3DXVECTOR4* Value, UInt32 RegisterCount) = 0;

	static ShaderRecord*	LoadShader(const char* Name, const char* SubPath, ShaderTemplate Template = ShaderTemplate{});

	// Whether TESR_DepthBufferBeforeWater has been resolved for this world render (see SetCT).
	// Cleared around each world render by RenderWorldSceneGraphHook.
	static bool				WorldDepthResolved;

	const char* Name;
	bool					HasRenderedBuffer;
	bool					HasDepthBuffer;
	bool					ClearSamplers;
};

class ShaderRecordVertex : public ShaderRecord {
public:
	ShaderRecordVertex(const char* shaderName);
	virtual ~ShaderRecordVertex();

	virtual void			SetShaderConstantF(UInt32 RegisterIndex, D3DXVECTOR4* Value, UInt32 RegisterCount);

	IDirect3DVertexShader9* ShaderHandle;
};

class ShaderRecordPixel : public ShaderRecord {
public:
	ShaderRecordPixel(const char* shaderName);
	virtual ~ShaderRecordPixel();

	virtual void			SetShaderConstantF(UInt32 RegisterIndex, D3DXVECTOR4* Value, UInt32 RegisterCount);

	IDirect3DPixelShader9* ShaderHandle;
};

enum ShaderRecordType {
	Default,
	Exterior,
	Interior,
};

class NiD3DVertexShaderEx : public NiD3DVertexShader {
public:
	void					SetupShader(IDirect3DVertexShader9* CurrentVertexHandle);
	void					DisposeShader();

	ShaderRecordVertex*		GetShaderRecord(ShaderRecordType Type);

	static void __fastcall Free(NiD3DVertexShaderEx* shader);
};

class NiD3DPixelShaderEx : public NiD3DPixelShader {
public:
	void					SetupShader(IDirect3DPixelShader9* CurrentPixelHandle);
	void					DisposeShader();

	ShaderRecordPixel*		GetShaderRecord(ShaderRecordType Type);

	static void __fastcall Free(NiD3DPixelShaderEx* shader);
};