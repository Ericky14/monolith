# Monolith MCP — Master Wishlist

Compiled 2026-03-14 from hands-on stress testing across Blueprint, Niagara, and Material agents.
**121 items total** (17 bug fixes + 104 new features/improvements).

Current action counts: Blueprint 46, Niagara 44, Material 25 = **115 total**
Estimated after full implementation: Blueprint ~82, Niagara ~65, Material ~69 = **~216 total**

---

## P0 — Crashes & Critical Bugs (Fix First)

These actively crash the editor, silently corrupt data, or force expensive workarounds that double the cost of every session.

### Crashes

| # | Domain | Issue | Details |
|---|--------|-------|---------|
| 1 | BP | `rename_component` crashes on name collision | Renaming `DefaultSceneRoot1` → `DefaultSceneRoot` when one exists = fatal `Obj.cpp:349`. Needs existence check before rename. |

### Silent Failures / Data Corruption

| # | Domain | Issue | Details |
|---|--------|-------|---------|
| 2 | Niagara | `set_parameter_default` silently fails for LinearColor | Reports success but value stays `(0,0,0,1)`. Same on `add_user_parameter`. Float defaults work fine. |
| 3 | Niagara | `move_module` reports success but no-ops | Module stays at original index. Workaround (remove + re-add) loses all input overrides. |
| 4 | Niagara | `set_curve_value` creates orphaned pins | Corrupts compilation. Only recovery is deleting the entire system. |
| 5 | Material | `connect_expressions` ignores `to_input` name | All connections go to input index 0. Multi-input nodes (Multiply A/B, Lerp Alpha) can't be wired correctly. Forces single `build_material_graph` calls. |
| 6 | Material | `set_expression_property` zeroes Constant `R` value | Setting `R` on `MaterialExpressionConstant` always writes 0.0. Forces delete-and-rebuild. |
| 7 | Material | `set_material_property` silently accepts prefixed enums | `BLEND_Additive` reports `changes: 1` but doesn't persist. Only short names (`Additive`) work. |
| 8 | Material | `disconnect_expression` disconnects ALL outputs | No way to target a specific output pin. Forces full rewire of collateral damage. |

### Missing Return Data

| # | Domain | Issue | Details |
|---|--------|-------|---------|
| 9 | Material | `build_material_graph` doesn't return created node names | Can't do follow-up edits because you don't know what names UE assigned. |
| 10 | Material | `get_compilation_stats` missing pixel shader instruction count | The one number that actually matters for material performance. |

### Stability

| # | Domain | Issue | Details |
|---|--------|-------|---------|
| 11 | BP | Parallel `add_variable` calls overwhelm HTTP listener | 5 simultaneous calls killed the socket mid-batch. |
| 12 | BP | `create_blueprint` causes multi-second MCP dropout | Heavy asset save stalls the server. Other agents see connection failures. |

### Inconsistencies

| # | Domain | Issue | Details |
|---|--------|-------|---------|
| 13 | Niagara | `add_renderer` requires `U` prefix, `list_renderers` omits it | Must use `UNiagaraLightRendererProperties` to add, returns `NiagaraLightRendererProperties` on list. Accept both forms. |
| 14 | BP | `add_node` rejects `"function"`, requires `"CallFunction"` | Editor UI calls them "functions" but the tool needs the internal type name. No alias layer. |
| 15 | BP | `GetActorLocation` not resolved, must use `K2_GetActorLocation` | Editor auto-aliases this, Monolith doesn't. |
| 16 | BP | `SetCastShadows` only resolves via `LightComponentBase` | Should walk full inheritance chain on the input class. |
| 17 | Niagara | Module display names ≠ script paths | `Gravity` vs `GravityForce`, `ScaleColorOverLife` vs `ScaleColor`. No way to know without inspecting existing systems. |

---

## P0 — Critical Missing Features

### Blueprint

