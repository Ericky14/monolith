---
name: unreal-niagara
description: Use when creating, editing, or inspecting Niagara particle systems via Monolith MCP. Covers systems, emitters, modules, parameters, renderers, DI, and HLSL. Triggers on Niagara, particle, VFX, emitter, particle system.
---

# Unreal Niagara VFX Workflows

You have access to **Monolith** with 41 Niagara actions via `niagara.query()`.

## Discovery

```
monolith.discover({ namespace: "niagara" })
```

## Asset Path Conventions

All asset paths follow UE content browser format (no .uasset extension):

| Location | Path Format | Example |
|----------|------------|--------|
| Project Content/ | `/Game/Path/To/Asset` | `/Game/VFX/NS_Sparks` |
| Project Plugins/ | `/PluginName/Path/To/Asset` | `/CarnageFX/VFX/NS_BloodSpray` |
| Engine Plugins | `/PluginName/Path/To/Asset` | `/Niagara/DefaultAssets/SystemAssets/NS_Default` |

## Key Parameter Names

- `asset_path` — the Niagara system asset path (NOT `system` or `asset`)
- `emitter` — emitter name (string)
- `module_node` — module GUID returned by `get_ordered_modules` (NOT module display name)

## Action Reference

### System Management (8)
| Action | Key Params | Purpose |
|--------|-----------|---------|
| `create_system` | `asset_path` | Create a new Niagara system |
| `add_emitter` | `asset_path`, `emitter` | Add an emitter to a system |
| `remove_emitter` | `asset_path`, `emitter` | Remove an emitter |
| `duplicate_emitter` | `asset_path`, `emitter` | Duplicate an emitter |
| `set_emitter_enabled` | `asset_path`, `emitter`, `enabled` | Enable/disable an emitter |
| `reorder_emitters` | `asset_path`, `order` | Change emitter evaluation order |
| `set_emitter_property` | `asset_path`, `emitter`, `property`, `value` | Modify emitter settings |
| `request_compile` | `asset_path` | Force recompile the system |

### Read / Inspection (4)
| Action | Key Params | Purpose |
|--------|-----------|---------|
| `list_emitters` | `asset_path` | List all emitters in a system |
| `list_renderers` | `asset_path`, `emitter` | List all renderers on an emitter |
| `get_ordered_modules` | `asset_path`, `emitter` | Get modules with GUIDs (needed for module actions) |
| `get_module_graph` | `asset_path`, `emitter`, `module_node` | Get module's internal graph |

### Module Editing (8)
| Action | Key Params | Purpose |
|--------|-----------|---------|
| `get_module_inputs` | `asset_path`, `emitter`, `module_node` | List all inputs on a module |
| `add_module` | `asset_path`, `emitter`, `module_path`, `stage` | Add a module to an emitter stage |
| `remove_module` | `asset_path`, `emitter`, `module_node` | Remove a module |
| `move_module` | `asset_path`, `emitter`, `module_node`, `new_index` | Reorder a module |
| `set_module_enabled` | `asset_path`, `emitter`, `module_node`, `enabled` | Enable/disable a module |
| `set_module_input_value` | `asset_path`, `emitter`, `module_node`, `input`, `value` | Set a module input to a literal value |
| `set_module_input_binding` | `asset_path`, `emitter`, `module_node`, `input`, `binding` | Bind a module input to a parameter |
| `set_module_input_di` | `asset_path`, `emitter`, `module_node`, `input`, `di_name` | Set a module input to a dynamic input |

### Parameters (9)
| Action | Key Params | Purpose |
|--------|-----------|---------|
| `get_all_parameters` | `asset_path` | All parameters (system, emitter, particle) |
| `get_user_parameters` | `asset_path` | User-exposed parameters |
| `get_parameter_value` | `asset_path`, `parameter` | Get a parameter's current value |
| `get_parameter_type` | `asset_path`, `parameter` | Get a parameter's type info |
| `trace_parameter_binding` | `asset_path`, `parameter` | Follow parameter binding chain |
| `add_user_parameter` | `asset_path`, `name`, `type` | Add a user parameter |
| `remove_user_parameter` | `asset_path`, `name` | Remove a user parameter |
| `set_parameter_default` | `asset_path`, `parameter`, `value` | Set parameter default value |
| `set_curve_value` | `asset_path`, `parameter`, `keys` | Set curve keys on a curve parameter |

