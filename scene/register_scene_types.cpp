/**************************************************************************/
/*  register_scene_types.cpp                                              */
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

#include "register_scene_types.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/animation/animation_blend_space_1d.h"
#include "scene/animation/animation_blend_space_2d.h"
#include "scene/animation/animation_blend_tree.h"
#include "scene/animation/animation_mixer.h"
#include "scene/animation/animation_node_extension.h"
#include "scene/animation/animation_node_state_machine.h"
#include "scene/animation/animation_player.h"
#include "scene/animation/animation_tree.h"
#include "scene/animation/tween.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/debugger/scene_debugger.h"
#include "scene/gui/aspect_ratio_container.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/center_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/check_button.h"
#include "scene/gui/code_edit.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/color_picker_shape.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/control.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/file_dialog.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/foldable_container.h"
#include "scene/gui/graph_edit.h"
#include "scene/gui/graph_frame.h"
#include "scene/gui/graph_node.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/link_button.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/menu_bar.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/nine_patch_rect.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/reference_rect.h"
#include "scene/gui/rich_text_effect.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/texture_button.h"
#include "scene/gui/texture_progress_bar.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/tree.h"
#include "scene/gui/video_stream_player.h"
#include "scene/main/canvas_item.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/http_request.h"
#include "scene/main/instance_placeholder.h"
#include "scene/main/missing_node.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/resource_preloader.h"
#include "scene/main/scene_tree.h"
#include "scene/main/shader_globals_override.h"
#include "scene/main/status_indicator.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/resources/animation_library.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/audio_stream_polyphonic.h"
#include "scene/resources/audio_stream_wav.h"
#include "scene/resources/bit_map.h"
#include "scene/resources/bone_map.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/camera_texture.h"
#include "scene/resources/canvas_item_material.h"
#include "scene/resources/color_palette.h"
#include "scene/resources/compositor.h"
#include "scene/resources/compressed_texture.h"
#include "scene/resources/curve_texture.h"
#include "scene/resources/environment.h"
#include "scene/resources/external_texture.h"
#include "scene/resources/font.h"
#include "scene/resources/gradient.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/immediate_mesh.h"
#include "scene/resources/label_settings.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh_data_tool.h"
#include "scene/resources/mesh_texture.h"
#include "scene/resources/multimesh.h"
#if !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)
#include "scene/resources/navigation_mesh.h"
#endif // !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)
#include "scene/resources/dpi_texture.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/particle_process_material.h"
#include "scene/resources/placeholder_textures.h"
#include "scene/resources/portable_compressed_texture.h"
#include "scene/resources/resource_format_text.h"
#include "scene/resources/shader_include.h"
#include "scene/resources/skeleton_profile.h"
#include "scene/resources/sky.h"
#include "scene/resources/style_box.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/style_box_line.h"
#include "scene/resources/style_box_texture.h"
#include "scene/resources/surface_tool.h"
#include "scene/resources/syntax_highlighter.h"
#include "scene/resources/text_line.h"
#include "scene/resources/text_paragraph.h"
#include "scene/resources/texture.h"
#include "scene/resources/texture_rd.h"
#include "scene/resources/theme.h"
#include "scene/resources/video_stream.h"
#include "scene/resources/visual_shader.h"
#include "scene/resources/visual_shader_nodes.h"
#include "scene/resources/visual_shader_particle_nodes.h"
#include "scene/resources/visual_shader_sdf_nodes.h"
#include "scene/theme/theme_db.h"

// 2D
#include "scene/2d/animated_sprite_2d.h"
#include "scene/2d/audio_listener_2d.h"
#include "scene/2d/audio_stream_player_2d.h"
#include "scene/2d/back_buffer_copy.h"
#include "scene/2d/camera_2d.h"
#include "scene/2d/canvas_group.h"
#include "scene/2d/canvas_modulate.h"
#include "scene/2d/cpu_particles_2d.h"
#include "scene/2d/gpu_particles_2d.h"
#include "scene/2d/light_2d.h"
#include "scene/2d/light_occluder_2d.h"
#include "scene/2d/line_2d.h"
#include "scene/2d/marker_2d.h"
#include "scene/2d/mesh_instance_2d.h"
#include "scene/2d/multimesh_instance_2d.h"
#include "scene/2d/parallax_2d.h"
#include "scene/2d/path_2d.h"
#include "scene/2d/polygon_2d.h"
#include "scene/2d/remote_transform_2d.h"
#include "scene/2d/skeleton_2d.h"
#include "scene/2d/sprite_2d.h"
#include "scene/2d/tile_map_layer.h"
#include "scene/2d/visible_on_screen_notifier_2d.h"
#include "scene/resources/2d/polygon_path_finder.h"
#include "scene/resources/2d/skeleton/skeleton_modification_2d.h"
#include "scene/resources/2d/skeleton/skeleton_modification_2d_ccdik.h"
#include "scene/resources/2d/skeleton/skeleton_modification_2d_fabrik.h"
#include "scene/resources/2d/skeleton/skeleton_modification_2d_lookat.h"
#include "scene/resources/2d/skeleton/skeleton_modification_2d_stackholder.h"
#include "scene/resources/2d/skeleton/skeleton_modification_2d_twoboneik.h"
#include "scene/resources/2d/skeleton/skeleton_modification_stack_2d.h"
#include "scene/resources/2d/tile_set.h"
#include "scene/resources/world_2d.h"

#ifndef NAVIGATION_2D_DISABLED
#include "scene/2d/navigation/navigation_agent_2d.h"
#include "scene/2d/navigation/navigation_link_2d.h"
#include "scene/2d/navigation/navigation_obstacle_2d.h"
#include "scene/2d/navigation/navigation_region_2d.h"
#include "scene/resources/2d/navigation_mesh_source_geometry_data_2d.h"
#include "scene/resources/2d/navigation_polygon.h"
#endif // NAVIGATION_2D_DISABLED