| # | Action | Details |
|---|--------|---------|
| 18 | `batch_execute` | Port from Niagara namespace. Single call runs `add_variable` × 8, `add_component` × 4, `add_node` × 30, `connect_pins` × 25, then compiles. Params: `asset_path`, `operations[]`, `stop_on_error`, `compile_on_complete`. Returns per-op results with index. This alone cuts a 40-call BP build to 1 call. **The single highest-leverage change across all domains.** |
| 19 | `resolve_node` | Dry-run node creation. Params: `node_type`, `function_name`, `target_class?`. Returns: `resolved_name` (K2_GetActorLocation), `resolved_class`, `pins[]`, `warnings[]`. |
| 20 | `search_functions` | Find function names without knowing them. Params: `query` ("actor location", "cast shadows"), `class_filter?`, `include_inherited`. Returns: `function_name`, `class_name`, `k2_name`, `is_pure`, `inputs`, `outputs`. |
| 21 | `get_node_details` | Full pin list, connections, and defaults for a specific node. Params: `asset_path`, `node_id`, `graph_name?`. Currently requires fetching entire graph. |
| 22 | `ping` / `get_status` | `ping` returns `{ status, uptime_seconds, pending_ops }` immediately. `get_status` returns current BP being edited, last compile result, ongoing operations. |

### Niagara

| # | Action | Details |
|---|--------|---------|
| 23 | `search_module_scripts` | **#1 Niagara pain point.** Params: `query` (fuzzy match against display name, file name, path), `category?` (spawn/update/forces/color/etc), `limit?`. Returns: `display_name`, `script_path`, `category`, `usage_hint`, `description`. |
| 24 | `list_module_scripts` | Browse all registered Niagara module scripts. Params: `usage?` (emitter_spawn/particle_update/etc), `category?`. Returns: `display_name`, `script_path`, `category`, `usage_stages[]`. |
| 25 | `configure_curve_keys` | Set keys on existing curve DI (since `set_curve_value` is broken). Params: `asset_path`, `emitter`, `module_node`, `input`, `keys[]` ({time, value} or {time, r, g, b, a}), `interp?` (linear/cubic/constant). |

### Material

| # | Action | Details |
|---|--------|---------|
| 26 | `disconnect_output_pin` | Targeted output disconnection. Params: `asset_path`, `expression_name`, `output_name?`, `output_index?`, `target_expression?` (only disconnect link to this node). |
| 27 | `get_instance_parameters` | Read current instance override values. Returns: `scalar[]`, `vector[]`, `texture[]`, `switch[]` with name, value, override status. Currently NO way to read instance values. |

---

## P1 — High Impact Quality of Life

### Blueprint — Node Creation & Wiring

| # | Action | Details |
|---|--------|---------|
| 28 | `add_node` aliases | Accept common aliases: `function`→`CallFunction`, `get`→`VariableGet`, `set`→`VariableSet`, `branch`/`if`→`IfThenElse`, `cast`→`DynamicCast`, `for_each`→`ForEachLoop`, `delay`→`Delay`, `print`→`PrintString`. When unknown, return closest matches. |
| 29 | `add_nodes_bulk` | Place multiple nodes in one call. Params: `asset_path`, `graph_name?`, `nodes[]` with caller-assigned `temp_id`, `auto_layout?`. Returns: `temp_id` → `node_id` mapping. `temp_id` enables referencing in subsequent `connect_pins_bulk`. |
| 30 | `connect_pins_bulk` | Wire multiple connections in one call. Params: `asset_path`, `graph_name?`, `connections[]` ({source_node, source_pin, target_node, target_pin}). Returns per-connection success/failure. |
| 31 | `set_pin_defaults_bulk` | Set multiple pin defaults in one call. Params: `asset_path`, `graph_name?`, `defaults[]` ({node_id, pin_name, value}). |
| 32 | Object pin defaults — clear error | `set_pin_default` should reject object-type pins immediately with "use set_variable_default" instead of silently accepting and failing at compile. Ideal: `set_node_object_default` action that auto-creates a hidden variable + VariableGet + wires it. |

### Blueprint — Templates & Scaffolding

| # | Action | Details |
|---|--------|---------|
| 33 | `apply_template` | Common graph patterns: `null_checked_component_getter`, `event_dispatcher_setup`, `interface_message_call`, `begin_overlap_with_cast`, `timeline_float`, `save_game_read_write`. Params: `asset_path`, `template`, `graph_name?`, `params` (template-specific). Returns: `nodes_created[]`, `entry_node`, `exit_node`. |
| 34 | `scaffold_interface_implementation` | After `implement_interface`, auto-create stub function graphs for all required methods. Params: `asset_path`, `interface_class`, `add_default_return?`. |
| 35 | `get_interface_functions` | Query what functions an interface requires. Params: `interface_class`. Returns: `functions[]` ({name, inputs, outputs, is_event}). |

