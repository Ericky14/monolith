# VFX Fire System — Issues & Bugs Found (2026-03-14)

Discovered during creation of NS_RealisticFire, BP_RealisticFire, M_Fire, M_Ember, M_Smoke.

## Niagara Issues

### ShapeLocation Non Uniform Scale may not apply to Sphere primitive
- **Problem:** Setting `Non Uniform Scale = (1,1,0.1)` on ShapeLocation with Sphere shape doesn't visibly flatten the spawn area. Particles still appear to come from a single point.
- **Root cause:** `Shape Primitive` is a static switch — can't be changed via `set_module_input_value`. Non Uniform Scale may only affect certain shape types, or the sphere implementation ignores it.
- **Fix:** Need to either (a) find a way to set static switches via MCP, or (b) remove the ShapeLocation module and add a new one with a different shape preset (Box/Cylinder/Disc), or (c) add a second ShapeLocation with an offset to fake a wider spawn.

### UseVelDistribution=true silently ignores Velocity vector
- **Problem:** When `UseVelDistribution=true`, the `Velocity` vector input is completely ignored. Speed comes from the linked `Velocity Speed` input instead. No warning or error.
- **Impact:** Velocity changes appeared to have no effect until we discovered this. Wasted several rounds of debugging.
- **Workaround:** Always set `UseVelDistribution=false` when using a direct Velocity vector.

### batch_execute doesn't support set_module_input_value
- **Problem:** `batch_execute` returns "Unknown op" for `set_module_input_value` operations.
- **Fix:** Add `set_module_input_value` to the batch_execute dispatch table in MonolithNiagaraActions.cpp.

### set_curve_value doesn't work on DataInterface curve inputs
- **Problem:** `set_curve_value` rejects DI-type curve inputs like `Uniform Curve Sprite Scale` (NiagaraDataInterfaceCurve). Must use `set_module_input_di` instead.
- **Fix:** Either (a) auto-detect DI curves in set_curve_value and route to DI path, or (b) document clearly which curve inputs are DI vs inline.

### ~~set_module_input_di requires "U" prefix for DI class~~ — FIXED (2026-03-14)
- **Was:** `di_class: "NiagaraDataInterfaceCurve"` failed with "DI class not found". Had to use `"UNiagaraDataInterfaceCurve"`.
- **Fix applied:** Handler now auto-resolves both forms — U prefix is optional.

### ScaleColor Scale RGB curve keys all read as value 0
- **Problem:** After setting RGB curve via `set_curve_value`, reading back the override shows all values as 0.000000. The color curve format may not match what was sent.
- **Investigate:** May be a serialization issue with vector curves vs separate R/G/B channels.

## Material Issues

### Custom HLSL node inputs must be defined at creation time
- **Problem:** Can't add inputs to a Custom node after creation via `set_expression_property`. Inputs like InUV, InTime must be specified in the `add_expression` call.
- **Impact:** Had to delete and recreate Custom_7 as Custom_0 with both inputs defined upfront.
- **Fix:** Document this limitation, or support `set_expression_inputs` action.

### UEditorAssetLibrary::LoadAsset fails on in-memory assets
- **Problem:** Materials created via `CreatePackage`/`NewObject` aren't found by `LoadAsset` until saved to disk.
- **Fix applied:** Added `FindFirstObject<UMaterial>` fallback + `SavePackage` in CreateMaterial. Already fixed.

## Blueprint Issues

### Set Timer by Function Name requires exact function name match
- **Problem:** The timer function name must exactly match a BP function name (case-sensitive). No error if the name doesn't match — the timer just never fires.
- **Note:** This is UE behavior, not a Monolith bug. Just document it.

### Old EventGraph nodes left behind after restructuring
- **Problem:** After disconnecting Tick and wiring BeginPlay→Timer, the old multiply/sin/MapRange nodes in EventGraph are now orphaned (disconnected). They compile fine but are dead code.
- **Fix:** Clean up orphaned nodes in BP_RealisticFire EventGraph (K2Node_CallFunction_1 through _9, K2Node_VariableGet_0 through _2).

## Future Improvements

- [ ] Add static switch support to Niagara MCP (`set_static_switch_value` action)
- [ ] Add `set_module_input_value` to batch_execute dispatch table
- [ ] Auto-detect DI vs inline curves in set_curve_value
- [x] Auto-strip/add "U" prefix for DI class names — FIXED (2026-03-14)
- [ ] Support adding inputs to existing Custom HLSL nodes
- [ ] Clean up orphaned nodes in BP_RealisticFire EventGraph