#ifndef _3D_DISABLED
#include "scene/3d/aim_modifier_3d.h"
#include "scene/3d/audio_listener_3d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/bone_attachment_3d.h"
#include "scene/3d/bone_constraint_3d.h"
#include "scene/3d/bone_twist_disperser_3d.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/ccd_ik_3d.h"
#include "scene/3d/chain_ik_3d.h"
#include "scene/3d/convert_transform_modifier_3d.h"
#include "scene/3d/copy_transform_modifier_3d.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/decal.h"
#include "scene/3d/fabr_ik_3d.h"
#include "scene/3d/fog_volume.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/gpu_particles_collision_3d.h"
#include "scene/3d/ik_modifier_3d.h"
#include "scene/3d/importer_mesh_instance_3d.h"
#include "scene/3d/iterate_ik_3d.h"
#include "scene/3d/jacobian_ik_3d.h"
#include "scene/3d/label_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/lightmap_gi.h"
#include "scene/3d/lightmap_probe.h"
#include "scene/3d/limit_angular_velocity_modifier_3d.h"
#include "scene/3d/look_at_modifier_3d.h"
#include "scene/3d/marker_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/modifier_bone_target_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/occluder_instance_3d.h"
#include "scene/3d/path_3d.h"
#include "scene/3d/reflection_probe.h"
#include "scene/3d/remote_transform_3d.h"
#include "scene/3d/retarget_modifier_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/3d/skeleton_modifier_3d.h"
#include "scene/3d/spline_ik_3d.h"
#include "scene/3d/spring_bone_collision_3d.h"
#include "scene/3d/spring_bone_collision_capsule_3d.h"
#include "scene/3d/spring_bone_collision_plane_3d.h"
#include "scene/3d/spring_bone_collision_sphere_3d.h"
#include "scene/3d/spring_bone_simulator_3d.h"
#include "scene/3d/sprite_3d.h"
#include "scene/3d/two_bone_ik_3d.h"
#include "scene/3d/visible_on_screen_notifier_3d.h"
#include "scene/3d/voxel_gi.h"
#include "scene/3d/world_environment.h"
#include "scene/animation/root_motion_view.h"
#include "scene/resources/3d/fog_material.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/3d/joint_limitation_3d.h"
#include "scene/resources/3d/joint_limitation_cone_3d.h"
#include "scene/resources/3d/mesh_library.h"
#include "scene/resources/3d/navigation_mesh_source_geometry_data_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/sky_material.h"
#include "scene/resources/3d/world_3d.h"
#ifndef NAVIGATION_3D_DISABLED
#include "scene/3d/navigation/navigation_agent_3d.h"
#include "scene/3d/navigation/navigation_link_3d.h"
#include "scene/3d/navigation/navigation_obstacle_3d.h"
#include "scene/3d/navigation/navigation_region_3d.h"
#include "scene/resources/3d/navigation_mesh_source_geometry_data_3d.h"
#endif // NAVIGATION_3D_DISABLED
#ifndef XR_DISABLED
#include "scene/3d/xr/xr_body_modifier_3d.h"
#include "scene/3d/xr/xr_face_modifier_3d.h"
#include "scene/3d/xr/xr_hand_modifier_3d.h"
#include "scene/3d/xr/xr_nodes.h"
#endif // XR_DISABLED
#endif // _3D_DISABLED

#if !defined(PHYSICS_2D_DISABLED) || !defined(PHYSICS_3D_DISABLED)
#include "scene/resources/physics_material.h"
#endif // !defined(PHYSICS_2D_DISABLED) || !defined(PHYSICS_3D_DISABLED)

#ifndef PHYSICS_2D_DISABLED
#include "scene/2d/physics/animatable_body_2d.h"
#include "scene/2d/physics/area_2d.h"
#include "scene/2d/physics/character_body_2d.h"
#include "scene/2d/physics/collision_polygon_2d.h"
#include "scene/2d/physics/collision_shape_2d.h"
#include "scene/2d/physics/joints/damped_spring_joint_2d.h"
#include "scene/2d/physics/joints/groove_joint_2d.h"
#include "scene/2d/physics/joints/joint_2d.h"
#include "scene/2d/physics/joints/pin_joint_2d.h"
#include "scene/2d/physics/kinematic_collision_2d.h"
#include "scene/2d/physics/physical_bone_2d.h"
#include "scene/2d/physics/physics_body_2d.h"
#include "scene/2d/physics/ray_cast_2d.h"
#include "scene/2d/physics/rigid_body_2d.h"
#include "scene/2d/physics/shape_cast_2d.h"
#include "scene/2d/physics/static_body_2d.h"
#include "scene/2d/physics/touch_screen_button.h"
#include "scene/resources/2d/capsule_shape_2d.h"
#include "scene/resources/2d/circle_shape_2d.h"
#include "scene/resources/2d/concave_polygon_shape_2d.h"
#include "scene/resources/2d/convex_polygon_shape_2d.h"
#include "scene/resources/2d/rectangle_shape_2d.h"
#include "scene/resources/2d/segment_shape_2d.h"
#include "scene/resources/2d/separation_ray_shape_2d.h"
#include "scene/resources/2d/shape_2d.h"
#include "scene/resources/2d/skeleton/skeleton_modification_2d_jiggle.h"
#include "scene/resources/2d/skeleton/skeleton_modification_2d_physicalbones.h"
#include "scene/resources/2d/world_boundary_shape_2d.h"
#endif // PHYSICS_2D_DISABLED

#ifndef PHYSICS_3D_DISABLED
#include "scene/3d/physics/animatable_body_3d.h"
#include "scene/3d/physics/area_3d.h"
#include "scene/3d/physics/character_body_3d.h"
#include "scene/3d/physics/collision_polygon_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/joints/cone_twist_joint_3d.h"
#include "scene/3d/physics/joints/generic_6dof_joint_3d.h"
#include "scene/3d/physics/joints/hinge_joint_3d.h"
#include "scene/3d/physics/joints/joint_3d.h"
#include "scene/3d/physics/joints/pin_joint_3d.h"
#include "scene/3d/physics/joints/slider_joint_3d.h"
#include "scene/3d/physics/kinematic_collision_3d.h"
#include "scene/3d/physics/physical_bone_3d.h"
#include "scene/3d/physics/physical_bone_simulator_3d.h"
#include "scene/3d/physics/physics_body_3d.h"
#include "scene/3d/physics/ray_cast_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/3d/physics/shape_cast_3d.h"
#include "scene/3d/physics/soft_body_3d.h"
#include "scene/3d/physics/spring_arm_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/3d/physics/vehicle_body_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/height_map_shape_3d.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/3d/mesh_library.h"
#include "scene/resources/3d/navigation_mesh_source_geometry_data_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/separation_ray_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/3d/world_boundary_shape_3d.h"
#endif // PHYSICS_3D_DISABLED

static Ref<ResourceFormatSaverText> resource_saver_text;
static Ref<ResourceFormatLoaderText> resource_loader_text;

static Ref<ResourceFormatLoaderCompressedTexture2D> resource_loader_stream_texture;
static Ref<ResourceFormatLoaderCompressedTextureLayered> resource_loader_texture_layered;
static Ref<ResourceFormatLoaderCompressedTexture3D> resource_loader_texture_3d;

static Ref<ResourceFormatSaverShader> resource_saver_shader;
static Ref<ResourceFormatLoaderShader> resource_loader_shader;

static Ref<ResourceFormatSaverShaderInclude> resource_saver_shader_include;
static Ref<ResourceFormatLoaderShaderInclude> resource_loader_shader_include;

