---
name: Material-Niagara Collaboration Workflow
description: Best practices for procedural particle materials, material-niagara handoff, renderer bindings, and common pitfalls learned from NS_RealisticFire exploration
type: reference
---

## Procedural Particle Material Patterns

### Fire Particles (Additive)
- **Blend mode:** Additive (`BLEND_Additive`) -- adds light without writing alpha, good for emissive effects
- **Shading model:** Unlit -- particles don't receive scene lighting, rely on emissive color
- **Soft edges:** Custom HLSL radial gradient (distance from UV center), not a texture -- fully procedural
- **Emissive boost:** Multiply emissive by scalar (e.g. x4) for HDR bloom pickup
- **Color:** Set in material as base, modulated at runtime by Niagara's `Particles.Color` binding

### Smoke Particles (Translucent)
- **Blend mode:** Translucent -- allows alpha blending against scene
- **Shading model:** Unlit (common for simple smoke; lit smoke needs volumetric or 6-point lighting)
- **Soft edges:** Same radial gradient technique as fire
- **DepthFade:** Critical for avoiding hard clipping against geometry (75u is reasonable for medium-scale smoke)
- **Opacity:** Kept low (x0.35 multiplier) -- smoke should be subtle, high opacity looks like solid blobs
- **Color:** Dark grey base, modulated by particle color

### Key Material Techniques for Particles
- **Radial gradient via HLSL:** `1.0 - saturate(length(UV - 0.5) * 2.0)` -- circular falloff from center, no texture needed
- **DepthFade node:** Softens intersection with geometry; value is distance in world units
- **Particle Color node:** Reads `Particles.Color` from Niagara -- the bridge between material and particle system
- **Dynamic Material Parameters:** 4 slots (Vector4f each) for passing custom per-particle data to materials

## Material-Niagara Handoff Checklist

### What the Material Agent Must Provide
1. Correct blend mode for the effect type (Additive for light-emitting, Translucent for alpha-blended)
2. Unlit shading model for most particles (lit only for advanced volumetric effects)
3. Particle Color node connected to emissive/opacity so Niagara color control works
4. DepthFade on translucent materials to prevent hard edges at geometry intersections
5. Any Dynamic Material Parameter inputs documented (which slot, what it controls)
6. Asset path in `/Game/` format for `set_renderer_material`

### What the Niagara Agent Must Set Up
1. Sprite renderer with material assigned via `set_renderer_material`
2. ColorBinding → `Particles.Color` (default, usually correct)
3. ScaleColor module with appropriate lifetime color curve (DI type: `NiagaraDataInterfaceColorCurve`)
4. If using Dynamic Material Parameters: DynamicMaterialBinding → appropriate particle attribute
5. NormalizedAgeBinding → `Particles.NormalizedAge` (default, needed for lifetime-based material effects)
6. Correct sort order if multiple translucent emitters overlap

## Renderer Binding Details (from NS_RealisticFire)

### Sprite Renderer Default Bindings (all auto-set)
- PositionBinding → Particles.Position
- ColorBinding → Particles.Color (drives Particle Color node in material)
- SpriteSizeBinding → Particles.SpriteSize
- SpriteRotationBinding → Particles.SpriteRotation
- NormalizedAgeBinding → Particles.NormalizedAge
- DynamicMaterialBinding → Particles.DynamicMaterialParameter (slots 0-3)
- MaterialRandomBinding → Particles.MaterialRandom

### Light Renderer Bindings
- ColorBinding → Particles.Color (shares fire color)
- RadiusBinding → Particles.LightRadius
- LightExponentBinding → Particles.LightExponent
- VolumetricScatteringBinding → Particles.LightVolumetricScattering

## Common Pitfalls

### LinearColor User Parameter Default Bug
- `add_user_parameter` with `"type": "LinearColor"` does NOT correctly set default values
- The value stays at (0,0,0,1) regardless of what you pass
- **Workaround:** Set color directly on module inputs via `set_module_input_value`, or set at runtime from Blueprint/C++

### Material Assignment
- `set_renderer_material` takes the full asset path: `/Game/VFX/Materials/M_FireParticle`
- The renderer reports it with object name suffix: `M_FireParticle.M_FireParticle` -- this is normal UE asset reference format
- Material must be saved/compiled before assigning to renderer, or it may appear as default material

### Emitter Display Names
- `list_emitters` returns display names (e.g. "Fire", "Smoke")
- These same display names work correctly in `list_renderers`, `get_ordered_modules`, `get_module_inputs`, `get_renderer_bindings` -- no need to use handle IDs
- If display names fail, the emitter may have been renamed without updating the handle

### Module GUID Sharing Across Emitters
- When emitters are duplicated, system-level module GUIDs (EmitterState, SpawnRate, etc.) can be shared
- Each emitter still has its own independent module instances despite shared GUIDs
- ScaleSpriteSize and other emitter-specific modules typically get unique GUIDs

### GPU vs CPU Sim Considerations
- Fire on GPU sim (thousands of particles possible), Smoke on CPU sim (fewer particles, simpler physics)
- GPU sim emitters: `get_compiled_gpu_hlsl` works; CPU sim emitters return "Emitter is not GPU simulation" error (expected, not a bug)
- Light renderer works with both GPU and CPU emitters

### Additive vs Translucent Sort Order
- Additive particles don't need depth sorting (they're commutative: order doesn't matter)
- Translucent particles DO need sorting -- CustomSortingBinding → Particles.NormalizedAge is the default
- Fire (additive) behind Smoke (translucent) is the correct emitter order for visual layering

## Fire+Smoke System Architecture Pattern

Standard module stack for both emitters:
```
Emitter Update:
  EmitterState → SpawnRate

Particle Spawn:
  InitializeParticle → ShapeLocation → AddVelocity

Particle Update:
  ParticleState → GravityForce → Drag → ScaleColor → ScaleSpriteSize → SolveForcesAndVelocity
```

Key differences between fire and smoke emitters should be in:
- SpawnRate (fire higher, smoke lower)
- InitializeParticle (fire: smaller/shorter lifetime, smoke: larger/longer lifetime)
- GravityForce (fire: negative/upward, smoke: slight upward)
- Drag (smoke: higher drag for slower motion)
- ScaleColor curve (fire: bright→dim, smoke: opacity fade-out)
- ScaleSpriteSize curve (fire: shrink over life, smoke: grow over life)
