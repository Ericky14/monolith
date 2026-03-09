# Monolith API Reference

**Total Actions: 122** across 9 namespaces

> Auto-generated from action registration code. Each action is called via HTTP POST to `http://localhost:<port>` with JSON body `{ "namespace": "<ns>", "action": "<action>", "params": { ... } }`.

---

## Table of Contents

| Namespace | Actions | Description |
|-----------|---------|-------------|
| [monolith](#monolith) | 4 | Core server tools (discover, status, update, reindex) |
| [blueprint](#blueprint) | 6 | Blueprint graph introspection |
| [material](#material) | 14 | Material graph editing and inspection |
| [animation](#animation) | 23 | Animation montages, blend spaces, state machines, skeletons |
| [niagara](#niagara) | 41 | Niagara VFX system editing (emitters, modules, params, renderers) |
| [editor](#editor) | 13 | Live Coding builds, compile output capture, and editor log capture |
| [config](#config) | 6 | INI config file inspection and search |
| [project](#project) | 5 | Project-wide asset index (SQLite + FTS5) |
| [source](#source) | 10 | Unreal Engine C++ source code navigation |

---

## monolith

Core server management and introspection tools.

### `monolith.discover`

List available tool namespaces and their actions. Pass namespace to filter.

> **New in Wave 2:** `discover` now returns per-action param schemas for all 122 actions, so callers can see required/optional params without consulting docs.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `namespace` | string | optional | Filter to a specific namespace |

---

### `monolith.status`

Get Monolith server health: version, uptime, port, registered action count, module status.

*No parameters.*

---

### `monolith.update`

Check for or install Monolith updates from GitHub Releases.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `action` | string | optional | `"check"` to compare versions, `"install"` to download and stage update. Default: `"check"` |

---

### `monolith.reindex`

Trigger a full project re-index of the Monolith project database.

*No parameters.*

---

## blueprint

Blueprint graph introspection -- read-only access to graphs, nodes, pins, variables, and execution flow.

### `blueprint.list_graphs`

List all graphs in a Blueprint asset.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the Blueprint asset |

**Returns:** Array of graphs with name, type (event_graph/function/macro/delegate_signature), and node count.

---

### `blueprint.get_graph_summary`

Get a lightweight graph overview with node id/class/title and exec connections only. Much smaller payload than `get_graph_data` (~10KB vs ~172KB for complex graphs).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the Blueprint asset |
| `graph_name` | string | optional | Graph name. Defaults to first UbergraphPage |

**Returns:** Array of nodes (id, class, title) and exec-only connections. No pin details or positions.

---

### `blueprint.get_graph_data`

Get full graph data with all nodes, pins, and connections.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the Blueprint asset |
| `graph_name` | string | optional | Graph name. Defaults to first UbergraphPage |
| `node_class_filter` | string | optional | Filter nodes by class name (case-insensitive substring match) |

**Returns:** Full node list with IDs, classes, titles, positions, pin details, and connections.

---

### `blueprint.get_variables`

Get all variables defined in a Blueprint.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the Blueprint asset |

**Returns:** Array of variables with name, type, default value, category, and flags (instance_editable, blueprint_read_only, expose_on_spawn, replicated, transient).

---

### `blueprint.get_execution_flow`

Get linearized execution flow from an entry point.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the Blueprint asset |
| `entry_point` | string | **required** | Event name, function name, or node title to trace from |

**Returns:** Recursive flow tree with nodes, branches, and execution paths.

---

### `blueprint.search_nodes`

Search for nodes in a Blueprint by title or function name.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the Blueprint asset |
| `query` | string | **required** | Search string matched against node title, class, and function name |

**Returns:** Matching nodes with graph, type, node ID, class, title, and function name.

---

## material

Material graph editing and inspection tools.

### `material.get_all_expressions`

Get all expression nodes in a base material.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |

---

### `material.get_expression_details`

Get full property reflection, inputs, and outputs for a single expression.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |
| `expression_name` | string | **required** | Name of the expression node |

---

### `material.get_full_connection_graph`

Get the complete connection graph (all wires) of a material.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |

---

### `material.disconnect_expression`

Disconnect inputs or outputs on a named expression.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |
| `expression_name` | string | **required** | Name of the expression node |
| `input_name` | string | optional | Specific input to disconnect |
| `disconnect_outputs` | bool | optional | Also disconnect outputs. Default: `false` |

---

### `material.build_material_graph`

Build entire material graph from JSON spec in a single undo transaction.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |
| `graph_spec` | object/string | **required** | JSON spec with `nodes`, `custom_hlsl_nodes`, `connections`, `outputs` arrays |
| `clear_existing` | bool | optional | Clear existing graph before building. Default: `false` |

---

### `material.begin_transaction`

Begin a named undo transaction for batching edits.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `transaction_name` | string | **required** | Name for the undo transaction |

---

### `material.end_transaction`

End the current undo transaction.

*No parameters.*

---

### `material.export_material_graph`

Export complete material graph to JSON (round-trippable with build_material_graph).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |
| `include_properties` | bool | optional | Include node property details. Default: `true` |
| `include_positions` | bool | optional | Include node X/Y positions. Default: `true` |

---

### `material.import_material_graph`

Import material graph from JSON string. Mode: overwrite or merge.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |
| `graph_json` | string | **required** | JSON string of the graph to import |
| `mode` | string | optional | `"overwrite"` or `"merge"`. Default: `"overwrite"` |

---

### `material.validate_material`

Validate material graph health and optionally auto-fix issues.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |
| `fix_issues` | bool | optional | Auto-fix detected issues. Default: `false` |

---

### `material.render_preview`

Render material preview to PNG file.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |
| `resolution` | number | optional | Preview resolution in pixels. Default: `256` |

---

### `material.get_thumbnail`

Get material thumbnail as base64-encoded PNG.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |
| `resolution` | number | optional | Thumbnail resolution in pixels. Default: `256` |
| `save_to_file` | bool | optional | Save thumbnail to `Saved/Monolith/thumbnails/` instead of returning base64. Default: `false` |

---

### `material.create_custom_hlsl_node`

Create a Custom HLSL expression node with inputs, outputs, and code.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material asset |
| `code` | string | **required** | HLSL code for the custom node |
| `description` | string | optional | Node description |
| `output_type` | string | optional | Output type name |
| `pos_x` | number | optional | Node X position. Default: `0` |
| `pos_y` | number | optional | Node Y position. Default: `0` |
| `inputs` | array | optional | Array of `{ "name": "..." }` input definitions |
| `additional_outputs` | array | optional | Array of `{ "name": "...", "type": "..." }` output definitions |

---

### `material.get_layer_info`

Get Material Layer or Material Layer Blend info.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the material layer asset |

---

## animation

Animation asset editing -- montages, blend spaces, state machines, notifies, bone tracks, and skeletons.

### `animation.add_montage_section`

Add a section to an animation montage.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the montage asset |
| `section_name` | string | **required** | Name for the new section |
| `start_time` | number | **required** | Start time in seconds |

---

### `animation.delete_montage_section`

Delete a section from an animation montage by index.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the montage asset |
| `section_index` | number | **required** | Section index to delete |

---

### `animation.set_section_next`

Set the next section for a montage section.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the montage asset |
| `section_name` | string | **required** | Source section name |
| `next_section_name` | string | **required** | Target next section name |

---

### `animation.set_section_time`

Set the start time of a montage section.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the montage asset |
| `section_name` | string | **required** | Section name |
| `new_time` | number | **required** | New start time in seconds |

---

### `animation.add_blendspace_sample`

Add a sample to a blend space.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the blend space asset |
| `anim_path` | string | **required** | Package path of the animation sequence |
| `x` | number | **required** | X-axis sample position |
| `y` | number | **required** | Y-axis sample position |

---

### `animation.edit_blendspace_sample`

Edit a blend space sample position and optionally its animation.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the blend space asset |
| `sample_index` | number | **required** | Index of the sample to edit |
| `x` | number | **required** | New X-axis position |
| `y` | number | **required** | New Y-axis position |
| `anim_path` | string | optional | New animation sequence path |

---

### `animation.delete_blendspace_sample`

Delete a sample from a blend space by index.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the blend space asset |
| `sample_index` | number | **required** | Index of the sample to delete |

---

### `animation.get_state_machines`

Get all state machines in an animation blueprint.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the anim blueprint |

---

### `animation.get_state_info`

Get detailed info about a state in a state machine.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the anim blueprint |
| `machine_name` | string | **required** | State machine name |
| `state_name` | string | **required** | State name |

---

### `animation.get_transitions`

Get all transitions in a state machine.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the anim blueprint |
| `machine_name` | string | **required** | State machine name |

---

### `animation.get_blend_nodes`

Get blend nodes in an animation blueprint graph.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the anim blueprint |
| `graph_name` | string | optional | Specific graph name to search |

---

### `animation.get_linked_layers`

Get linked animation layers in an animation blueprint.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the anim blueprint |

---

### `animation.get_graphs`

Get all graphs in an animation blueprint.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the anim blueprint |

---

### `animation.get_nodes`

Get animation nodes with optional class and graph filters.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the anim blueprint |
| `node_class_filter` | string | optional | Filter by node class name |
| `graph_name` | string | optional | Filter to a specific graph by name |

---

### `animation.set_notify_time`

Set the trigger time of an animation notify.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the animation asset |
| `notify_index` | number | **required** | Index of the notify |
| `new_time` | number | **required** | New trigger time in seconds |

---

### `animation.set_notify_duration`

Set the duration of a state animation notify.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the animation asset |
| `notify_index` | number | **required** | Index of the notify |
| `new_duration` | number | **required** | New duration in seconds |

---

### `animation.set_bone_track_keys`

Set position, rotation, and scale keys on a bone track.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the animation sequence |
| `bone_name` | string | **required** | Name of the bone |
| `positions_json` | string | **required** | JSON array of position keys |
| `rotations_json` | string | **required** | JSON array of rotation keys |
| `scales_json` | string | **required** | JSON array of scale keys |

---

### `animation.add_bone_track`

Add a bone track to an animation sequence.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the animation sequence |
| `bone_name` | string | **required** | Name of the bone to add a track for |

---

### `animation.remove_bone_track`

Remove a bone track from an animation sequence.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the animation sequence |
| `bone_name` | string | **required** | Name of the bone track to remove |
| `include_children` | bool | optional | Also remove child bone tracks. Default: `false` |

---

### `animation.add_virtual_bone`

Add a virtual bone to a skeleton.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the skeleton asset |
| `source_bone` | string | **required** | Source bone name |
| `target_bone` | string | **required** | Target bone name |

---

### `animation.remove_virtual_bones`

Remove virtual bones from a skeleton.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the skeleton asset |
| `bone_names` | array | **required** | Array of virtual bone names to remove |

---

### `animation.get_skeleton_info`

Get skeleton bone hierarchy and virtual bones.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the skeleton asset |

---

### `animation.get_skeletal_mesh_info`

Get skeletal mesh info including morph targets, sockets, LODs, and materials.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the skeletal mesh asset |

---

## niagara

Niagara VFX system editing -- emitters, modules, parameters, renderers, and batch operations.

> **Note:** All Niagara actions accept `asset_path` (preferred) or `system_path` (backward compatible) for the system asset path parameter.

### Emitter Actions

#### `niagara.add_emitter`

Add an emitter to a Niagara system.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter_asset` | string | **required** | Package path of the emitter asset to add |
| `name` | string | optional | Custom name for the emitter handle |

---

#### `niagara.remove_emitter`

Remove an emitter from a Niagara system.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |

---

#### `niagara.duplicate_emitter`

Duplicate an emitter within a Niagara system.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `source_emitter` | string | **required** | Source emitter handle ID |
| `new_name` | string | optional | Name for the duplicated emitter |

---

#### `niagara.set_emitter_enabled`

Enable or disable an emitter.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `enabled` | bool | **required** | Enable state |

---

#### `niagara.reorder_emitters`

Reorder emitters in a system.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `order` | array | **required** | Array of emitter handle IDs in desired order |

---

#### `niagara.set_emitter_property`

Set an emitter property.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `property` | string | **required** | Property name |
| `value` | any | optional | Property value |

---

#### `niagara.request_compile`

Request compilation of a Niagara system.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |

---

#### `niagara.create_system`

Create a new Niagara system.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `save_path` | string | **required** | Package path to save the new system |
| `template` | string | optional | Template system asset path |

---

#### `niagara.list_emitters`

List all emitters in a Niagara system with summary info.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the Niagara system |

**Returns:** Array of emitters with name, index, enabled, sim_target, and renderer_count.

---

#### `niagara.list_renderers`

List all renderers across emitters in a Niagara system.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | optional | Filter to a specific emitter |

**Returns:** Array of renderers with emitter, renderer class, index, enabled, and material.

---

### Module Actions

#### `niagara.get_ordered_modules`

Get ordered modules in a script stage.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `usage` | string | **required** | Script usage (e.g. `"EmitterSpawnScript"`, `"ParticleUpdateScript"`) |

---

#### `niagara.get_module_inputs`

Get inputs for a module node.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `module_node` | string | **required** | Module node GUID |

---

#### `niagara.get_module_graph`

Get the node graph of a module script.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `script_path` | string | **required** | Package path of the Niagara script |

---

#### `niagara.add_module`

Add a module to a script stage.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `usage` | string | **required** | Script usage stage |
| `module_script` | string | **required** | Module script asset path |
| `index` | number | optional | Insertion index. `-1` to append |

---

#### `niagara.remove_module`

Remove a module from a script stage.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `module_node` | string | **required** | Module node GUID |

---

#### `niagara.move_module`

Move a module to a new index.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `module_node` | string | **required** | Module node GUID |
| `new_index` | number | **required** | Target index |

---

#### `niagara.set_module_enabled`

Enable or disable a module.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `module_node` | string | **required** | Module node GUID |
| `enabled` | bool | **required** | Enable state |

---

#### `niagara.set_module_input_value`

Set a module input value.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `module_node` | string | **required** | Module node GUID |
| `input` | string | **required** | Input parameter name |
| `value` | any | optional | Value (number, bool, string, vector `{x,y,z}`, color `{r,g,b,a}`) |

---

#### `niagara.set_module_input_binding`

Bind a module input to a parameter.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `module_node` | string | **required** | Module node GUID |
| `input` | string | **required** | Input parameter name |
| `binding` | string | **required** | Parameter path to bind to |

---

#### `niagara.set_module_input_di`

Set a data interface on a module input.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `module_node` | string | **required** | Module node GUID |
| `input` | string | **required** | Input parameter name |
| `di_class` | string | **required** | Data interface class name |
| `config` | string | optional | JSON configuration for DI properties |

---

#### `niagara.create_module_from_hlsl`

Create a Niagara module from HLSL.

*Delegates to Python bridge.*

---

#### `niagara.create_function_from_hlsl`

Create a Niagara function from HLSL.

*Delegates to Python bridge.*

---

### Parameter Actions

#### `niagara.get_all_parameters`

Get all parameters in a system.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |

---

#### `niagara.get_user_parameters`

Get user-exposed parameters.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |

---

#### `niagara.get_parameter_value`

Get a parameter value.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `parameter` | string | **required** | Parameter name (with or without `"User."` prefix) |

---

#### `niagara.get_parameter_type`

Get info about a Niagara type.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `type` | string | **required** | Niagara type name (e.g. `"float"`, `"FVector3f"`) |

---

#### `niagara.trace_parameter_binding`

Trace where a parameter is used.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `parameter` | string | **required** | Parameter name |

---

#### `niagara.add_user_parameter`

Add a user parameter.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `name` | string | **required** | Parameter name |
| `type` | string | **required** | Niagara type |
| `default` | any | optional | Default value |

---

#### `niagara.remove_user_parameter`

Remove a user parameter.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `name` | string | **required** | Parameter name (with or without `"User."` prefix) |

---

#### `niagara.set_parameter_default`

Set a parameter default value.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `parameter` | string | **required** | Parameter name (with or without `"User."` prefix) |
| `value` | any | optional | New default value |

---

#### `niagara.set_curve_value`

Set curve keys on a module input.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `module` | string | **required** | Module name |
| `input` | string | **required** | Input name |
| `keys` | string | **required** | JSON array of curve keys (`{ "time", "value", "arrive_tangent"?, "leave_tangent"? }`) |

---

### Renderer Actions

#### `niagara.add_renderer`

Add a renderer to an emitter.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `class` | string | **required** | Renderer class (`"Sprite"`, `"Mesh"`, `"Ribbon"`, `"Light"`, `"Component"`) |

---

#### `niagara.remove_renderer`

Remove a renderer from an emitter.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `renderer_index` | number | **required** | Renderer index |

---

#### `niagara.set_renderer_material`

Set renderer material.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `renderer_index` | number | **required** | Renderer index |
| `material` | string | **required** | Material asset path |

---

#### `niagara.set_renderer_property`

Set a renderer property.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `renderer_index` | number | **required** | Renderer index |
| `property` | string | **required** | Property name |
| `value` | any | optional | Property value |

---

#### `niagara.get_renderer_bindings`

Get renderer attribute bindings.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `renderer_index` | number | **required** | Renderer index |

---

#### `niagara.set_renderer_binding`

Set a renderer attribute binding.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID |
| `renderer_index` | number | **required** | Renderer index |
| `binding_name` | string | **required** | Binding property name |
| `attribute` | string | **required** | Attribute path to bind |

---

### Batch & Utility Actions

#### `niagara.batch_execute`

Execute multiple operations in one transaction.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `operations` | string | **required** | JSON array of operation objects, each with `"op"` field and action-specific params |

---

#### `niagara.create_system_from_spec`

Create a full system from JSON spec.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `spec` | string | **required** | JSON specification with `save_path`, optional `template`, `user_parameters`, `emitters` |

---

#### `niagara.get_di_functions`

Get data interface function signatures.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `di_class` | string | **required** | Data interface class name |

---

#### `niagara.get_compiled_gpu_hlsl`

Get compiled GPU HLSL for an emitter.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `system_path` | string | **required** | Package path of the Niagara system |
| `emitter` | string | **required** | Emitter handle ID (must be a GPU emitter) |

---

## editor

Live Coding build management and editor log capture.

### `editor.trigger_build`

Trigger a Live Coding compile.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `wait` | bool | optional | Block until compile finishes |

---

### `editor.live_compile`

Trigger a Live Coding compile (alias for trigger_build).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `wait` | bool | optional | Block until compile finishes |

---

### `editor.get_build_errors`

Get build errors and warnings from the last compile.

*No parameters.*

---

### `editor.get_build_status`

Check if a build is currently in progress.

*No parameters.*

---

### `editor.get_build_summary`

Get summary of last build (errors, warnings, time).

*No parameters.*

---

### `editor.search_build_output`

Search build log output by pattern.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `pattern` | string | **required** | Search pattern |
| `limit` | number | optional | Maximum results |

---

### `editor.get_recent_logs`

Get recent editor log entries.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `count` | number | optional | Number of entries to return |

---

### `editor.search_logs`

Search log entries by category, verbosity, and text pattern.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `pattern` | string | **required** | Text pattern to search |
| `category` | string | **required** | Log category filter |
| `verbosity` | string | **required** | Max verbosity level (`"fatal"`, `"error"`, `"warning"`, `"log"`, `"verbose"`) |
| `limit` | number | optional | Maximum results |

---

### `editor.tail_log`

Get last N log lines.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `count` | number | optional | Number of lines to return |

---

### `editor.get_log_categories`

List active log categories.

*No parameters.*

---

### `editor.get_log_stats`

Get log statistics by verbosity level.

*No parameters.*

---

### `editor.get_compile_output`

Get structured compile report: result, time, log lines from compile categories (LogLiveCoding, LogCompile, LogLinker), error/warning counts, patch status. Time-windowed to the last compile event via OnPatchComplete delegate.

*No parameters.*

**Returns:** `last_result`, `last_compile_time`, `last_compile_end_time`, `patch_applied`, `compiling`, `error_count`, `warning_count`, `log_line_count`, `compile_log` (array of log entries).

---

### `editor.get_crash_context`

Get last crash/ensure context information.

*No parameters.*

---

## config

INI config file inspection and search across the full Unreal Engine config hierarchy.

### `config.resolve_setting`

Get effective value of a config key across the full INI hierarchy.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `file` | string | **required** | Config category (e.g. `"Engine"`, `"Game"`, `"Input"`) |
| `section` | string | **required** | INI section (e.g. `"/Script/Engine.GarbageCollectionSettings"`) |
| `key` | string | **required** | Config key name |

---

### `config.explain_setting`

Show where a config value comes from across Base->Default->User layers.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `file` | string | **required** | Config category |
| `section` | string | **required** | INI section |
| `key` | string | **required** | Config key name |
| `setting` | string | optional | Shortcut: search for this key across common categories (instead of file/section/key) |

---

### `config.diff_from_default`

Show project config overrides vs engine defaults for a category.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `file` | string | **required** | Config category |
| `section` | string | optional | Filter to a specific section |

---

### `config.search_config`

Full-text search across all config files.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `query` | string | **required** | Search text |
| `file` | string | optional | Filter to a specific config category |

---

### `config.get_section`

Read an entire config section from a specific file.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `file` | string | **required** | Config file short name (e.g. `"DefaultEngine"`, `"BaseEngine"`) |
| `section` | string | **required** | INI section name |

---

### `config.get_config_files`

List all config files with their hierarchy level.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `category` | string | optional | Filter to a specific config category |

---

## project

Project-wide asset index powered by SQLite with FTS5 full-text search. Requires `bEnableIndex` in Monolith settings.

### `project.search`

Full-text search across all indexed project assets, nodes, variables, and parameters.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `query` | string | **required** | FTS5 search query (supports `AND`, `OR`, `NOT`, `prefix*`) |
| `limit` | number | optional | Maximum results. Default: `50` |

---

### `project.find_references`

Find all assets that reference or are referenced by the given asset.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the asset (e.g. `/Game/Characters/BP_Hero`) |

---

### `project.find_by_type`

Find all assets of a given type (e.g. Blueprint, Material, StaticMesh).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_type` | string | **required** | Asset class name (e.g. `Blueprint`, `Material`, `StaticMesh`, `Texture2D`) |
| `limit` | number | optional | Maximum results. Default: `100` |
| `offset` | number | optional | Pagination offset. Default: `0` |

---

### `project.get_stats`

Get project index statistics -- total counts by table and asset class breakdown.

*No parameters.*

---

### `project.get_asset_details`

Get deep details for a specific asset -- nodes, variables, parameters, dependencies.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | **required** | Package path of the asset (e.g. `/Game/Characters/BP_Hero`) |

---

## source

Unreal Engine C++ source code navigation powered by a pre-built SQLite index.

### `source.read_source`

Get the implementation source code for a class, function, or struct.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `symbol` | string | **required** | Symbol name (class, function, or struct) |
| `include_header` | bool | optional | Include header declaration. Default: `true` |
| `max_lines` | number | optional | Maximum lines to return. Default: `0` (unlimited) |
| `members_only` | bool | optional | Show only member signatures (skip function bodies). Default: `false` |

---

### `source.find_references`

Find all usage sites of a symbol (calls, includes, type references).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `symbol` | string | **required** | Symbol name |
| `ref_kind` | string | optional | Filter by reference kind |
| `limit` | number | optional | Maximum results. Default: `50` |

---

### `source.find_callers`

Find all functions that call the given function.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `symbol` | string | **required** | Function name |
| `limit` | number | optional | Maximum results. Default: `50` |

---

### `source.find_callees`

Find all functions called by the given function.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `symbol` | string | **required** | Function name |
| `limit` | number | optional | Maximum results. Default: `50` |

---

### `source.search_source`

Full-text search across Unreal Engine source code and shaders.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `query` | string | **required** | Search query |
| `scope` | string | optional | Search scope. Default: `"all"` |
| `limit` | number | optional | Maximum results. Default: `20` |
| `mode` | string | optional | Search mode (`"fts"` or `"regex"`). Default: `"fts"` |
| `module` | string | optional | Filter to a specific module |
| `path_filter` | string | optional | Filter by file path substring |
| `symbol_kind` | string | optional | Filter by symbol kind |

---

### `source.get_class_hierarchy`

Show the inheritance tree for a class.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `class_name` | string | **required** | Class name |
| `direction` | string | optional | `"up"`, `"down"`, or `"both"`. Default: `"both"` |
| `depth` | number | optional | Hierarchy depth. Default: `1` |

---

### `source.get_module_info`

Get module statistics: file count, symbol counts by kind, and key classes.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `module_name` | string | **required** | Module name |

---

### `source.get_symbol_context`

Get a symbol definition with surrounding context lines.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `symbol` | string | **required** | Symbol name |
| `context_lines` | number | optional | Number of context lines. Default: `20` |

---

### `source.read_file`

Read source lines from a file by path.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `file_path` | string | **required** | File path (relative to engine or absolute) |
| `start_line` | number | optional | Start line. Default: `1` |
| `end_line` | number | optional | End line. Default: `0` (end of file) |

---

### `source.trigger_reindex`

Trigger Python indexer to rebuild the engine source DB.

*No parameters.*
