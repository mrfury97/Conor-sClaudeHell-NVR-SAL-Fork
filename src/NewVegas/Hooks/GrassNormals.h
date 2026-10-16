#pragma once

// Per-texture normal maps for grass.
//
// Grass draws with its diffuse texture only. For each grass texture this finds a matching normal
// map beside it -- <diffuse>_n.dds, a loose file under Data\Textures -- and binds it to sampler
// s10 of GRASS23x000TMS.pso for that grass's draws, with GrassNormalParams (c199) telling the
// shader whether a map is bound and how strongly to apply it. Grass without a map is untouched.
//
// Hooked at TallGrassShader's UpdateConstants, which the engine calls once per grass geometry
// before drawing it: the same vtable slot NVR already hooks on SkyShader, and verified at install
// against both classes' RTTI before anything is patched.
namespace GrassNormals {
	void Install();			// once, from AttachHooks
	void InvalidateState();	// from SetShadersHook: anything may since have changed s10, s11 or c199
}