void register_scene_types() {
	OS::get_singleton()->benchmark_begin_measure("Scene", "Register Types");

	SceneStringNames::create();

	OS::get_singleton()->yield(); // may take time to init

	Node::init_node_hrcr();

	if constexpr (GD_IS_CLASS_ENABLED(CompressedTexture2D)) {
		resource_loader_stream_texture.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_stream_texture);
	}

	if constexpr (GD_IS_CLASS_ENABLED(TextureLayered)) {
		resource_loader_texture_layered.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_texture_layered);
	}

	if constexpr (GD_IS_CLASS_ENABLED(Texture3D)) {
		resource_loader_texture_3d.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_texture_3d);
	}

	resource_saver_text.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_text, true);

	resource_loader_text.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_text, true);

	if constexpr (GD_IS_CLASS_ENABLED(Shader)) {
		resource_saver_shader.instantiate();
		ResourceSaver::add_resource_format_saver(resource_saver_shader, true);

		resource_loader_shader.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_shader, true);
	}

	if constexpr (GD_IS_CLASS_ENABLED(ShaderInclude)) {
		resource_saver_shader_include.instantiate();
		ResourceSaver::add_resource_format_saver(resource_saver_shader_include, true);

		resource_loader_shader_include.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_shader_include, true);
	}

	OS::get_singleton()->yield(); // may take time to init

	FOUNDRY_REGISTER_CLASS(Node);
	FOUNDRY_REGISTER_CLASS(MissingNode);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(InstancePlaceholder);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(CanvasItem);

	FOUNDRY_REGISTER_VIRTUAL_CLASS(Texture);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(Texture2D);

	FOUNDRY_REGISTER_VIRTUAL_CLASS(Material);
	FOUNDRY_REGISTER_CLASS(PlaceholderMaterial);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(Viewport);
	FOUNDRY_REGISTER_CLASS(SubViewport);
	FOUNDRY_REGISTER_CLASS(ViewportTexture);

	FOUNDRY_REGISTER_VIRTUAL_CLASS(CompositorEffect);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(MultiplayerPeer);
	FOUNDRY_REGISTER_CLASS(MultiplayerPeerExtension);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(MultiplayerAPI);
	FOUNDRY_REGISTER_CLASS(MultiplayerAPIExtension);

	FOUNDRY_REGISTER_CLASS(HTTPRequest);
	FOUNDRY_REGISTER_CLASS(Timer);
	FOUNDRY_REGISTER_CLASS(CanvasLayer);
	FOUNDRY_REGISTER_CLASS(ResourcePreloader);
	FOUNDRY_REGISTER_CLASS(Window);

	FOUNDRY_REGISTER_CLASS(StatusIndicator);

	/* REGISTER GUI */

	OS::get_singleton()->yield(); // may take time to init

	FOUNDRY_REGISTER_CLASS(Control);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(BaseButton);
	FOUNDRY_REGISTER_CLASS(Button);
	FOUNDRY_REGISTER_CLASS(Label);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(Range);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(ScrollBar);
	FOUNDRY_REGISTER_CLASS(HScrollBar);
	FOUNDRY_REGISTER_CLASS(VScrollBar);
	FOUNDRY_REGISTER_CLASS(ProgressBar);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Slider);
	FOUNDRY_REGISTER_CLASS(HSlider);
	FOUNDRY_REGISTER_CLASS(VSlider);
	FOUNDRY_REGISTER_CLASS(Popup);
	FOUNDRY_REGISTER_CLASS(PopupPanel);
	FOUNDRY_REGISTER_CLASS(CheckBox);
	FOUNDRY_REGISTER_CLASS(CheckButton);
	FOUNDRY_REGISTER_CLASS(LinkButton);
	FOUNDRY_REGISTER_CLASS(Panel);
	FOUNDRY_REGISTER_CLASS(ButtonGroup);

	OS::get_singleton()->yield(); // may take time to init

	FOUNDRY_REGISTER_CLASS(Container);
	FOUNDRY_REGISTER_CLASS(TextureRect);
	FOUNDRY_REGISTER_CLASS(ColorRect);
	FOUNDRY_REGISTER_CLASS(NinePatchRect);
	FOUNDRY_REGISTER_CLASS(ReferenceRect);
	FOUNDRY_REGISTER_CLASS(AspectRatioContainer);
	FOUNDRY_REGISTER_CLASS(TabContainer);
	FOUNDRY_REGISTER_CLASS(TabBar);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Separator);
	FOUNDRY_REGISTER_CLASS(HSeparator);
	FOUNDRY_REGISTER_CLASS(VSeparator);
	FOUNDRY_REGISTER_CLASS(TextureButton);
	FOUNDRY_REGISTER_CLASS(BoxContainer);
	FOUNDRY_REGISTER_CLASS(HBoxContainer);
	FOUNDRY_REGISTER_CLASS(VBoxContainer);
	FOUNDRY_REGISTER_CLASS(GridContainer);
	FOUNDRY_REGISTER_CLASS(CenterContainer);
	FOUNDRY_REGISTER_CLASS(ScrollContainer);
	FOUNDRY_REGISTER_CLASS(PanelContainer);
	FOUNDRY_REGISTER_CLASS(FlowContainer);
	FOUNDRY_REGISTER_CLASS(HFlowContainer);
	FOUNDRY_REGISTER_CLASS(VFlowContainer);
	FOUNDRY_REGISTER_CLASS(MarginContainer);

	OS::get_singleton()->yield(); // may take time to init

	FOUNDRY_REGISTER_CLASS(TextureProgressBar);
	FOUNDRY_REGISTER_CLASS(ItemList);

	FOUNDRY_REGISTER_CLASS(LineEdit);
	FOUNDRY_REGISTER_CLASS(VideoStreamPlayer);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(VideoStreamPlayback);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(VideoStream);

#ifndef ADVANCED_GUI_DISABLED
	FOUNDRY_REGISTER_CLASS(AcceptDialog);
	FOUNDRY_REGISTER_CLASS(ConfirmationDialog);

	FOUNDRY_REGISTER_CLASS(FileDialog);

	FOUNDRY_REGISTER_CLASS(PopupMenu);
	FOUNDRY_REGISTER_CLASS(Tree);

	FOUNDRY_REGISTER_CLASS(TextEdit);
	FOUNDRY_REGISTER_CLASS(CodeEdit);
	FOUNDRY_REGISTER_CLASS(SyntaxHighlighter);
	FOUNDRY_REGISTER_CLASS(CodeHighlighter);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(TreeItem);
	FOUNDRY_REGISTER_CLASS(MenuBar);
	FOUNDRY_REGISTER_CLASS(MenuButton);
	FOUNDRY_REGISTER_CLASS(OptionButton);
	FOUNDRY_REGISTER_CLASS(SpinBox);
	FOUNDRY_REGISTER_CLASS(ColorPicker);
	FOUNDRY_REGISTER_CLASS(ColorPickerButton);
	FOUNDRY_REGISTER_CLASS(RichTextLabel);
	FOUNDRY_REGISTER_CLASS(RichTextEffect);
	FOUNDRY_REGISTER_CLASS(CharFXTransform);

	FOUNDRY_REGISTER_CLASS(SubViewportContainer);
	FOUNDRY_REGISTER_CLASS(SplitContainer);
	FOUNDRY_REGISTER_CLASS(HSplitContainer);
	FOUNDRY_REGISTER_CLASS(VSplitContainer);

	FOUNDRY_REGISTER_CLASS(GraphElement);
	FOUNDRY_REGISTER_CLASS(GraphNode);
	FOUNDRY_REGISTER_CLASS(GraphFrame);
	FOUNDRY_REGISTER_CLASS(GraphEdit);

	FOUNDRY_REGISTER_CLASS(FoldableGroup);
	FOUNDRY_REGISTER_CLASS(FoldableContainer);

	OS::get_singleton()->yield(); // may take time to init

	int swap_cancel_ok = GLOBAL_DEF(PropertyInfo(Variant::INT, "gui/common/swap_cancel_ok", PROPERTY_HINT_ENUM, "Auto,Cancel First,OK First"), 0);
	if (DisplayServer::get_singleton() && swap_cancel_ok == 0) {
		swap_cancel_ok = DisplayServer::get_singleton()->get_swap_cancel_ok() ? 2 : 1;
	}
	AcceptDialog::set_swap_cancel_ok(swap_cancel_ok == 2);
