#pragma once

NiD3DVertexShader* (__thiscall* CreateVertexShader)(BSShader*, char*, char*, char*, char*) = (NiD3DVertexShader* (__thiscall*)(BSShader*, char*, char*, char*, char*))Hooks::CreateVertexShader;
NiD3DVertexShader* __fastcall CreateVertexShaderHook(BSShader* This, UInt32 edx, char* FileName, char* Arg2, char* ShaderType, char* ShaderName) {

	NiD3DVertexShaderEx* VertexShader = (NiD3DVertexShaderEx*)(*CreateVertexShader)(This, FileName, Arg2, ShaderType, ShaderName);
	if (!VertexShader) return nullptr;

	VertexShader->ShaderProg[ShaderRecordType::Default] = NULL;
	VertexShader->ShaderProg[ShaderRecordType::Exterior] = NULL;
	VertexShader->ShaderProg[ShaderRecordType::Interior] = NULL;
	VertexShader->ShaderHandleBackup = VertexShader->ShaderHandle;
	VertexShader->SetName(ShaderName);
	if (!strcmp(ShaderName, "ISNOISESCROLLANDBLEND.vso")) {
		VertexShader->SetName("WATERHEIGHTMAP0.vso");
		TheShaderManager->WaterVertexShaders[0] = VertexShader;
	}
	else if (!strcmp(ShaderName, "ISNOISENORMALMAP.vso")) {
		VertexShader->SetName("WATERHEIGHTMAP1.vso");
		TheShaderManager->WaterVertexShaders[1] = VertexShader;
	}
	TheShaderManager->LoadShader(VertexShader);
	// A grass vertex shader without a replacement pairs vanilla outputs with the replacement grass
	// pixel shaders: they detect it (no grass data) and fall back to plain lighting, but say so once.
	if (!strncmp(ShaderName, "GRASS", 5) && !VertexShader->ShaderProg[ShaderRecordType::Default] && !VertexShader->ShaderProg[ShaderRecordType::Exterior] && !VertexShader->ShaderProg[ShaderRecordType::Interior])
		Logger::Log("Grass vertex shader %s has no NVR replacement: grass it draws gets no grass lighting (magenta in the grass debug views)", ShaderName);
	return (NiD3DVertexShader*)VertexShader;

}

NiD3DPixelShader* (__thiscall* CreatePixelShader)(BSShader*, char*, char*, char*, char*) = (NiD3DPixelShader* (__thiscall*)(BSShader*, char*, char*, char*, char*))Hooks::CreatePixelShader;
NiD3DPixelShader* __fastcall CreatePixelShaderHook(BSShader* This, UInt32 edx, char* FileName, char* Arg2, char* ShaderType, char* ShaderName) {

	NiD3DPixelShaderEx* PixelShader = (NiD3DPixelShaderEx*)(*CreatePixelShader)(This, FileName, Arg2, ShaderType, ShaderName);
	if (!PixelShader) return nullptr;

	PixelShader->ShaderProg[ShaderRecordType::Default]	= NULL;
	PixelShader->ShaderProg[ShaderRecordType::Exterior] = NULL;
	PixelShader->ShaderProg[ShaderRecordType::Interior] = NULL;
	PixelShader->ShaderHandleBackup = PixelShader->ShaderHandle;
	PixelShader->SetName(ShaderName);
	if (!strcmp(ShaderName, "ISNOISESCROLLANDBLEND.pso")) {
		PixelShader->SetName("WATERHEIGHTMAP0.pso");
		TheShaderManager->WaterPixelShaders[0] = PixelShader;
	}
	else if (!strcmp(ShaderName, "ISNOISENORMALMAP.pso")) {
		PixelShader->SetName("WATERHEIGHTMAP1.pso");
		TheShaderManager->WaterPixelShaders[1] = PixelShader;
	}
	TheShaderManager->LoadShader(PixelShader);
	// A grass pixel shader without a replacement is drawn with the replacement grass vertex shaders,
	// whose outputs do not match what vanilla reads (GRASS23x001.pso read attenuation coordinates
	// where they write the shadow position). Every grass pixel shader needs routing through
	// GRASS23x000TMS.pso (GrassShaders::Templates); flag any that is not.
	if (!strncmp(ShaderName, "GRASS", 5) && !PixelShader->ShaderProg[ShaderRecordType::Default] && !PixelShader->ShaderProg[ShaderRecordType::Exterior] && !PixelShader->ShaderProg[ShaderRecordType::Interior])
		Logger::Log("WARNING: grass pixel shader %s has no NVR replacement; with the replaced grass vertex shaders it may draw wrongly. Add it to GrassShaders::Templates.", ShaderName);
	return (NiD3DPixelShader*)PixelShader;

}

void (__cdecl* SetShaderPackage)(int, int, UInt8, int, char*, int) = (void (__cdecl*)(int, int, UInt8, int, char*, int))Hooks::SetShaderPackage;
void __cdecl SetShaderPackageHook(int Arg1, int Arg2, UInt8 Force1XShaders, int Arg4, char* GraphicsName, int Arg6) {
	
	UInt32* ShaderPackage = (UInt32*)0x011F91C0;
	UInt32* ShaderPackageMax = (UInt32*)0x011F91BC;

	SetShaderPackage(Arg1, Arg2, Force1XShaders, Arg4, GraphicsName, Arg6);
	*ShaderPackage = 7;
	*ShaderPackageMax = 7;

}