### Renderers (6)
| Action | Key Params | Purpose |
|--------|-----------|---------|
| `add_renderer` | `asset_path`, `emitter`, `type` | Add a renderer to an emitter |
| `remove_renderer` | `asset_path`, `emitter`, `renderer` | Remove a renderer |
| `set_renderer_material` | `asset_path`, `emitter`, `renderer`, `material` | Assign material to renderer |
| `set_renderer_property` | `asset_path`, `emitter`, `renderer`, `property`, `value` | Modify renderer settings |
| `get_renderer_bindings` | `asset_path`, `emitter`, `renderer` | Get renderer's attribute bindings |
| `set_renderer_binding` | `asset_path`, `emitter`, `renderer`, `binding`, `value` | Set a renderer attribute binding |

### Batch Operations (2)
| Action | Key Params | Purpose |
|--------|-----------|---------|
| `batch_execute` | `asset_path`, `commands` | Execute multiple actions in sequence |
| `create_system_from_spec` | `spec` | Create a complete system from a JSON specification |

### Data Interface & HLSL (2)
| Action | Key Params | Purpose |
|--------|-----------|---------|
| `get_di_functions` | `di_name` | List functions available on a data interface |
| `get_compiled_gpu_hlsl` | `asset_path`, `emitter` | Get the compiled GPU HLSL for an emitter (auto-compiles if needed) |

### Stubs (2) — NOT YET FUNCTIONAL
| Action | Status |
|--------|--------|
| `create_module_from_hlsl` | BLOCKED — not yet implemented |
| `create_function_from_hlsl` | BLOCKED — not yet implemented |

## Common Workflows

### Inspect a system
```
niagara.query({ action: "list_emitters", params: { asset_path: "/Game/VFX/NS_Sparks" } })
niagara.query({ action: "get_ordered_modules", params: { asset_path: "/Game/VFX/NS_Sparks", emitter: "Fountain" } })
niagara.query({ action: "get_module_inputs", params: { asset_path: "/Game/VFX/NS_Sparks", emitter: "Fountain", module_node: "<GUID from get_ordered_modules>" } })
```

### Create a system and add an emitter
```
niagara.query({ action: "create_system", params: { asset_path: "/Game/VFX/NS_Sparks" } })
niagara.query({ action: "add_emitter", params: { asset_path: "/Game/VFX/NS_Sparks", emitter: "Fountain" } })
```

### Set a module input value
```
niagara.query({ action: "set_module_input_value", params: {
  asset_path: "/Game/VFX/NS_Sparks", emitter: "Fountain",
  module_node: "<GUID>", input: "Lifetime", value: 2.0
}})
```

### Add a renderer with material
```
niagara.query({ action: "add_renderer", params: { asset_path: "/Game/VFX/NS_Sparks", emitter: "Fountain", type: "SpriteRenderer" } })
niagara.query({ action: "set_renderer_material", params: {
  asset_path: "/Game/VFX/NS_Sparks", emitter: "Fountain",
  renderer: "SpriteRenderer", material: "/Game/Materials/M_Particle"
}})
```

## Rules

- Use `monolith.discover("niagara")` to see per-action param schemas — there are 41 actions
- The primary asset param is `asset_path`, NOT `system` or `asset`
- Module actions require `module_node` (a GUID) — get it from `get_ordered_modules`
- Module stages: `Emitter Spawn`, `Emitter Update`, `Particle Spawn`, `Particle Update`, `Render`
- User parameters are the main interface for Blueprint/C++ control of effects
- Parameter actions now accept the `User.` prefix (e.g. `User.MyParam`) in addition to bare names
- `create_module_from_hlsl` and `create_function_from_hlsl` are stubs — they will error