#endif

	int root_dir = GLOBAL_GET("internationalization/rendering/root_node_layout_direction");
	Control::set_root_layout_direction(root_dir);
	Window::set_root_layout_direction(root_dir);

	/* REGISTER ANIMATION */
	FOUNDRY_REGISTER_CLASS(Tween);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Tweener);
	FOUNDRY_REGISTER_CLASS(PropertyTweener);
	FOUNDRY_REGISTER_CLASS(IntervalTweener);
	FOUNDRY_REGISTER_CLASS(CallbackTweener);
	FOUNDRY_REGISTER_CLASS(MethodTweener);
	FOUNDRY_REGISTER_CLASS(SubtweenTweener);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(AnimationMixer);
	FOUNDRY_REGISTER_CLASS(AnimationPlayer);
	FOUNDRY_REGISTER_CLASS(AnimationTree);
	FOUNDRY_REGISTER_CLASS(AnimationNode);
	FOUNDRY_REGISTER_CLASS(AnimationRootNode);
	FOUNDRY_REGISTER_CLASS(AnimationNodeBlendTree);
	FOUNDRY_REGISTER_CLASS(AnimationNodeBlendSpace1D);
	FOUNDRY_REGISTER_CLASS(AnimationNodeBlendSpace2D);
	FOUNDRY_REGISTER_CLASS(AnimationNodeStateMachine);
	FOUNDRY_REGISTER_CLASS(AnimationNodeStateMachinePlayback);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(AnimationNodeExtension);

	FOUNDRY_REGISTER_CLASS(AnimationNodeSync);
	FOUNDRY_REGISTER_CLASS(AnimationNodeStateMachineTransition);
	FOUNDRY_REGISTER_CLASS(AnimationNodeOutput);
	FOUNDRY_REGISTER_CLASS(AnimationNodeOneShot);
	FOUNDRY_REGISTER_CLASS(AnimationNodeAnimation);
	FOUNDRY_REGISTER_CLASS(AnimationNodeAdd2);
	FOUNDRY_REGISTER_CLASS(AnimationNodeAdd3);
	FOUNDRY_REGISTER_CLASS(AnimationNodeBlend2);
	FOUNDRY_REGISTER_CLASS(AnimationNodeBlend3);
	FOUNDRY_REGISTER_CLASS(AnimationNodeSub2);
	FOUNDRY_REGISTER_CLASS(AnimationNodeTimeScale);
	FOUNDRY_REGISTER_CLASS(AnimationNodeTimeSeek);
	FOUNDRY_REGISTER_CLASS(AnimationNodeTransition);

	FOUNDRY_REGISTER_CLASS(ShaderGlobalsOverride); // can be used in any shader

	OS::get_singleton()->yield(); // may take time to init

	/* REGISTER 3D */