### Blueprint — Inspection & Editing

| # | Action | Details |
|---|--------|---------|
| 36 | `get_function_signature` | Read function inputs, outputs, flags, local variables. Params: `asset_path`, `function_name`. |
| 37 | `get_component_property` | Read current component property values. Params: `asset_path`, `component_name`, `property_name`. |
| 38 | `get_all_component_properties` | Full property dump for a component. |
| 39 | Event dispatcher full CRUD | `set_event_dispatcher_params`, `remove_event_dispatcher`, `rename_event_dispatcher`, `get_event_dispatcher_details`. |
| 40 | Compile error → node ID linking | `compile_blueprint` response should include `node_id`, `graph_name`, `pin_name`, `severity` per error. |

### Niagara — Property Discovery

| # | Action | Details |
|---|--------|---------|
| 41 | `list_emitter_properties` | Discover what `set_emitter_property` accepts. Params: `asset_path`, `emitter`. Returns: `name`, `type`, `current_value`, `description`. |
| 42 | `list_renderer_properties` | Same for `set_renderer_property`. Params: `asset_path`, `emitter`, `renderer_index?`. Returns: `name`, `type`, `current_value`. |
| 43 | `get_module_input_value` | Read current override value (not just metadata). Params: `asset_path`, `emitter`, `module_node`, `input`. Returns: `value`, `is_default`, `is_linked`, `linked_parameter`. Or extend `get_module_inputs` to include `current_value`. |

### Niagara — System Management

| # | Action | Details |
|---|--------|---------|
| 44 | `set_system_property` | Write system-level properties: `bFixedBounds`, `WarmupTime`, `WarmupTickCount`, `MaxPoolSize`, etc. Params: `asset_path`, `property`, `value`. Returns old and new values. |
| 45 | `get_system_properties` | Read system-level settings. Params: `asset_path`. Returns: `fixed_bounds`, `warmup_time`, `max_pool_size`, `effect_type`, etc. |
| 46 | `get_system_summary` | One-call overview instead of 3+ round trips. Returns: `system_properties`, `user_parameters[]`, `emitters[]` ({name, sim_target, module_count, renderer_count}). |
| 47 | `get_emitter_summary` | Combined `get_ordered_modules` + `list_renderers` + key properties in one call. |
| 48 | `clone_emitter` | Copy emitter within same system. Params: `asset_path`, `emitter`, `new_name?`. Returns: `emitter_name`, `emitter_id`, `index`. |

### Niagara — Error Handling

| # | Action | Details |
|---|--------|---------|
| 49 | Better `add_module` error messages | Fuzzy-match against known modules and suggest alternatives. Current: "Failed to load module script". Better: "Did you mean: GravityForce.GravityForce?" |
| 50 | Consistent input name handling | Three conventions depending on action (bare `SpawnRate`, `Module.Gravity`, mixed). Accept both forms everywhere, normalize internally. |

### Material — Graph Editing

| # | Action | Details |
|---|--------|---------|
| 51 | `update_custom_hlsl_node` | Edit HLSL code without rebuilding node. Params: `asset_path`, `expression_name`, `code?`, `output_type?`, `inputs?`, `additional_outputs?`. **Biggest procedural material friction point.** |
| 52 | `replace_expression` | Swap node type in-place (e.g., Constant3Vector → VectorParameter). Params: `asset_path`, `expression_name`, `new_class`, `new_props?`, `preserve_connections?`. Pin-name-matching reconnection. |
| 53 | `list_expression_classes` | Enumerate available node types. Params: `filter?`, `category?`. Returns: `class`, `category`, `input_pins[]`, `output_pins[]`. |
| 54 | `get_expression_pin_info` | Query pin names for a class without creating an instance. Params: `class_name`. Returns: `inputs[]`, `outputs[]` with names and types. |
| 55 | `get_expression_connections` | Query connections for a single node. Returns: `inputs[]` and `outputs[]` with connected node/pin info. |
| 56 | `move_expression` | Reposition nodes. Params: `asset_path`, `expression_name`, `pos_x`, `pos_y`, `relative?`. Batch variant: `move_expressions` with array. |

### Material — Properties & Instances

