---
name: unreal-ui
description: Use when working with Unreal Engine UI via Monolith MCP — creating widget blueprints, building HUDs, menus, settings panels, styling, animations, data binding, save systems, and accessibility. Triggers on UI, HUD, widget, menu, settings, save game, accessibility, font, anchor, toast, dialog, loading screen.
---

# Unreal UI Workflows

**42 UI actions** via `ui_query()`. Discover with `monolith_discover({ namespace: "ui" })`.

## Key Parameters

- `asset_path` -- Widget Blueprint path (e.g. `/Game/UI/WBP_MyWidget`)
- `save_path` -- destination for new WBP assets
- `widget_name` / `widget_class` -- widget name in tree / type (`TextBlock`, `Image`, `Button`, etc.)
- `parent_name` -- parent panel (omit for root)
- `anchor_preset` -- `center`, `top_left`, `stretch_fill`, etc.

## Action Reference

| Action | Key Params | Purpose |
|--------|-----------|---------|
| **Widget CRUD (7)** | | |
| `create_widget_blueprint` | `save_path`, `parent_class`?, `root_widget`? | Create new WBP |
| `get_widget_tree` | `asset_path` | Full hierarchy as JSON |
| `add_widget` | `asset_path`, `widget_class`, `widget_name`?, `parent_name`?, `anchor_preset`?, `position`?, `size`? | Add widget to panel |
| `remove_widget` | `asset_path`, `widget_name` | Remove from tree |
| `set_widget_property` | `asset_path`, `widget_name`, `property_name`, `value` | Set any UPROPERTY |
| `compile_widget` | `asset_path` | Compile WBP |
| `list_widget_types` | `filter`? | Available widget classes |
| **Slot & Layout (3)** | | |
| `set_slot_property` | `asset_path`, `widget_name`, `anchors`?, `offsets`?, `position`?, `size`?, `z_order`?, `padding`? | Any slot property |
| `set_anchor_preset` | `asset_path`, `widget_name`, `preset` | Named anchor preset |
| `move_widget` | `asset_path`, `widget_name`, `new_parent_name` | Reparent widget |
| **Templates (8)** | | |
| `create_hud_element` | `asset_path`, `element_type` | crosshair, health_bar, ammo_counter, stamina_bar, interaction_prompt, damage_indicator, compass, subtitles, flashlight_battery |
| `create_menu` | `save_path`, `menu_type`, `buttons`? | main_menu, pause_menu, death_screen, credits |
| `create_settings_panel` | `save_path`, `tabs`? | Tabbed settings (graphics, audio, controls, gameplay, accessibility) |
| `create_dialog` | `save_path`, `title`?, `body`?, `confirm_text`?, `cancel_text`? | Confirmation dialog |
| `create_notification_toast` | `save_path`, `position`? | Toast widget |
| `create_loading_screen` | `save_path`, `show_progress`?, `show_tips`?, `show_spinner`? | Loading screen |
| `create_inventory_grid` | `save_path`, `columns`?, `rows`?, `slot_size`? | Inventory grid |
| `create_save_slot_list` | `save_path`, `max_slots`? | Save slot selector |
| **Styling (6)** | | |
| `set_brush` | `asset_path`, `widget_name`, `property_name`, `draw_type`?, `tint_color`?, `corner_radius`?, `texture_path`? | Configure brush |
| `set_font` | `asset_path`, `widget_name`, `font_size`?, `typeface`?, `outline_size`?, `outline_color`? | Font on text widgets |
| `set_color_scheme` | `colors` | EStyleColor User1-16 palette |
| `batch_style` | `asset_path`, `widget_class`, `property_name`, `value` | Apply to all widgets of class |
| `set_text` | `asset_path`, `widget_name`, `text`, `text_color`?, `font_size`?, `justification`? | Convenience text setter |
| `set_image` | `asset_path`, `widget_name`, `texture_path`?, `material_path`?, `tint_color`?, `size`? | Convenience image setter |
| **Animation (5)** | | |
| `list_animations` | `asset_path` | List UWidgetAnimations |
| `get_animation_details` | `asset_path`, `animation_name` | Tracks, sections, keyframes |
| `create_animation` | `asset_path`, `animation_name`, `duration`, `tracks` | Create with keyframed tracks |
| `add_animation_keyframe` | `asset_path`, `animation_name`, `widget_name`, `property`, `time`, `value` | Add keyframe |
| `remove_animation` | `asset_path`, `animation_name` | Remove animation |
| **Data Binding (4)** | | |
| `list_widget_events` | `asset_path`, `widget_name`? | Bindable events |
| `list_widget_properties` | `asset_path`, `widget_name` | Settable properties with types |
| `setup_list_view` | `asset_path`, `list_widget_name`, `entry_widget_class`?, `entry_height`? | Configure ListView/TileView |
| `get_widget_bindings` | `asset_path` | All property bindings |
| **Settings & Save Scaffolding (5)** | | |
| `scaffold_game_user_settings` | `class_name`, `module_name`, `features`? | UGameUserSettings subclass C++ |
| `scaffold_save_game` | `class_name`, `module_name`, `properties`? | ULocalPlayerSaveGame subclass C++ |
| `scaffold_save_subsystem` | `class_name`, `module_name`, `save_game_class` | Save management subsystem C++ |
| `scaffold_audio_settings` | `categories`? | Audio settings wiring info |
| `scaffold_input_remapping` | `actions`? | Keybinding remapping setup |
| **Accessibility (4)** | | |
| `scaffold_accessibility_subsystem` | `class_name`, `module_name` | Accessibility settings subsystem C++ |
| `audit_accessibility` | `asset_path` | Audit font size, focus, navigation, tooltips |
| `set_colorblind_mode` | `mode`, `severity`?, `correct`? | Colorblind correction (runtime) |
| `set_text_scale` | `scale` | UI text scale (runtime) |

