---
name: unreal-materials
description: Use when creating, editing, or inspecting Unreal Engine materials via Monolith MCP. Covers PBR setup, graph building, material instances, templates, HLSL nodes, validation, and previews. Triggers on material, shader, PBR, texture, material instance, material graph.
---

# Unreal Material Workflows

You have access to **Monolith** with 25 material actions via `material_query()`.

## Discovery

```
monolith_discover({ namespace: "material" })
```

## Asset Path Conventions

All asset paths follow UE content browser format (no .uasset extension):

| Location | Path Format | Example |
|----------|------------|---------|
| Project Content/ | `/Game/Path/To/Asset` | `/Game/Materials/M_Rock` |
| Project Plugins/ | `/PluginName/Path/To/Asset` | `/CarnageFX/Materials/M_Blood` |
| Engine Plugins | `/PluginName/Path/To/Asset` | `/Niagara/DefaultAssets/SystemAssets/NS_Default` |

## Key Parameter Names

- `asset_path` — the material asset path (NOT `asset`)

## Action Reference (25 actions)

### Read Actions
| Action | Key Params | Purpose |
|--------|-----------|---------|
| `get_all_expressions` | `asset_path` | List all expression nodes in a material |
| `get_expression_details` | `asset_path`, `expression_name` | Inspect a specific node's properties and pins |
| `get_full_connection_graph` | `asset_path` | Complete node/wire topology |
| `get_material_parameters` | `asset_path` | List all scalar/vector/texture parameters |
| `get_compilation_stats` | `asset_path` | Instruction counts (vertex + pixel shader), sampler usage, blend mode, compile status |
| `get_layer_info` | `asset_path` | Inspect material layer/blend stack |
| `export_material_graph` | `asset_path`, `include_properties`?, `include_positions`? | Serialize graph as JSON. Pass `include_properties: false` to reduce ~70% |
| `get_thumbnail` | `asset_path`, `save_to_file`? | Get thumbnail. Use `save_to_file: true` — inline base64 wastes context |
| `validate_material` | `asset_path`, `fix_issues`? | Check for broken connections, unused nodes, errors |
| `render_preview` | `asset_path` | Trigger material compilation and preview |

### Write Actions
| Action | Key Params | Purpose |
|--------|-----------|---------|
| `create_material` | `asset_path`, `blend_mode`?, `shading_model`?, `two_sided`? | Create a new empty material with properties |
| `build_material_graph` | `asset_path`, `graph_spec`, `clear_existing`? | Build entire graph from JSON spec (fastest path). Emits blend mode validation warnings (e.g. Opacity on Opaque, OpacityMask on non-Masked) |
| `create_custom_hlsl_node` | `asset_path`, `code`, `inputs`?, `additional_outputs`? | Add a Custom HLSL expression |
| `set_material_property` | `asset_path`, `blend_mode`?, `shading_model`?, `two_sided`?, etc. | Set material-level properties |
| `set_expression_property` | `asset_path`, `expression_name`, `property_name`, `value` | Set a property on an existing expression |
| `connect_expressions` | `asset_path`, `from_expression`, `to_expression`/`to_property` | Wire two expressions or wire to material output. Emits blend mode validation warnings when connecting to Opacity/OpacityMask outputs |
| `disconnect_expression` | `asset_path`, `expression_name`, `input_name`?, `target_expression`?, `output_index`? | Remove connections. Use `target_expression` + `output_index` to disconnect a specific connection |
| `delete_expression` | `asset_path`, `expression_name` | Delete an expression node |
| `create_material_instance` | `asset_path`, `parent_material` | Create a material instance |
| `set_instance_parameter` | `asset_path`, `parameter_name`, `scalar_value`/`vector_value`/`texture_value` | Set instance parameter |
| `duplicate_material` | `source_path`, `dest_path` | Duplicate a material asset |
| `recompile_material` | `asset_path` | Force recompile |
| `import_material_graph` | `asset_path`, `graph_json`, `mode`? | Deserialize graph from JSON |
| `begin_transaction` | `transaction_name` | Start an undo group |
| `end_transaction` | — | End an undo group |

## PBR Material Workflow

### 1. Create material, set properties, then build the graph

**CRITICAL:** `build_material_graph` requires a `graph_spec` wrapper object. The spec goes INSIDE `graph_spec`, not at the top level.

```
// Step 1: Create with properties
material_query({ action: "create_material", params: {
  asset_path: "/Game/Materials/M_Rock",
  shading_model: "DefaultLit"
}})

// Step 2: Build the graph (note the graph_spec wrapper!)
material_query({ action: "build_material_graph", params: {
  asset_path: "/Game/Materials/M_Rock",
  clear_existing: true,
  graph_spec: {
    nodes: [
      { id: "TexBC", class: "TextureSample", props: { Texture: "/Game/Textures/T_Rock_D" }, pos: [-400, 0] },
      { id: "TexN", class: "TextureSample", props: { Texture: "/Game/Textures/T_Rock_N", SamplerType: "Normal" }, pos: [-400, 200] },
      { id: "TexORM", class: "TextureSample", props: { Texture: "/Game/Textures/T_Rock_ORM" }, pos: [-400, 400] }
    ],
    connections: [
      { from: "TexORM", to: "TexBC", from_pin: "R", to_pin: "AmbientOcclusion" }
    ],
    outputs: [
      { from: "TexBC", from_pin: "RGB", to_property: "BaseColor" },
      { from: "TexN", from_pin: "RGB", to_property: "Normal" },
      { from: "TexORM", from_pin: "G", to_property: "Roughness" },
      { from: "TexORM", from_pin: "B", to_property: "Metallic" }
    ],
    custom_hlsl_nodes: []
  }
}})
```