#ifndef _3D_DISABLED
	FOUNDRY_REGISTER_CLASS(Node3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Node3DGizmo);
	FOUNDRY_REGISTER_CLASS(Skin);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(SkinReference);
	FOUNDRY_REGISTER_CLASS(Skeleton3D);
	FOUNDRY_REGISTER_CLASS(ImporterMesh);
	FOUNDRY_REGISTER_CLASS(ImporterMeshInstance3D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(VisualInstance3D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(GeometryInstance3D);
	FOUNDRY_REGISTER_CLASS(Camera3D);
	FOUNDRY_REGISTER_CLASS(AudioListener3D);
	FOUNDRY_REGISTER_CLASS(MeshInstance3D);
	FOUNDRY_REGISTER_CLASS(OccluderInstance3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Occluder3D);
	FOUNDRY_REGISTER_CLASS(ArrayOccluder3D);
	FOUNDRY_REGISTER_CLASS(QuadOccluder3D);
	FOUNDRY_REGISTER_CLASS(BoxOccluder3D);
	FOUNDRY_REGISTER_CLASS(SphereOccluder3D);
	FOUNDRY_REGISTER_CLASS(PolygonOccluder3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(SpriteBase3D);
	FOUNDRY_REGISTER_CLASS(Sprite3D);
	FOUNDRY_REGISTER_CLASS(AnimatedSprite3D);
	FOUNDRY_REGISTER_CLASS(Label3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Light3D);
	FOUNDRY_REGISTER_CLASS(DirectionalLight3D);
	FOUNDRY_REGISTER_CLASS(OmniLight3D);
	FOUNDRY_REGISTER_CLASS(SpotLight3D);
	FOUNDRY_REGISTER_CLASS(ReflectionProbe);
	FOUNDRY_REGISTER_CLASS(Decal);
	FOUNDRY_REGISTER_CLASS(VoxelGI);
	FOUNDRY_REGISTER_CLASS(VoxelGIData);
	FOUNDRY_REGISTER_CLASS(LightmapGI);
	FOUNDRY_REGISTER_CLASS(LightmapGIData);
	FOUNDRY_REGISTER_CLASS(LightmapProbe);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Lightmapper);
	FOUNDRY_REGISTER_CLASS(GPUParticles3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(GPUParticlesCollision3D);
	FOUNDRY_REGISTER_CLASS(GPUParticlesCollisionBox3D);
	FOUNDRY_REGISTER_CLASS(GPUParticlesCollisionSphere3D);
	FOUNDRY_REGISTER_CLASS(GPUParticlesCollisionSDF3D);
	FOUNDRY_REGISTER_CLASS(GPUParticlesCollisionHeightField3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(GPUParticlesAttractor3D);
	FOUNDRY_REGISTER_CLASS(GPUParticlesAttractorBox3D);
	FOUNDRY_REGISTER_CLASS(GPUParticlesAttractorSphere3D);
	FOUNDRY_REGISTER_CLASS(GPUParticlesAttractorVectorField3D);
	FOUNDRY_REGISTER_CLASS(CPUParticles3D);
	FOUNDRY_REGISTER_CLASS(Marker3D);
	FOUNDRY_REGISTER_CLASS(RootMotionView);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(SkeletonModifier3D);
	FOUNDRY_REGISTER_CLASS(ModifierBoneTarget3D);
	FOUNDRY_REGISTER_CLASS(RetargetModifier3D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(JointLimitation3D);
	FOUNDRY_REGISTER_CLASS(JointLimitationCone3D);
	FOUNDRY_REGISTER_CLASS(SpringBoneSimulator3D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(SpringBoneCollision3D);
	FOUNDRY_REGISTER_CLASS(SpringBoneCollisionSphere3D);
	FOUNDRY_REGISTER_CLASS(SpringBoneCollisionCapsule3D);
	FOUNDRY_REGISTER_CLASS(SpringBoneCollisionPlane3D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(BoneConstraint3D);
	FOUNDRY_REGISTER_CLASS(CopyTransformModifier3D);
	FOUNDRY_REGISTER_CLASS(ConvertTransformModifier3D);
	FOUNDRY_REGISTER_CLASS(AimModifier3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(IKModifier3D);
	FOUNDRY_REGISTER_CLASS(TwoBoneIK3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(ChainIK3D);
	FOUNDRY_REGISTER_CLASS(SplineIK3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(IterateIK3D);
	FOUNDRY_REGISTER_CLASS(FABRIK3D);
	FOUNDRY_REGISTER_CLASS(CCDIK3D);
	FOUNDRY_REGISTER_CLASS(JacobianIK3D);
	FOUNDRY_REGISTER_CLASS(LimitAngularVelocityModifier3D);
	FOUNDRY_REGISTER_CLASS(BoneTwistDisperser3D);

#ifndef XR_DISABLED
	FOUNDRY_REGISTER_CLASS(XRCamera3D);
	FOUNDRY_REGISTER_CLASS(XRNode3D);
	FOUNDRY_REGISTER_CLASS(XRController3D);
	FOUNDRY_REGISTER_CLASS(XRAnchor3D);
	FOUNDRY_REGISTER_CLASS(XROrigin3D);
	FOUNDRY_REGISTER_CLASS(XRBodyModifier3D);
	FOUNDRY_REGISTER_CLASS(XRHandModifier3D);
	FOUNDRY_REGISTER_CLASS(XRFaceModifier3D);
#endif // XR_DISABLED

	OS::get_singleton()->yield(); // may take time to init

#ifndef PHYSICS_3D_DISABLED
	FOUNDRY_REGISTER_ABSTRACT_CLASS(CollisionObject3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(PhysicsBody3D);
	FOUNDRY_REGISTER_CLASS(StaticBody3D);
	FOUNDRY_REGISTER_CLASS(AnimatableBody3D);
	FOUNDRY_REGISTER_CLASS(RigidBody3D);
	FOUNDRY_REGISTER_CLASS(KinematicCollision3D);
	FOUNDRY_REGISTER_CLASS(CharacterBody3D);
	FOUNDRY_REGISTER_CLASS(SpringArm3D);

	FOUNDRY_REGISTER_CLASS(PhysicalBoneSimulator3D);
	FOUNDRY_REGISTER_CLASS(PhysicalBone3D);
	FOUNDRY_REGISTER_CLASS(SoftBody3D);
#endif // PHYSICS_3D_DISABLED

	FOUNDRY_REGISTER_CLASS(BoneAttachment3D);
	FOUNDRY_REGISTER_CLASS(LookAtModifier3D);

#ifndef PHYSICS_3D_DISABLED
	FOUNDRY_REGISTER_CLASS(VehicleBody3D);
	FOUNDRY_REGISTER_CLASS(VehicleWheel3D);
	FOUNDRY_REGISTER_CLASS(Area3D);
	FOUNDRY_REGISTER_CLASS(CollisionShape3D);
	FOUNDRY_REGISTER_CLASS(CollisionPolygon3D);
	FOUNDRY_REGISTER_CLASS(RayCast3D);
	FOUNDRY_REGISTER_CLASS(ShapeCast3D);
#endif // PHYSICS_3D_DISABLED
	FOUNDRY_REGISTER_CLASS(MultiMeshInstance3D);

	FOUNDRY_REGISTER_CLASS(Curve3D);
	FOUNDRY_REGISTER_CLASS(Path3D);
	FOUNDRY_REGISTER_CLASS(PathFollow3D);
	FOUNDRY_REGISTER_CLASS(VisibleOnScreenNotifier3D);
	FOUNDRY_REGISTER_CLASS(VisibleOnScreenEnabler3D);
	FOUNDRY_REGISTER_CLASS(WorldEnvironment);
	FOUNDRY_REGISTER_CLASS(FogVolume);
	FOUNDRY_REGISTER_CLASS(FogMaterial);
	FOUNDRY_REGISTER_CLASS(RemoteTransform3D);

#ifndef PHYSICS_3D_DISABLED
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Joint3D);
	FOUNDRY_REGISTER_CLASS(PinJoint3D);
	FOUNDRY_REGISTER_CLASS(HingeJoint3D);
	FOUNDRY_REGISTER_CLASS(SliderJoint3D);
	FOUNDRY_REGISTER_CLASS(ConeTwistJoint3D);
	FOUNDRY_REGISTER_CLASS(Generic6DOFJoint3D);
#endif // PHYSICS_3D_DISABLED

#ifndef NAVIGATION_3D_DISABLED
	FOUNDRY_REGISTER_CLASS(NavigationMeshSourceGeometryData3D);
	FOUNDRY_REGISTER_CLASS(NavigationRegion3D);
	FOUNDRY_REGISTER_CLASS(NavigationAgent3D);
	FOUNDRY_REGISTER_CLASS(NavigationObstacle3D);
	FOUNDRY_REGISTER_CLASS(NavigationLink3D);
#endif // NAVIGATION_3D_DISABLED

	OS::get_singleton()->yield(); // may take time to init
#endif // _3D_DISABLED

	/* REGISTER SHADER */

	FOUNDRY_REGISTER_CLASS(Shader);
	FOUNDRY_REGISTER_CLASS(VisualShader);
	FOUNDRY_REGISTER_CLASS(ShaderInclude);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNode);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeCustom);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeInput);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNodeOutput);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNodeResizableBase);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNodeGroupBase);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNodeConstant);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNodeVectorBase);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeFrame);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeFloatConstant);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeIntConstant);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeUIntConstant);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeBooleanConstant);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeColorConstant);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVec2Constant);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVec3Constant);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVec4Constant);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTransformConstant);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeFloatOp);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeIntOp);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeUIntOp);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVectorOp);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeColorOp);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTransformOp);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTransformVecMult);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeFloatFunc);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeIntFunc);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeUIntFunc);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVectorFunc);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeColorFunc);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTransformFunc);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeUVFunc);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeUVPolarCoord);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeDotProduct);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVectorLen);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeDeterminant);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeDerivativeFunc);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeClamp);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeFaceForward);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeOuterProduct);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeSmoothStep);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeStep);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVectorDistance);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVectorRefract);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeMix);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVectorCompose);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTransformCompose);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVectorDecompose);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTransformDecompose);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTexture);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeCurveTexture);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeCurveXYZTexture);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNodeSample3D);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTexture2DArray);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTexture3D);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeCubemap);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNodeParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParameterRef);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeFloatParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeIntParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeUIntParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeBooleanParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeColorParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVec2Parameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVec3Parameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVec4Parameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTransformParameter);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNodeTextureParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTexture2DParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTextureParameterTriplanar);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTexture2DArrayParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTexture3DParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeCubemapParameter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeLinearSceneDepth);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeWorldPositionFromDepth);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeScreenNormalWorldSpace);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeIf);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeSwitch);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeFresnel);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeExpression);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeGlobalExpression);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeIs);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeCompare);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeMultiplyAdd);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeBillboard);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeDistanceFade);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeProximityFade);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeRandomRange);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeRemap);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeRotationByAxis);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNodeVarying);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVaryingSetter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeVaryingGetter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeReroute);

	FOUNDRY_REGISTER_CLASS(VisualShaderNodeSDFToScreenUV);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeScreenUVToSDF);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTextureSDF);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeTextureSDFNormal);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeSDFRaymarch);

	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParticleOutput);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(VisualShaderNodeParticleEmitter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParticleSphereEmitter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParticleBoxEmitter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParticleRingEmitter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParticleMeshEmitter);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParticleMultiplyByAxisAngle);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParticleConeVelocity);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParticleRandomness);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParticleAccelerator);
	FOUNDRY_REGISTER_CLASS(VisualShaderNodeParticleEmit);

	FOUNDRY_REGISTER_CLASS(ShaderMaterial);
	FOUNDRY_REGISTER_CLASS(CanvasTexture);
	FOUNDRY_REGISTER_CLASS(CanvasItemMaterial);
	SceneTree::add_idle_callback(CanvasItemMaterial::flush_changes);
	CanvasItemMaterial::init_shaders();

	/* REGISTER 2D */

	FOUNDRY_REGISTER_CLASS(Node2D);
	FOUNDRY_REGISTER_CLASS(CanvasGroup);
	FOUNDRY_REGISTER_CLASS(CPUParticles2D);
	FOUNDRY_REGISTER_CLASS(GPUParticles2D);
	FOUNDRY_REGISTER_CLASS(Sprite2D);
	FOUNDRY_REGISTER_CLASS(SpriteFrames);
	FOUNDRY_REGISTER_CLASS(AnimatedSprite2D);
	FOUNDRY_REGISTER_CLASS(Marker2D);
	FOUNDRY_REGISTER_CLASS(Line2D);
	FOUNDRY_REGISTER_CLASS(MeshInstance2D);
	FOUNDRY_REGISTER_CLASS(MultiMeshInstance2D);
