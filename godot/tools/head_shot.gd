extends SceneTree

func _init() -> void:
	var vp := SubViewport.new()
	vp.size = Vector2i(1600, 560)
	vp.own_world_3d = true
	vp.transparent_bg = false
	root.add_child(vp)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.24, 0.35, 0.20)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.88, 0.91, 0.96)
	env.ambient_light_energy = 0.36
	var we := WorldEnvironment.new()
	we.environment = env
	vp.add_child(we)
	var light := DirectionalLight3D.new()
	light.light_color = Color(1.0, 0.95, 0.86)
	light.light_energy = 0.95
	light.rotation_degrees = Vector3(-38, -25, 0)
	vp.add_child(light)

	var scene_scr := load("res://scripts/DZFootScene.gd").new() as Node3D
	for n in ["hair_short", "hair_long", "hair_mohawk", "hair_curly", "hair_ponytail"]:
		scene_scr._hair_templates.append((load("res://models/%s.glb" % n) as PackedScene).instantiate())
	for n in ["beard_stubble", "beard_short", "beard_full"]:
		scene_scr._beard_templates.append((load("res://models/%s.glb" % n) as PackedScene).instantiate())
	var base_ps := load("res://models/player_base.glb") as PackedScene
	var skin_indices := [0, 1, 2, 3, 4, 6]
	var beard_styles := [0, 1, 2, 3, 2, 3]
	var hair_cols := [2, 0, 1, 0, 3, 0]
	var kits := [
		[Color(0.95, 0.95, 0.97), Color(0.08, 0.28, 0.78)],
		[Color(0.88, 0.10, 0.14), Color(0.95, 0.95, 0.97)],
		[Color(0.22, 0.78, 0.30), Color(0.10, 0.10, 0.12)],
		[Color(0.95, 0.95, 0.97), Color(0.84, 0.08, 0.12)],
		[Color(0.08, 0.52, 0.22), Color(0.82, 0.08, 0.12)],
		[Color(0.95, 0.82, 0.08), Color(0.08, 0.50, 0.20)],
	]
	for i in 6:
		var p := base_ps.instantiate() as Node3D
		vp.add_child(p)
		p.position = Vector3(i * 0.48, 0, 0)
		var sk_idx: int = skin_indices[i]
		var sk_tex := load("res://textures/skin_%d.png" % sk_idx) as Texture2D
		scene_scr._apply_kit_to_meshes(p, kits[i][0], kits[i][1])
		scene_scr._apply_skin_texture(p, sk_tex, sk_idx, beard_styles[i], hair_cols[i])
		scene_scr._attach_head_accessory(p, i, hair_cols[i], beard_styles[i])
		scene_scr._attach_back_number(p, str(i + 7))
	var cam := Camera3D.new()
	vp.add_child(cam)
	cam.look_at_from_position(Vector3(1.20, 0.95, 3.4), Vector3(1.20, 0.92, 0))
	cam.fov = 34
	cam.current = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	for k in 8:
		await process_frame
	await RenderingServer.frame_post_draw
	var img := vp.get_texture().get_image()
	img.save_png("e:/dzfoot/head_shot.png")
	quit()
