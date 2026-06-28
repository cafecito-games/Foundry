/**************************************************************************/
/*  register_editor_types.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "register_editor_types.h"

#include "core/object/script_language.h"
#include "editor/animation/animation_tree_editor_plugin.h"
#include "editor/audio/audio_stream_editor_plugin.h"
#include "editor/audio/audio_stream_randomizer_editor_plugin.h"
#include "editor/debugger/debug_adapter/debug_adapter_server.h"
#include "editor/debugger/editor_debugger_plugin.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_platform_apple_embedded.h"
#include "editor/export/editor_export_platform_extension.h"
#include "editor/export/editor_export_platform_pc.h"
#include "editor/export/editor_export_plugin.h"
#include "editor/export/register_exporters.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_spin_slider.h"
#include "editor/gui/editor_toaster.h"
#include "editor/import/3d/resource_importer_obj.h"
#include "editor/import/3d/resource_importer_scene.h"
#include "editor/import/editor_import_plugin.h"
#include "editor/import/resource_importer_bitmask.h"
#include "editor/import/resource_importer_bmfont.h"
#include "editor/import/resource_importer_csv_translation.h"
#include "editor/import/resource_importer_dynamic_font.h"
#include "editor/import/resource_importer_image.h"
#include "editor/import/resource_importer_imagefont.h"
#include "editor/import/resource_importer_layered_texture.h"
#include "editor/import/resource_importer_shader_file.h"
#include "editor/import/resource_importer_svg.h"
#include "editor/import/resource_importer_texture.h"
#include "editor/import/resource_importer_texture_atlas.h"
#include "editor/import/resource_importer_wav.h"
#include "editor/inspector/editor_context_menu_plugin.h"
#include "editor/inspector/editor_resource_picker.h"
#include "editor/inspector/editor_resource_preview.h"
#include "editor/inspector/editor_resource_tooltip_plugins.h"
#include "editor/inspector/input_event_editor_plugin.h"
#include "editor/inspector/sub_viewport_preview_editor_plugin.h"
#include "editor/inspector/tool_button_editor_plugin.h"
#include "editor/scene/2d/camera_2d_editor_plugin.h"
#include "editor/scene/2d/light_occluder_2d_editor_plugin.h"
#include "editor/scene/2d/line_2d_editor_plugin.h"
#include "editor/scene/2d/particles_2d_editor_plugin.h"
#include "editor/scene/2d/path_2d_editor_plugin.h"
#include "editor/scene/2d/physics/cast_2d_editor_plugin.h"
#include "editor/scene/2d/physics/collision_polygon_2d_editor_plugin.h"
#include "editor/scene/2d/physics/collision_shape_2d_editor_plugin.h"
#include "editor/scene/2d/polygon_2d_editor_plugin.h"
#include "editor/scene/2d/skeleton_2d_editor_plugin.h"
#include "editor/scene/2d/sprite_2d_editor_plugin.h"
#include "editor/scene/2d/tiles/tiles_editor_plugin.h"
#include "editor/scene/3d/bone_map_editor_plugin.h"
#include "editor/scene/3d/camera_3d_editor_plugin.h"
#include "editor/scene/3d/gpu_particles_collision_sdf_editor_plugin.h"
#include "editor/scene/3d/lightmap_gi_editor_plugin.h"
#include "editor/scene/3d/mesh_editor_plugin.h"
#include "editor/scene/3d/mesh_instance_3d_editor_plugin.h"
#include "editor/scene/3d/mesh_library_editor_plugin.h"
#include "editor/scene/3d/multimesh_editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_gizmos.h"
#include "editor/scene/3d/occluder_instance_3d_editor_plugin.h"
#include "editor/scene/3d/particles_3d_editor_plugin.h"
#include "editor/scene/3d/path_3d_editor_plugin.h"
#include "editor/scene/3d/physics/physical_bone_3d_editor_plugin.h"
#include "editor/scene/3d/polygon_3d_editor_plugin.h"
#include "editor/scene/3d/skeleton_3d_editor_plugin.h"
#include "editor/scene/3d/voxel_gi_editor_plugin.h"
#include "editor/scene/curve_editor_plugin.h"
#include "editor/scene/gradient_editor_plugin.h"
#include "editor/scene/gui/control_editor_plugin.h"
#include "editor/scene/gui/font_config_plugin.h"
#include "editor/scene/gui/margin_container_editor_plugin.h"
#include "editor/scene/gui/style_box_editor_plugin.h"
#include "editor/scene/gui/theme_editor_plugin.h"
#include "editor/scene/material_editor_plugin.h"
#include "editor/scene/packed_scene_editor_plugin.h"
#include "editor/scene/resource_preloader_editor_plugin.h"
#include "editor/scene/sprite_frames_editor_plugin.h"
#include "editor/scene/texture/bit_map_editor_plugin.h"
#include "editor/scene/texture/gradient_texture_2d_editor_plugin.h"
#include "editor/scene/texture/texture_3d_editor_plugin.h"
#include "editor/scene/texture/texture_editor_plugin.h"
#include "editor/scene/texture/texture_layered_editor_plugin.h"
#include "editor/scene/texture/texture_region_editor_plugin.h"
#include "editor/script/editor_script.h"
#include "editor/script/editor_script_plugin.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_feature_profile.h"
#include "editor/settings/editor_settings.h"
#include "editor/shader/shader_editor_plugin.h"
#include "editor/shader/shader_file_editor_plugin.h"
#include "editor/translations/editor_translation_parser.h"
#include "editor/version_control/editor_vcs_interface.h"
#ifndef DISABLE_DEPRECATED
#include "editor/scene/2d/parallax_background_editor_plugin.h"
#include "editor/scene/3d/skeleton_ik_3d_editor_plugin.h"
#endif

void register_editor_types() {
	OS::get_singleton()->benchmark_begin_measure("Editor", "Register Types");

	ResourceLoader::set_timestamp_on_load(true);
	ResourceSaver::set_timestamp_on_save(true);

	EditorStringNames::create();

	FOUNDRY_REGISTER_CLASS(EditorPaths);
	FOUNDRY_REGISTER_CLASS(EditorPlugin);
	FOUNDRY_REGISTER_CLASS(EditorTranslationParserPlugin);
	FOUNDRY_REGISTER_CLASS(EditorImportPlugin);
	FOUNDRY_REGISTER_CLASS(EditorScript);
	FOUNDRY_REGISTER_CLASS(EditorDock);
	FOUNDRY_REGISTER_CLASS(EditorSelection);
	FOUNDRY_REGISTER_CLASS(EditorFileDialog);
	FOUNDRY_REGISTER_CLASS(EditorSettings);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(EditorToaster);
	FOUNDRY_REGISTER_CLASS(EditorNode3DGizmo);
	FOUNDRY_REGISTER_CLASS(EditorNode3DGizmoPlugin);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(EditorResourcePreview);
	FOUNDRY_REGISTER_CLASS(EditorResourcePreviewGenerator);
	FOUNDRY_REGISTER_CLASS(EditorResourceTooltipPlugin);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(EditorFileSystem);
	FOUNDRY_REGISTER_CLASS(EditorFileSystemDirectory);
	FOUNDRY_REGISTER_CLASS(EditorVCSInterface);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(ScriptEditor);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(ScriptEditorBase);
	FOUNDRY_REGISTER_CLASS(EditorSyntaxHighlighter);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(EditorInterface);
	FOUNDRY_REGISTER_CLASS(EditorExportPlugin);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(EditorExportPlatform);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(EditorExportPlatformPC);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(EditorExportPlatformAppleEmbedded);
	FOUNDRY_REGISTER_CLASS(EditorExportPlatformExtension);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(EditorExportPreset);

	register_exporter_types();

	FOUNDRY_REGISTER_CLASS(EditorResourceConversionPlugin);
	FOUNDRY_REGISTER_CLASS(EditorSceneFormatImporter);
	FOUNDRY_REGISTER_CLASS(EditorScenePostImportPlugin);
	FOUNDRY_REGISTER_CLASS(EditorInspector);
	FOUNDRY_REGISTER_CLASS(EditorInspectorPlugin);
	FOUNDRY_REGISTER_CLASS(EditorProperty);
	FOUNDRY_REGISTER_CLASS(ScriptCreateDialog);
	FOUNDRY_REGISTER_CLASS(EditorFeatureProfile);
	FOUNDRY_REGISTER_CLASS(EditorSpinSlider);
	FOUNDRY_REGISTER_CLASS(EditorResourcePicker);
	FOUNDRY_REGISTER_CLASS(EditorScriptPicker);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(EditorUndoRedoManager);
	FOUNDRY_REGISTER_CLASS(EditorContextMenuPlugin);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(FileSystemDock);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(EditorFileSystemImportFormatSupportQuery);

	FOUNDRY_REGISTER_CLASS(EditorScenePostImport);
	FOUNDRY_REGISTER_CLASS(EditorCommandPalette);
	FOUNDRY_REGISTER_CLASS(EditorDebuggerPlugin);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(EditorDebuggerSession);

	// Required to document import options in the class reference.
	FOUNDRY_REGISTER_CLASS(ResourceImporterBitMap);
	FOUNDRY_REGISTER_CLASS(ResourceImporterBMFont);
	FOUNDRY_REGISTER_CLASS(ResourceImporterCSVTranslation);
	FOUNDRY_REGISTER_CLASS(ResourceImporterDynamicFont);
	FOUNDRY_REGISTER_CLASS(ResourceImporterImage);
	FOUNDRY_REGISTER_CLASS(ResourceImporterImageFont);
	FOUNDRY_REGISTER_CLASS(ResourceImporterSVG);
	FOUNDRY_REGISTER_CLASS(ResourceImporterLayeredTexture);
	FOUNDRY_REGISTER_CLASS(ResourceImporterOBJ);
	FOUNDRY_REGISTER_CLASS(ResourceImporterScene);
	FOUNDRY_REGISTER_CLASS(ResourceImporterShaderFile);
	FOUNDRY_REGISTER_CLASS(ResourceImporterTexture);
	FOUNDRY_REGISTER_CLASS(ResourceImporterTextureAtlas);
	FOUNDRY_REGISTER_CLASS(ResourceImporterWAV);

	// This list is alphabetized, and plugins that depend on Node2D are in their own section below.
	EditorPlugins::add_by_type<AnimationTreeEditorPlugin>();
	EditorPlugins::add_by_type<AudioStreamEditorPlugin>();
	EditorPlugins::add_by_type<AudioStreamRandomizerEditorPlugin>();
	EditorPlugins::add_by_type<BitMapEditorPlugin>();
	EditorPlugins::add_by_type<BoneMapEditorPlugin>();
	EditorPlugins::add_by_type<Camera3DEditorPlugin>();
	EditorPlugins::add_by_type<ControlEditorPlugin>();
	EditorPlugins::add_by_type<CPUParticles3DEditorPlugin>();
	EditorPlugins::add_by_type<CurveEditorPlugin>();
	if (!Engine::get_singleton()->is_recovery_mode_hint()) {
		EditorPlugins::add_by_type<DebugAdapterServer>();
	}
	EditorPlugins::add_by_type<EditorScriptPlugin>();
	EditorPlugins::add_by_type<FontEditorPlugin>();
	EditorPlugins::add_by_type<GPUParticles3DEditorPlugin>();
	EditorPlugins::add_by_type<GPUParticlesCollisionSDF3DEditorPlugin>();
	EditorPlugins::add_by_type<GradientEditorPlugin>();
	EditorPlugins::add_by_type<GradientTexture2DEditorPlugin>();
	EditorPlugins::add_by_type<InputEventEditorPlugin>();
	EditorPlugins::add_by_type<LightmapGIEditorPlugin>();
	EditorPlugins::add_by_type<MarginContainerEditorPlugin>();
	EditorPlugins::add_by_type<MaterialEditorPlugin>();
	EditorPlugins::add_by_type<MeshEditorPlugin>();
	EditorPlugins::add_by_type<MeshInstance3DEditorPlugin>();
	EditorPlugins::add_by_type<MeshLibraryEditorPlugin>();
	EditorPlugins::add_by_type<MultiMeshEditorPlugin>();
	EditorPlugins::add_by_type<OccluderInstance3DEditorPlugin>();
	EditorPlugins::add_by_type<PackedSceneEditorPlugin>();
	EditorPlugins::add_by_type<Path3DEditorPlugin>();
	EditorPlugins::add_by_type<PhysicalBone3DEditorPlugin>();
	EditorPlugins::add_by_type<Polygon3DEditorPlugin>();
	EditorPlugins::add_by_type<ResourcePreloaderEditorPlugin>();
	EditorPlugins::add_by_type<ShaderEditorPlugin>();
	EditorPlugins::add_by_type<ShaderFileEditorPlugin>();
	EditorPlugins::add_by_type<Skeleton3DEditorPlugin>();
	EditorPlugins::add_by_type<SpriteFramesEditorPlugin>();
	EditorPlugins::add_by_type<StyleBoxEditorPlugin>();
	EditorPlugins::add_by_type<SubViewportPreviewEditorPlugin>();
	EditorPlugins::add_by_type<Texture3DEditorPlugin>();
	EditorPlugins::add_by_type<TextureEditorPlugin>();
	EditorPlugins::add_by_type<TextureLayeredEditorPlugin>();
	EditorPlugins::add_by_type<TextureRegionEditorPlugin>();
	EditorPlugins::add_by_type<ThemeEditorPlugin>();
	EditorPlugins::add_by_type<ToolButtonEditorPlugin>();
	EditorPlugins::add_by_type<VoxelGIEditorPlugin>();
#ifndef DISABLE_DEPRECATED
	EditorPlugins::add_by_type<SkeletonIK3DEditorPlugin>();
#endif

	// 2D
	EditorPlugins::add_by_type<Camera2DEditorPlugin>();
	EditorPlugins::add_by_type<CollisionPolygon2DEditorPlugin>();
	EditorPlugins::add_by_type<CollisionShape2DEditorPlugin>();
	EditorPlugins::add_by_type<CPUParticles2DEditorPlugin>();
	EditorPlugins::add_by_type<GPUParticles2DEditorPlugin>();
	EditorPlugins::add_by_type<LightOccluder2DEditorPlugin>();
	EditorPlugins::add_by_type<Line2DEditorPlugin>();
	EditorPlugins::add_by_type<Path2DEditorPlugin>();
	EditorPlugins::add_by_type<Polygon2DEditorPlugin>();
	EditorPlugins::add_by_type<Cast2DEditorPlugin>();
	EditorPlugins::add_by_type<Skeleton2DEditorPlugin>();
	EditorPlugins::add_by_type<Sprite2DEditorPlugin>();
	EditorPlugins::add_by_type<TileSetEditorPlugin>();
	EditorPlugins::add_by_type<TileMapEditorPlugin>();
#ifndef DISABLE_DEPRECATED
	EditorPlugins::add_by_type<ParallaxBackgroundEditorPlugin>();
#endif

	// For correct doc generation.
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "editor/run/main_run_args", PROPERTY_HINT_NONE, "monospace"), "");

	GLOBAL_DEF(PropertyInfo(Variant::STRING, "editor/script/templates_search_path", PROPERTY_HINT_DIR), "res://script_templates");

	GLOBAL_DEF("editor/naming/default_signal_callback_name", "_on_{node_name}_{signal_name}");
	GLOBAL_DEF("editor/naming/default_signal_callback_to_self_name", "_on_{signal_name}");
	GLOBAL_DEF(PropertyInfo(Variant::INT, "editor/naming/scene_name_casing", PROPERTY_HINT_ENUM, "Auto,PascalCase,snake_case,kebab-case,camelCase"), EditorNode::SCENE_NAME_CASING_SNAKE_CASE);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "editor/naming/script_name_casing", PROPERTY_HINT_ENUM, "Auto,PascalCase,snake_case,kebab-case,camelCase"), ScriptLanguage::SCRIPT_NAME_CASING_AUTO);

	GLOBAL_DEF("editor/import/reimport_missing_imported_files", true);
	GLOBAL_DEF("editor/import/use_multiple_threads", true);

	GLOBAL_DEF(PropertyInfo(Variant::INT, "editor/import/atlas_max_width", PROPERTY_HINT_RANGE, "128,8192,1,or_greater"), 2048);

	GLOBAL_DEF("editor/export/convert_text_resources_to_binary", true);

	GLOBAL_DEF("editor/version_control/plugin_name", "");
	GLOBAL_DEF("editor/version_control/autoload_on_startup", false);

	EditorInterface::create();
	Engine::Singleton ei_singleton = Engine::Singleton("EditorInterface", EditorInterface::get_singleton());
	ei_singleton.editor_only = true;
	Engine::get_singleton()->add_singleton(ei_singleton);

	if (RenderingServer::get_singleton()) {
		// RenderingServer needs to exist for this to succeed.
		Texture3DEditor::init_shaders();
		TextureLayeredEditor::init_shaders();
		TexturePreview::init_shaders();
	}

	// Required as FoundryExtensions can register docs at init time way before this
	// class is actually instantiated.
	EditorHelp::init_gdext_pointers();

	OS::get_singleton()->benchmark_end_measure("Editor", "Register Types");
}

void unregister_editor_types() {
	OS::get_singleton()->benchmark_begin_measure("Editor", "Unregister Types");

	Texture3DEditor::finish_shaders();
	TextureLayeredEditor::finish_shaders();
	TexturePreview::finish_shaders();

	EditorNode::cleanup();
	EditorInterface::free();

	if (EditorPaths::get_singleton()) {
		EditorPaths::free();
	}
	EditorStringNames::free();

	OS::get_singleton()->benchmark_end_measure("Editor", "Unregister Types");
}