#ifndef PHYSICS_2D_DISABLED
	FOUNDRY_REGISTER_ABSTRACT_CLASS(CollisionObject2D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(PhysicsBody2D);
	FOUNDRY_REGISTER_CLASS(StaticBody2D);
	FOUNDRY_REGISTER_CLASS(AnimatableBody2D);
	FOUNDRY_REGISTER_CLASS(RigidBody2D);
	FOUNDRY_REGISTER_CLASS(CharacterBody2D);
	FOUNDRY_REGISTER_CLASS(KinematicCollision2D);
	FOUNDRY_REGISTER_CLASS(Area2D);
	FOUNDRY_REGISTER_CLASS(CollisionShape2D);
	FOUNDRY_REGISTER_CLASS(CollisionPolygon2D);
	FOUNDRY_REGISTER_CLASS(RayCast2D);
	FOUNDRY_REGISTER_CLASS(ShapeCast2D);
#endif // PHYSICS_2D_DISABLED
	FOUNDRY_REGISTER_CLASS(VisibleOnScreenNotifier2D);
	FOUNDRY_REGISTER_CLASS(VisibleOnScreenEnabler2D);
	FOUNDRY_REGISTER_CLASS(Polygon2D);
	FOUNDRY_REGISTER_CLASS(Skeleton2D);
	FOUNDRY_REGISTER_CLASS(Bone2D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Light2D);
	FOUNDRY_REGISTER_CLASS(PointLight2D);
	FOUNDRY_REGISTER_CLASS(DirectionalLight2D);
	FOUNDRY_REGISTER_CLASS(LightOccluder2D);
	FOUNDRY_REGISTER_CLASS(OccluderPolygon2D);
	FOUNDRY_REGISTER_CLASS(BackBufferCopy);
	FOUNDRY_REGISTER_CLASS(CanvasModulate);

	OS::get_singleton()->yield(); // may take time to init

	FOUNDRY_REGISTER_CLASS(Camera2D);
	FOUNDRY_REGISTER_CLASS(AudioListener2D);
#ifndef PHYSICS_2D_DISABLED
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Joint2D);
	FOUNDRY_REGISTER_CLASS(PinJoint2D);
	FOUNDRY_REGISTER_CLASS(GrooveJoint2D);
	FOUNDRY_REGISTER_CLASS(DampedSpringJoint2D);
	FOUNDRY_REGISTER_CLASS(TouchScreenButton);
#endif // PHYSICS_2D_DISABLED
	FOUNDRY_REGISTER_CLASS(TileSet);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(TileSetSource);
	FOUNDRY_REGISTER_CLASS(TileSetAtlasSource);
	FOUNDRY_REGISTER_CLASS(TileSetScenesCollectionSource);
	FOUNDRY_REGISTER_CLASS(TileMapPattern);
	FOUNDRY_REGISTER_CLASS(TileData);
	FOUNDRY_REGISTER_CLASS(TileMapLayer);
	FOUNDRY_REGISTER_CLASS(Parallax2D);
	FOUNDRY_REGISTER_CLASS(RemoteTransform2D);

	FOUNDRY_REGISTER_CLASS(SkeletonModificationStack2D);
	FOUNDRY_REGISTER_CLASS(SkeletonModification2D);
	FOUNDRY_REGISTER_CLASS(SkeletonModification2DLookAt);
	FOUNDRY_REGISTER_CLASS(SkeletonModification2DCCDIK);
	FOUNDRY_REGISTER_CLASS(SkeletonModification2DFABRIK);
	FOUNDRY_REGISTER_CLASS(SkeletonModification2DTwoBoneIK);
	FOUNDRY_REGISTER_CLASS(SkeletonModification2DStackHolder);

#ifndef PHYSICS_2D_DISABLED
	FOUNDRY_REGISTER_CLASS(PhysicalBone2D);
	FOUNDRY_REGISTER_CLASS(SkeletonModification2DJiggle);
	FOUNDRY_REGISTER_CLASS(SkeletonModification2DPhysicalBones);
#endif // PHYSICS_2D_DISABLED

	OS::get_singleton()->yield(); // may take time to init

	/* REGISTER RESOURCES */

	FOUNDRY_REGISTER_ABSTRACT_CLASS(Shader);
	FOUNDRY_REGISTER_CLASS(ParticleProcessMaterial);
	SceneTree::add_idle_callback(ParticleProcessMaterial::flush_changes);
	ParticleProcessMaterial::init_shaders();

	FOUNDRY_REGISTER_VIRTUAL_CLASS(Mesh);
	FOUNDRY_REGISTER_CLASS(MeshConvexDecompositionSettings);
	FOUNDRY_REGISTER_CLASS(ArrayMesh);
	FOUNDRY_REGISTER_CLASS(PlaceholderMesh);
	FOUNDRY_REGISTER_CLASS(ImmediateMesh);
	FOUNDRY_REGISTER_CLASS(MultiMesh);
	FOUNDRY_REGISTER_CLASS(SurfaceTool);
	FOUNDRY_REGISTER_CLASS(MeshDataTool);

#ifndef _3D_DISABLED
	FOUNDRY_REGISTER_CLASS(AudioStreamPlayer3D);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(PrimitiveMesh);
	FOUNDRY_REGISTER_CLASS(BoxMesh);
	FOUNDRY_REGISTER_CLASS(CapsuleMesh);
	FOUNDRY_REGISTER_CLASS(CylinderMesh);
	FOUNDRY_REGISTER_CLASS(PlaneMesh);
	FOUNDRY_REGISTER_CLASS(PrismMesh);
	FOUNDRY_REGISTER_CLASS(QuadMesh);
	FOUNDRY_REGISTER_CLASS(SphereMesh);
	FOUNDRY_REGISTER_CLASS(TextMesh);
	FOUNDRY_REGISTER_CLASS(TorusMesh);
	FOUNDRY_REGISTER_CLASS(TubeTrailMesh);
	FOUNDRY_REGISTER_CLASS(RibbonTrailMesh);
	FOUNDRY_REGISTER_CLASS(PointMesh);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(BaseMaterial3D);
	FOUNDRY_REGISTER_CLASS(StandardMaterial3D);
	FOUNDRY_REGISTER_CLASS(ORMMaterial3D);
	FOUNDRY_REGISTER_CLASS(ProceduralSkyMaterial);
	FOUNDRY_REGISTER_CLASS(PanoramaSkyMaterial);
	FOUNDRY_REGISTER_CLASS(PhysicalSkyMaterial);
	SceneTree::add_idle_callback(BaseMaterial3D::flush_changes);
	BaseMaterial3D::init_shaders();

	FOUNDRY_REGISTER_CLASS(MeshLibrary);

	OS::get_singleton()->yield(); // may take time to init