| # | Action | Details |
|---|--------|---------|
| 57 | `get_material_properties` | Read all material-level settings. Returns: `blend_mode`, `shading_model`, `domain`, `two_sided`, etc. |
| 58 | `set_instance_parameters` (batch) | Set multiple instance parameters in one call. Array of {name, type, value}. |
| 59 | `set_instance_parent` | Reparent a material instance. Params: `asset_path`, `new_parent`. |
| 60 | `clear_instance_parameter` | Remove a single override, revert to parent default. |
| 61 | Static switch support in `create_material_instance` | Accept `static_switch_parameters` and `static_component_mask_parameters`. |
| 62 | Additional `set_material_property` properties | `fully_rough`, `translucency_lighting_mode`, `translucency_pass`, `decal_blend_mode`, `output_velocity`, `cast_shadow_as_masked`, `stencil_compare`, `stencil_ref_value`, `used_with_niagara_ribbons`, `used_with_static_meshes`, etc. |
| 63 | Additional `create_material` properties | `opacity_mask_clip_value`, `used_with_*` flags, `dithered_lod_transition`, `fully_rough`, `tangent_space_normal`, etc. Saves follow-up `set_material_property` calls. |

### Material — Validation

| # | Action | Details |
|---|--------|---------|
| 64 | `validate_material` — blend mode output validation | Warn: wiring to `OpacityMask` on Additive (no effect), missing `Opacity` on Translucent, subsurface outputs on non-subsurface model, emissive > 1.0 on Additive without HDR. |
| 65 | Error messages with valid values | When a value is rejected, include valid options. Current: "Failed to set blend_mode". Better: "Invalid 'BLEND_Additive'. Valid: Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite, AlphaHoldout". |
| 66 | `save_material` | Explicit save to disk. Currently changes are in-memory only until auto-save. |

### Material — Functions

| # | Action | Details |
|---|--------|---------|
| 67 | `create_material_function` | Params: `asset_path`, `description?`, `expose_to_library?`, `library_categories?`. |
| 68 | `build_function_graph` | Same schema as `build_material_graph` but with `inputs[]` and `outputs[]` definitions. |
| 69 | `get_function_info` | Read function inputs, outputs, description, categories. |

### Material — Batch Operations

| # | Action | Details |
|---|--------|---------|
| 70 | `batch_set_material_property` | Apply properties to multiple materials at once. Params: `asset_paths[]`, `properties`. |
| 71 | `batch_recompile` | Recompile multiple materials in one call. Params: `asset_paths[]`. |

### Material — HLSL & Textures

| # | Action | Details |
|---|--------|---------|
| 72 | `create_custom_hlsl_node` include paths | Accept `include_file_paths?` and `additional_defines?` for complex HLSL referencing engine utilities. |
| 73 | `import_texture` | Import from disk. Params: `source_file`, `dest_path`, `compression?`, `srgb?`, `lod_group?`, `max_size?`, `virtual_texture?`. Closes the loop on fully automated material creation. |

### Material — Preview

| # | Action | Details |
|---|--------|---------|
| 74 | `render_preview` improvements | Accept `preview_mesh?` (Sphere/Cube/Plane/asset path), `rotation?`, `save_to_file?`, `background?`. |

---

## P2 — Workflow Accelerators

### Blueprint

| # | Action | Details |
|---|--------|---------|
| 75 | `add_timeline` | Create timeline nodes with tracks, keys, loop/autoplay settings. Params: `timeline_name`, `duration`, `tracks[]` ({name, type, keys[]}). |
| 76 | `edit_timeline` / `get_timeline_data` | Modify and read existing timelines. |
| 77 | `promote_pin_to_variable` | Takes a pin reference, creates matching variable, wires VariableGet automatically. Currently requires 3 separate calls. |
| 78 | `add_comment_node` | Comment box around a group of nodes. Params: `text`, `node_ids[]`, `color?`. |
| 79 | `validate_blueprint` improvements | Detect: unconnected required pins, orphaned nodes, unused variables, uncalled functions, Tick usage with performance warning. Return node IDs. |
| 80 | `set_macro_params` | Set input/output tunnels of a macro. |
| 81 | `get_blueprint_info` expansion | Return: `last_compiled`, `compile_status`, `has_tick`, `graph_names[]`, `file_size_kb`. |
| 82 | `add_event_node` | Dedicated action for event entry nodes (BeginPlay, Tick, Overlap, etc.) without needing to know internal node type. |
| 83 | `get_implemented_events` | List all events with entry nodes in any graph. |
| 84 | `swap_component_names` | Swap two component names atomically. |
| 85 | `clear_construction_script` | Remove all non-entry nodes from construction script. |
| 86 | Cast node: rename `actor_class` → `cast_class` | Accept any UObject path, not just actors. Deprecated alias for backwards compat. |
| 87 | Soft reference type string docs + validation | Document `TSoftObjectPtr` / `TSoftClassPtr` type strings for `add_variable`. |
| 88 | `add_replicated_variable` | Add variable + auto-create `OnRep_` stub + link. Params: `replication_condition`, `create_on_rep_function?`. |

