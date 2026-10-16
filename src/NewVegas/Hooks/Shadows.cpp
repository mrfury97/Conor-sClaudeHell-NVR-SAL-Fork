#pragma once

void __fastcall RenderShadowMapHook(void* apThis) {
	// TAA jitters the world scene only, on the understanding that shadow maps are rendered before
	// it. If that ever stops being true, the cascades get fitted to a jittered frustum and shadow
	// edges shimmer -- say so in the log rather than leave it to be found by eye.
	static bool reportedJitteredShadows = false;
	if (!reportedJitteredShadows && TheShaderManager->Effects.TAA->IsJitterActive()) {
		Logger::Log("[WARNING] TAA : shadow maps rendered while the world camera was jittered; cascades will shimmer");
		reportedJitteredShadows = true;
	}

	GpuProfiler::Begin(GpuProfiler::ShadowMaps);
	{
		PerfProfiler::Scope Profile(PerfProfiler::TShadowMaps, PerfProfiler::ShadowMaps);
		TheShadowManager->RenderShadowMaps();
	}
	GpuProfiler::End(GpuProfiler::ShadowMaps);
	CdeclCall(0x871A50);
}

static void AddCastShadowFlag(TESObjectREFR* Ref, TESObjectLIGH* Light, NiPointLight* LightPoint) {
	
	ShadowsExteriorEffect::InteriorsStruct* ShadowsInteriors = &TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors;

	if (Light->lightFlags & TESObjectLIGH::LightFlags::kLightFlags_CanCarry) {
		LightPoint->CastShadows = ShadowsInteriors->TorchesCastShadows;
		LightPoint->CanCarry = 1;
		if (Ref == Player) {
			if (Player->isThirdPerson) {
				if (Player->firstPersonSkinInfo->LightForm == Light) LightPoint->CastShadows = 0;
			}
			else {
				if (Player->ActorSkinInfo->LightForm == NULL && Player->firstPersonSkinInfo->LightForm == Light) LightPoint->CastShadows = 0;
			}
			LightPoint->CanCarry = 2;
		}
	}		
	else if (Ref && Ref->baseForm && Ref->baseForm->formType == TESForm::FormType::kFormType_Light) {
		LightPoint->CastShadows = !(Ref->flags & TESForm::FormFlags::kFormFlags_NotCastShadows);
		LightPoint->CanCarry = 0;
	}

}

__declspec(naked) void AddCastShadowFlagHook() {

	__asm {
		mov     esi, [ebp + 0x008]
		pushad
		mov		ecx, [ebp - 0x164]
		push	eax
		push	ecx
		push	esi
		call	AddCastShadowFlag
		pop		esi
		pop		ecx
		pop		eax
		popad
		pop		ecx
		pop		esi
		mov     ecx, [ebp - 0x34]
		jmp		Jumpers::Shadows::AddCastShadowFlagReturn
	}

}

static void LeavesNodeName(BSTreeNode* TreeNode) {
	
	TreeNode->m_children.data[1]->SetName("Leaves");

}

__declspec(naked) void LeavesNodeNameHook() {

	__asm {
		mov [ebp - 0x70], eax
//		pushad
		push	eax 
		call	LeavesNodeName
		pop		eax
//		popad
		jmp		Jumpers::Shadows::LeavesNodeNameReturn
	}

}