#ifndef PHYSICS_3D_DISABLED
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Shape3D);
	FOUNDRY_REGISTER_CLASS(SeparationRayShape3D);
	FOUNDRY_REGISTER_CLASS(SphereShape3D);
	FOUNDRY_REGISTER_CLASS(BoxShape3D);
	FOUNDRY_REGISTER_CLASS(CapsuleShape3D);
	FOUNDRY_REGISTER_CLASS(CylinderShape3D);
	FOUNDRY_REGISTER_CLASS(HeightMapShape3D);
	FOUNDRY_REGISTER_CLASS(WorldBoundaryShape3D);
	FOUNDRY_REGISTER_CLASS(ConvexPolygonShape3D);
	FOUNDRY_REGISTER_CLASS(ConcavePolygonShape3D);
#endif // PHYSICS_3D_DISABLED
	FOUNDRY_REGISTER_CLASS(World3D);

	OS::get_singleton()->yield(); // may take time to init
#endif // _3D_DISABLED

#if !defined(PHYSICS_2D_DISABLED) || !defined(PHYSICS_3D_DISABLED)
	FOUNDRY_REGISTER_CLASS(PhysicsMaterial);
#endif // !defined(PHYSICS_2D_DISABLED) || !defined(PHYSICS_3D_DISABLED)
	FOUNDRY_REGISTER_CLASS(Compositor);
	FOUNDRY_REGISTER_CLASS(Environment);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(CameraAttributes);
	FOUNDRY_REGISTER_CLASS(CameraAttributesPhysical);
	FOUNDRY_REGISTER_CLASS(CameraAttributesPractical);
	FOUNDRY_REGISTER_CLASS(World2D);
	FOUNDRY_REGISTER_CLASS(Sky);
	FOUNDRY_REGISTER_CLASS(CompressedTexture2D);
	FOUNDRY_REGISTER_CLASS(PortableCompressedTexture2D);
	FOUNDRY_REGISTER_CLASS(ImageTexture);
	FOUNDRY_REGISTER_CLASS(AtlasTexture);
	FOUNDRY_REGISTER_CLASS(MeshTexture);
	FOUNDRY_REGISTER_CLASS(CurveTexture);
	FOUNDRY_REGISTER_CLASS(CurveXYZTexture);
	FOUNDRY_REGISTER_CLASS(GradientTexture1D);
	FOUNDRY_REGISTER_CLASS(GradientTexture2D);
	FOUNDRY_REGISTER_CLASS(CameraTexture);
	FOUNDRY_REGISTER_CLASS(ExternalTexture);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(TextureLayered);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(ImageTextureLayered);
	FOUNDRY_REGISTER_VIRTUAL_CLASS(Texture3D);
	FOUNDRY_REGISTER_CLASS(ImageTexture3D);
	FOUNDRY_REGISTER_CLASS(CompressedTexture3D);
	FOUNDRY_REGISTER_CLASS(Cubemap);
	FOUNDRY_REGISTER_CLASS(CubemapArray);
	FOUNDRY_REGISTER_CLASS(Texture2DArray);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(CompressedTextureLayered);
	FOUNDRY_REGISTER_CLASS(CompressedCubemap);
	FOUNDRY_REGISTER_CLASS(CompressedCubemapArray);
	FOUNDRY_REGISTER_CLASS(CompressedTexture2DArray);
	FOUNDRY_REGISTER_CLASS(PlaceholderTexture2D);
	FOUNDRY_REGISTER_CLASS(PlaceholderTexture3D);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(PlaceholderTextureLayered);
	FOUNDRY_REGISTER_CLASS(PlaceholderTexture2DArray);
	FOUNDRY_REGISTER_CLASS(PlaceholderCubemap);
	FOUNDRY_REGISTER_CLASS(PlaceholderCubemapArray);
	FOUNDRY_REGISTER_CLASS(DPITexture);

	// These classes are part of renderer_rd
	FOUNDRY_REGISTER_CLASS(Texture2DRD);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(TextureLayeredRD);
	FOUNDRY_REGISTER_CLASS(Texture2DArrayRD);
	FOUNDRY_REGISTER_CLASS(TextureCubemapRD);
	FOUNDRY_REGISTER_CLASS(TextureCubemapArrayRD);
	FOUNDRY_REGISTER_CLASS(Texture3DRD);

	FOUNDRY_REGISTER_CLASS(Animation);
	FOUNDRY_REGISTER_CLASS(AnimationLibrary);

	FOUNDRY_REGISTER_ABSTRACT_CLASS(Font);
	FOUNDRY_REGISTER_CLASS(FontFile);
	FOUNDRY_REGISTER_CLASS(FontVariation);
	FOUNDRY_REGISTER_CLASS(SystemFont);
	FOUNDRY_REGISTER_CLASS(ColorPalette);

	FOUNDRY_REGISTER_CLASS(Curve);

	FOUNDRY_REGISTER_CLASS(LabelSettings);

	FOUNDRY_REGISTER_CLASS(TextLine);
	FOUNDRY_REGISTER_CLASS(TextParagraph);

	FOUNDRY_REGISTER_VIRTUAL_CLASS(StyleBox);
	FOUNDRY_REGISTER_CLASS(StyleBoxEmpty);
	FOUNDRY_REGISTER_CLASS(StyleBoxTexture);
	FOUNDRY_REGISTER_CLASS(StyleBoxFlat);
	FOUNDRY_REGISTER_CLASS(StyleBoxLine);
	FOUNDRY_REGISTER_CLASS(Theme);

	FOUNDRY_REGISTER_CLASS(BitMap);
	FOUNDRY_REGISTER_CLASS(Gradient);

	FOUNDRY_REGISTER_CLASS(SkeletonProfile);
	FOUNDRY_REGISTER_CLASS(SkeletonProfileHumanoid);
	FOUNDRY_REGISTER_CLASS(BoneMap);

	OS::get_singleton()->yield(); // may take time to init

	FOUNDRY_REGISTER_CLASS(AudioStreamPlayer);
	FOUNDRY_REGISTER_CLASS(AudioStreamWAV);
	FOUNDRY_REGISTER_CLASS(AudioStreamPolyphonic);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(AudioStreamPlaybackPolyphonic);

	OS::get_singleton()->yield(); // may take time to init

	FOUNDRY_REGISTER_CLASS(AudioStreamPlayer2D);
	FOUNDRY_REGISTER_CLASS(Curve2D);
	FOUNDRY_REGISTER_CLASS(Path2D);
	FOUNDRY_REGISTER_CLASS(PathFollow2D);