### Niagara

| # | Action | Details |
|---|--------|---------|
| 89 | `export_system_spec` | Reverse-engineer existing system into `create_system_from_spec`-compatible JSON. Params: `asset_path`, `include_values?`. |
| 90 | `configure_data_interface` | Set DI properties after assignment. Params: `emitter`, `module_node`, `input`, `properties` ({NoiseFrequency: 1.5, etc}). |
| 91 | `add_event_handler` | Inter-emitter events (death, collision, location). Params: `source_emitter`, `target_emitter`, `event_type`, `handler_script?`. |
| 92 | `duplicate_system` | Clone entire Niagara system asset. Params: `asset_path`, `save_path`. |
| 93 | `set_fixed_bounds` | Set explicit bounds box. Params: `emitter?` (system if omitted), `min`, `max`. Critical for GPU perf. |
| 94 | `validate_system` | Pre-compile check: missing renderers, unbound inputs, GPU + Light renderer, missing materials. Returns: `errors[]`, `warnings[]`, `suggestions[]`. |
| 95 | `get_compile_errors` | Detailed Niagara compile errors. Returns: `errors[]` ({emitter, module, message, severity}). |
| 96 | `set_effect_type` | Assign effect type for scalability/cull distance. Params: `asset_path`, `effect_type` (path or "none"). |
| 97 | `create_emitter` (truly empty) | No template required. Params: `asset_path`, `name`, `sim_target?`. |
| 98 | `set_emitter_sim_target` | Dedicated CPU/GPU toggle with proper `PostEditChange` + `RebuildEmitterNodes` chain. |

### Material

| # | Action | Details |
|---|--------|---------|
| 99 | `compare_materials` | Diff two material graphs. Returns: added/removed/modified nodes and connections. |
| 100 | `auto_layout` | Automatic topological-sort-based graph layout. Params: `algorithm?`, `spacing?`. |
| 101 | `duplicate_expression` | Clone a node within same graph. Params: `expression_name`, `new_name?`, `offset?`. |
| 102 | `rename_expression` | Set display name (Desc property). |
| 103 | `batch_create_instances` | Create a family of instances from one parent. Params: `parent_material`, `instances[]` ({name, path, parameters}). |
| 104 | `list_material_instances` | Find all instances of a parent. Params: `parent_path`, `recursive?`. |
| 105 | `validate_material` performance warnings | Instruction thresholds, excessive samplers, dynamic branching, uncompressed textures. |
| 106 | `build_material_graph` merge mode | If node with same ID exists, UPDATE properties instead of creating duplicate. |
| 107 | `build_material_graph` comment boxes | `comments[]` in graph spec: `text`, `nodes[]` to encompass, `color`. |
| 108 | Reroute/named reroute node support | `MaterialExpressionReroute` and `NamedRerouteDeclaration`/`Usage` in graph spec. |
| 109 | `render_expression_preview` | Preview a single node's output without wiring to final output. |
| 110 | `find_textures_by_convention` | Given base name `T_Wood_Oak`, find `_D`, `_N`, `_ORM`, `_E`, `_H`, `_M` variants. |
| 111 | `create_texture_from_render_target` | Bake RT to texture asset. |
| 112 | Material Parameter Collection support | `create_parameter_collection`, `set_collection_parameter_value`. |
| 113 | `copy_material_graph` | Copy graph from one material to another. Different from `duplicate_material`. |
| 114 | Substrate/material attributes future-proofing | `get_substrate_info`, `MakeMaterialAttributes`/`BreakMaterialAttributes` support in graph spec. |

---

## P3 — Advanced & Future Features

### Blueprint