**graph_spec fields:**
- `nodes[]` — `{ id, class, props?, pos? }` — standard expression nodes
- `custom_hlsl_nodes[]` — `{ id, code, description?, output_type?, inputs?, additional_outputs?, pos? }` — Custom HLSL nodes
- `connections[]` — `{ from, to, from_pin?, to_pin? }` — inter-node wires (IDs from nodes/custom_hlsl_nodes)
- `outputs[]` — `{ from, from_pin?, to_property }` — wires to material output pins (BaseColor, Normal, Roughness, etc.)

**ID format:** Use short descriptive IDs. After creation, `id_to_name` in the response maps your IDs to UE object names.

**clear_existing: true** clears all expressions but preserves material-level properties (BlendMode, ShadingModel, etc.).

**Blend mode validation:** `build_material_graph` and `connect_expressions` warn when you wire to an output that is inactive for the current blend mode — e.g. wiring to `Opacity` on an Opaque material, or `OpacityMask` on a non-Masked material. These are warnings, not errors, but the connection will have no effect until blend mode is changed.

### 2. Validate after changes
```
material_query({ action: "validate_material", params: { asset_path: "/Game/Materials/M_Rock" } })
```

## Editing Existing Materials

Always inspect before modifying:
```
material_query({ action: "get_all_expressions", params: { asset_path: "/Game/Materials/M_Skin" } })
material_query({ action: "get_full_connection_graph", params: { asset_path: "/Game/Materials/M_Skin" } })
```

Wrap modifications in transactions for undo support:
```
material_query({ action: "begin_transaction", params: { asset_path: "/Game/Materials/M_Skin", description: "Add emissive" } })
// ... make changes ...
material_query({ action: "end_transaction", params: { asset_path: "/Game/Materials/M_Skin" } })
```

## Checking Shader Instruction Counts

`get_compilation_stats` returns both vertex and pixel shader instruction counts:
```
material_query({ action: "get_compilation_stats", params: { asset_path: "/Game/Materials/M_Rock" } })
// Returns: num_vertex_shader_instructions, num_pixel_shader_instructions, num_samplers, blend_mode, etc.
```

Use this after graph changes to catch runaway instruction counts before they hit the profiler.

## Particle / VFX Material Conventions

When creating materials for Niagara particle systems, follow these conventions so the Niagara agent can use them correctly:

### Material Setup
- **Shading Model:** `Unlit` — particles shouldn't receive scene lighting
- **Blend Mode:** `Additive` for fire, glow, sparks. `Translucent` for smoke, fog, dust.
- **Two Sided:** Always enabled for particles

### Required Nodes
- **Particle Color:** Always multiply final color by `Particle Color` node — this lets Niagara control color per-particle via `Particles.Color`
- **Dynamic Parameter:** Add `DynamicParameter` node if the effect needs runtime control (e.g. erosion, intensity). Niagara drives these via `Particles.DynamicMaterialParameter`

### Soft Particle Edges (No Textures)
Use procedural radial gradients instead of texture samples for fully procedural particles:
```
Custom HLSL: "float2 c = TexCoords - 0.5; return saturate(1.0 - length(c) * 2.0);"
```
- Input: `TextureCoordinate` node (UV0)
- Output feeds into opacity (smoke) or emissive intensity (fire)
- For fire: power the gradient by 2-3 for tighter cores
- For smoke: multiply opacity by 0.3-0.5 for transparency

### Depth Fade
- Add `DepthFade` (50-100 units) on translucent particles to prevent hard intersections with geometry

### Naming Convention
- Particle materials: `M_<EffectName>Particle` (e.g. `M_FireParticle`, `M_SmokeParticle`)
- Save to `/Game/VFX/Materials/` alongside the Niagara systems

## Collaborating with Niagara Agent

When building materials for VFX, the material agent runs FIRST. The Niagara agent runs AFTER and assigns materials to renderers.

**What Niagara needs from you:**
1. Materials saved and compiled at known paths
2. `Particle Color` node wired in so Niagara can drive color
3. `DynamicParameter` node if Niagara needs per-particle material control
4. Correct blend mode (Additive vs Translucent) for the effect type
5. Unlit shading so particles aren't affected by scene lighting

**What to document in your response:**
- The asset path of each material created
- What blend mode was used and why
- Whether Dynamic Parameters are available and what they control
- Any special UV or texture coordinate requirements

## Rules

- **Graph editing only works on base Materials**, not MaterialInstanceConstants
- The primary asset param is `asset_path` (not `asset`)
- Always call `validate_material` after graph changes
- `build_material_graph` is the fastest way to create complex graphs — single JSON spec for all nodes + wires
- Use `export_material_graph` to snapshot a graph before making destructive changes
- Use `get_all_expressions` + `get_full_connection_graph` for inspection. Only use `export_material_graph` for round-tripping. Pass `include_properties: false` to reduce payload by ~70%
- Use `render_preview` or `get_thumbnail` with `save_to_file: true` — inline base64 wastes context window
- Blend mode warnings from `connect_expressions` / `build_material_graph` are informational — the connection is made, but the output pin is inactive unless the material's blend mode matches
- There are exactly 25 material actions — use `monolith_discover("material")` to see them all
