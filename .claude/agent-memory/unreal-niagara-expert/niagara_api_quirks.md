---
name: Niagara API Quirks
description: Non-obvious Monolith Niagara action behaviors discovered through testing
type: feedback
---

## batch_execute operation key
- The operation key in batch_execute is `"op"`, NOT `"action"`. Using `"action"` silently fails with "Unknown op".
- Example: `{"op": "set_module_input_value", "emitter": "Sparkles", ...}`

## create_system vs create_system_from_spec
- `create_system` with no template creates a blank system (no emitters). Use `add_emitter` afterward with template path like `/Niagara/DefaultAssets/Templates/Emitters/Fountain`.
- `create_system` with a template path expects a SYSTEM template, not emitter template. System templates may not exist at obvious paths.
- `create_system_from_spec` with `"template": "Fountain"` shorthand OR full path `/Niagara/DefaultAssets/Templates/Emitters/Fountain` both fail — emitter_asset resolves to empty string. The template key in create_system_from_spec emitter specs does NOT work.
- Safest approach: `create_system` (blank) + `add_emitter` with full emitter template path + `batch_execute` for bulk configuration.
- `add_module` with `index` parameter correctly inserts at the specified position (e.g. index=4 puts it before SolveForcesAndVelocity at index 5).
- ScaleSpriteSize module uses `UNiagaraDataInterfaceCurve` for `Uniform Curve Sprite Scale` input, with `CurveValues` array (not CurveR/G/B/A like color curves).

## LinearColor user parameter defaults
- `add_user_parameter` with `"type": "LinearColor"` and `"default": "(R=0.1,G=0.8,B=1.0,A=1.0)"` does NOT set the default correctly — it remains (0,0,0,1).
- `set_parameter_default` with JSON format `{"r":0.1,"g":0.8,"b":1.0,"a":1.0}` also reports success but doesn't actually change the value.
- Workaround: Set the color directly on the module input via `set_module_input_value` before binding, or set at runtime from C++/Blueprint.

## DataInterface inputs (ScaleColor curve, etc.)
- NEVER use `set_curve_value` on DataInterface-typed inputs. Use `set_module_input_di` with proper DI class.
- ScaleColor's `Linear Color Curve` is type `NiagaraDataInterfaceColorCurve` — use `set_module_input_di` with `di_class: "UNiagaraDataInterfaceColorCurve"` and config with `CurveR/G/B/A` arrays of `{Time, Value}` keys.

## Module ordering
- Newly added modules via `add_module` go to the END of the stage, even after SolveForcesAndVelocity.
- Force modules (CurlNoiseForce, GravityForce, Drag) must be BEFORE SolveForcesAndVelocity.
- Always call `move_module` after `add_module` if the default position is wrong.

## GPU HLSL check
- `get_compiled_gpu_hlsl` returns an error "Emitter is not GPU simulation" for CPU sim emitters — this is expected, not an actual compilation failure.