| # | Action | Details |
|---|--------|---------|
| 115 | `diff_blueprints` | Compare two BPs or a BP against last compile. Returns structural differences. |
| 116 | `copy_nodes` / `paste_nodes` | Copy a set of nodes to a clipboard slot, paste into same or different graph/BP. |
| 117 | `move_nodes_bulk` | Move multiple nodes preserving relative layout. |
| 118 | `auto_layout` | Run editor's graph layout algorithm on a graph or subregion. |
| 119 | `export_graph` / `import_graph` | Serialize/deserialize function graphs as portable JSON for template reuse. |
| 120 | `duplicate_function` | Copy function graph to new name within same BP. |
| 121 | `undo_last` | Undo last Monolith operation via editor undo stack. Safety net for destructive edits. |
| 122 | `get_blueprint_stats` | Node count per graph, estimated frame cost tier, Tick usage. |
| 123 | `set_blueprint_description` / `set_blueprint_category` | Asset metadata. |
| 124 | `reparent_blueprint` dry_run | Return what would break without making the change. |
| 125 | `get_nodes_in_region` | Return node IDs within a bounding box. |

### Niagara

| # | Action | Details |
|---|--------|---------|
| 126 | `add_dynamic_input` / `set_dynamic_input_value` / `search_dynamic_inputs` | Dynamic inputs on module pins — inline expressions, random ranges, curve lookups. |
| 127 | `add_simulation_stage` | Multi-pass GPU sim (fluid, neighbor grid). Params: `emitter`, `name`, `iteration_source?`, `data_interface?`. |
| 128 | `add_particle_reader` | Inter-emitter particle attribute reading. Params: `reader_emitter`, `source_emitter`, `name?`. |
| 129 | `list_data_interfaces` | Browse available DI classes. Params: `filter?`. Returns: `class_name`, `display_name`, `description`, `category`. |
| 130 | `set_scalability_settings` | Per-platform scaling. Params: `quality_level`, `max_distance?`, `spawn_count_scale?`. |
| 131 | `preview_system` | Trigger preview in editor. Params: `action` (reset/pause/resume). |
| 132 | `batch_execute` improvements | Conditional execution, variable passing between steps (`$1.module_node`), rollback on failure. |
| 133 | `create_system_from_spec` improvements | Display name resolution for modules, inline curves, material assignment in spec, dynamic inputs in spec. |
| 134 | `create_system_from_preset` / `save_emitter_preset` / `load_emitter_preset` | Named presets and reusable emitter configs. |
| 135 | Ribbon renderer configuration guidance | Document valid properties for `set_renderer_property` on ribbon renderers (UV tiling, width mode, tessellation, facing mode). |

---

## Cross-Domain Themes

### Theme 1 — Discovery/Search (all 3 domains)
The most consistent pain point. Every domain needs discoverability for its building blocks.
- **BP:** `search_functions` (#20), `resolve_node` (#19), node type aliases (#28)
- **Niagara:** `search_module_scripts` (#23), `list_module_scripts` (#24), `list_renderer_properties` (#42), `list_emitter_properties` (#41)
- **Material:** `list_expression_classes` (#53), `get_expression_pin_info` (#54)

### Theme 2 — Bulk Operations (all 3 domains)
Round-trip overhead is the second biggest productivity killer.
- **BP:** `batch_execute` (#18), `add_nodes_bulk` (#29), `connect_pins_bulk` (#30), `set_pin_defaults_bulk` (#31)
- **Niagara:** `batch_execute` improvements (#132)
- **Material:** `batch_set_material_property` (#70), `batch_recompile` (#71), `set_instance_parameters` batch (#58)

### Theme 3 — Templates/Scaffolding (BP + Niagara)
Reusable patterns eliminate boilerplate.
- **BP:** `apply_template` (#33), `scaffold_interface_implementation` (#34)
- **Niagara:** `export_system_spec` (#89), `create_system_from_preset` (#134)

### Theme 4 — Silent Failures (all 3 domains)
Operations that report success but don't work are worse than errors.
- **Niagara:** LinearColor defaults (#2), move_module (#3), set_curve_value (#4)
- **Material:** set_material_property enum prefix (#7), connect_expressions to_input (#5)
- **BP:** Object pin defaults accepted then fail at compile (#32)

### Theme 5 — Error Quality
Bad error messages waste more time than missing features.
- **Niagara:** `add_module` fuzzy suggestions (#49)
- **Material:** Error messages with valid values (#65)
- **BP:** Compile errors linked to node IDs (#40)
