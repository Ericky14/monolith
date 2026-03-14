---
name: build_material_graph correct JSON schema
description: Correct field names and known quirks for build_material_graph — nodes/class keys, properties don't apply, connection naming, recommended workflow
type: feedback
---

## graph_spec field names (verified 2026-03-13)

- **Array key:** `nodes` (NOT `expressions` — that silently creates 0 nodes)
- **Node fields:** `class` for expression class (e.g. `"MaterialExpressionMultiply"`). `name` is accepted but NOT honored — nodes get auto-named like `MaterialExpressionMultiply_6`
- **CRITICAL: `type` does NOT work** — creates base `MaterialExpression` objects with no pins. Always use `class`.
- **Class shorthand:** Can omit `MaterialExpression` prefix (e.g. `"Multiply"`)
- **Properties do NOT apply** — `properties`/`props` in node specs are silently ignored. Must follow up with `set_expression_property` calls.
- **Connections:** `from_expression`, `to_expression`, `from_output`, `to_input`, `to_property` (same naming as `connect_expressions` action)
- **Connections CAN be included in graph_spec** and DO work. Put them in a `connections` array alongside `nodes`.

## Material functions vs expression classes

- **RadialGradientExponential** is a Material Function at `/Engine/Functions/Engine_MaterialFunctions01/Gradient/RadialGradientExponential`, NOT a native expression class
- Use Custom HLSL nodes for procedural math equivalents (proven approach)
- `MaterialExpressionMaterialFunctionCall` with function path reference is the alternative (untested via MCP)

## set_material_property

Uses named fields directly on params (`blend_mode`, `shading_model`, `two_sided`, `used_with_niagara_sprites`, etc.), NOT `property_name`/`value` pairs.

## Recommended workflow for new materials

1. `create_material` — blend/shading/domain/two_sided
2. `set_material_property` — usage flags (used_with_niagara_sprites etc.)
3. `build_material_graph` — `graph_spec` with `nodes` (class only, no properties) + `connections`
4. `create_custom_hlsl_node` — for HLSL nodes (these DO apply code/inputs correctly)
5. `connect_expressions` — wire HLSL nodes to rest of graph
6. `set_expression_property` — set values on each node needing non-defaults
7. `get_all_expressions` — verify node names (auto-generated, order matches spec)
8. `validate_material` + `recompile_material`

**Why:** Multiple sessions hit the same traps — wrong field names silently create broken nodes, properties silently ignored.

**How to apply:** Always follow the workflow above. Never trust that properties applied — verify with `get_expression_details` if in doubt.