#ifndef PHYSICS_2D_DISABLED
	FOUNDRY_REGISTER_ABSTRACT_CLASS(Shape2D);
	FOUNDRY_REGISTER_CLASS(WorldBoundaryShape2D);
	FOUNDRY_REGISTER_CLASS(SegmentShape2D);
	FOUNDRY_REGISTER_CLASS(SeparationRayShape2D);
	FOUNDRY_REGISTER_CLASS(CircleShape2D);
	FOUNDRY_REGISTER_CLASS(RectangleShape2D);
	FOUNDRY_REGISTER_CLASS(CapsuleShape2D);
	FOUNDRY_REGISTER_CLASS(ConvexPolygonShape2D);
	FOUNDRY_REGISTER_CLASS(ConcavePolygonShape2D);
#endif // PHYSICS_2D_DISABLED

#if !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)
	FOUNDRY_REGISTER_CLASS(NavigationMesh);
#endif // !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)

#ifndef NAVIGATION_2D_DISABLED
	FOUNDRY_REGISTER_CLASS(NavigationMeshSourceGeometryData2D);
	FOUNDRY_REGISTER_CLASS(NavigationPolygon);
	FOUNDRY_REGISTER_CLASS(NavigationRegion2D);
	FOUNDRY_REGISTER_CLASS(NavigationAgent2D);
	FOUNDRY_REGISTER_CLASS(NavigationObstacle2D);
	FOUNDRY_REGISTER_CLASS(NavigationLink2D);
	FOUNDRY_REGISTER_CLASS(PolygonPathFinder);

	OS::get_singleton()->yield(); // may take time to init

	// 2D nodes that support navmesh baking need to server register their source geometry parsers.
	MeshInstance2D::navmesh_parse_init();
	MultiMeshInstance2D::navmesh_parse_init();
	NavigationObstacle2D::navmesh_parse_init();
	Polygon2D::navmesh_parse_init();
	TileMapLayer::navmesh_parse_init();
#ifndef PHYSICS_2D_DISABLED
	StaticBody2D::navmesh_parse_init();
#endif // PHYSICS_2D_DISABLED
#endif // NAVIGATION_2D_DISABLED

#ifndef NAVIGATION_3D_DISABLED
	// 3D nodes that support navmesh baking need to server register their source geometry parsers.
	MeshInstance3D::navmesh_parse_init();
	MultiMeshInstance3D::navmesh_parse_init();
	NavigationObstacle3D::navmesh_parse_init();
#ifndef PHYSICS_3D_DISABLED
	StaticBody3D::navmesh_parse_init();
#endif // PHYSICS_3D_DISABLED
#endif // NAVIGATION_3D_DISABLED

#if !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)
	OS::get_singleton()->yield(); // may take time to init
#endif // !defined(NAVIGATION_2D_DISABLED) || !defined(NAVIGATION_3D_DISABLED)

	FOUNDRY_REGISTER_ABSTRACT_CLASS(SceneState);
	FOUNDRY_REGISTER_CLASS(PackedScene);

	FOUNDRY_REGISTER_CLASS(SceneTree);
	FOUNDRY_REGISTER_ABSTRACT_CLASS(SceneTreeTimer); // sorry, you can't create it

	OS::get_singleton()->yield(); // may take time to init

	for (int i = 0; i < 20; i++) {
		GLOBAL_DEF_BASIC(vformat("%s/layer_%d", PNAME("layer_names/2d_render"), i + 1), "");
		GLOBAL_DEF_BASIC(vformat("%s/layer_%d", PNAME("layer_names/3d_render"), i + 1), "");
	}

	for (int i = 0; i < 32; i++) {
		GLOBAL_DEF_BASIC(vformat("%s/layer_%d", PNAME("layer_names/2d_physics"), i + 1), "");
#ifndef NAVIGATION_2D_DISABLED
		GLOBAL_DEF_BASIC(vformat("%s/layer_%d", PNAME("layer_names/2d_navigation"), i + 1), "");
#endif // NAVIGATION_2D_DISABLED
		GLOBAL_DEF_BASIC(vformat("%s/layer_%d", PNAME("layer_names/3d_physics"), i + 1), "");
#ifndef NAVIGATION_3D_DISABLED
		GLOBAL_DEF_BASIC(vformat("%s/layer_%d", PNAME("layer_names/3d_navigation"), i + 1), "");
#endif // NAVIGATION_3D_DISABLED
	}

	for (int i = 0; i < 32; i++) {
		GLOBAL_DEF_BASIC(vformat("%s/layer_%d", PNAME("layer_names/avoidance"), i + 1), "");
	}

	if (RenderingServer::get_singleton()) {
		// RenderingServer needs to exist for this to succeed.
		ColorPickerShape::init_shaders();
		GraphEdit::init_shaders();
	}

	SceneDebugger::initialize();

	OS::get_singleton()->benchmark_end_measure("Scene", "Register Types");
}

void unregister_scene_types() {
	OS::get_singleton()->benchmark_begin_measure("Scene", "Unregister Types");

	SceneDebugger::deinitialize();

	if constexpr (GD_IS_CLASS_ENABLED(TextureLayered)) {
		ResourceLoader::remove_resource_format_loader(resource_loader_texture_layered);
		resource_loader_texture_layered.unref();
	}

	if constexpr (GD_IS_CLASS_ENABLED(Texture3D)) {
		ResourceLoader::remove_resource_format_loader(resource_loader_texture_3d);
		resource_loader_texture_3d.unref();
	}

	if constexpr (GD_IS_CLASS_ENABLED(CompressedTexture2D)) {
		ResourceLoader::remove_resource_format_loader(resource_loader_stream_texture);
		resource_loader_stream_texture.unref();
	}

	ResourceSaver::remove_resource_format_saver(resource_saver_text);
	resource_saver_text.unref();

	ResourceLoader::remove_resource_format_loader(resource_loader_text);
	resource_loader_text.unref();

	if constexpr (GD_IS_CLASS_ENABLED(Shader)) {
		ResourceSaver::remove_resource_format_saver(resource_saver_shader);
		resource_saver_shader.unref();

		ResourceLoader::remove_resource_format_loader(resource_loader_shader);
		resource_loader_shader.unref();
	}

	if constexpr (GD_IS_CLASS_ENABLED(ShaderInclude)) {
		ResourceSaver::remove_resource_format_saver(resource_saver_shader_include);
		resource_saver_shader_include.unref();

		ResourceLoader::remove_resource_format_loader(resource_loader_shader_include);
		resource_loader_shader_include.unref();
	}

	// StandardMaterial3D is not initialized when 3D is disabled, so it shouldn't be cleaned up either
#ifndef _3D_DISABLED
	BaseMaterial3D::finish_shaders();
	PhysicalSkyMaterial::cleanup_shader();
	PanoramaSkyMaterial::cleanup_shader();
	ProceduralSkyMaterial::cleanup_shader();
	FogMaterial::cleanup_shader();
#endif // _3D_DISABLED

	ParticleProcessMaterial::finish_shaders();
	CanvasItemMaterial::finish_shaders();
	ColorPickerShape::finish_shaders();
	GraphEdit::finish_shaders();
	SceneStringNames::free();

	OS::get_singleton()->benchmark_end_measure("Scene", "Unregister Types");
}

void register_scene_singletons() {
	OS::get_singleton()->benchmark_begin_measure("Scene", "Register Singletons");

	FOUNDRY_REGISTER_CLASS(ThemeDB);

	Engine::get_singleton()->add_singleton(Engine::Singleton("ThemeDB", ThemeDB::get_singleton()));

	OS::get_singleton()->benchmark_end_measure("Scene", "Register Singletons");
}