## Anchor Presets

`top_left`(0,0,0,0) `top_center`(0.5,0,0.5,0) `top_right`(1,0,1,0) `center_left`(0,0.5,0,0.5) `center`(0.5,0.5,0.5,0.5) `center_right`(1,0.5,1,0.5) `bottom_left`(0,1,0,1) `bottom_center`(0.5,1,0.5,1) `bottom_right`(1,1,1,1) `stretch_fill`(0,0,1,1) `stretch_horizontal`(0,0.5,1,0.5) `stretch_vertical`(0.5,0,0.5,1)

## Typical Workflow: Build a HUD

```
1. ui_query("create_widget_blueprint", {"save_path": "/Game/UI/WBP_GameHUD"})
2. ui_query("create_hud_element", {"asset_path": "/Game/UI/WBP_GameHUD", "element_type": "health_bar"})
3. ui_query("create_hud_element", {"asset_path": "/Game/UI/WBP_GameHUD", "element_type": "crosshair"})
4. ui_query("set_font", {"asset_path": "/Game/UI/WBP_GameHUD", "widget_name": "ammo_counter_Current", "font_size": 28, "typeface": "Bold"})
```

## Horror UI + Accessibility Guidelines

**Horror:**
- No health bar -- use vignette + desaturation + heartbeat audio via post-process
- Minimal HUD -- auto-hide when full (stamina, etc.)
- Subtitles ON by default -- caption ALL sounds
- Diegetic where possible -- flashlight battery on prop, not overlay
- No minimap -- navigation uncertainty = horror
- Short interaction trace -- must get close

**Accessibility (critical -- hospice patients):**
- **Atkinson Hyperlegible** font at `Content/UI/Fonts/Atkinson/`
- Minimum: 18pt body, 22pt subtitles
- Always offer: text scale, colorblind mode, reduced motion, hold-vs-toggle
- Focus indicators must be visually distinct (not just color)
- Run `audit_accessibility` on every WBP before shipping
