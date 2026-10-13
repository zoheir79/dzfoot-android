extends SceneTree

var HAIR_S := Vector3(1.0, 1.0, 1.0)
var HAIR_O := Vector3(0, 0, 0)
var BEARD_S := Vector3(1.0, 1.0, 1.0)
var BEARD_O := Vector3(0, 0, 0)

func _tint(n: Node, mat: Material) -> void:
	if n is MeshInstance3D and (n as MeshInstance3D).mesh:
		for s in (n as MeshInstance3D).mesh.get_surface_count():
			(n as MeshInstance3D).set_surface_override_material(s, mat)
	for c in n.get_children():
		_tint(c, mat)

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() >= 8:
		HAIR_S = Vector3(float(args[0]), float(args[1]), float(args[2]))
		HAIR_O = Vector3(float(args[3]), float(args[4]), float(args[5]))
		BEARD_S = Vector3(float(args[6]), float(args[6]), float(args[6]))
		BEARD_O = Vector3(0, float(args[7]), float(args[8]) if args.size() >= 9 else 0.0)
	var vp := SubViewport.new()
	vp.size = Vector2i(1600, 500)
	vp.own_world_3d = true
	vp.transparent_bg = false
	root.add_child(vp)
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.3, 0.5, 0.3)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.7, 0.7, 0.7)
	var we := WorldEnvironment.new()
	we.environment = env
	vp.add_child(we)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-50, -30, 0)
	vp.add_child(light)
	var base_ps := load("res://models/player_base.glb") as PackedScene
	var hairs := ["short", "long", "mohawk", "curly", "ponytail", "bald"]
	var tones := [Color8(242, 209, 183), Color8(165, 122, 81), Color8(45, 28, 15)]
	for i in 6:
		var p := base_ps.instantiate() as Node3D
		vp.add_child(p)
		p.position = Vector3(i * 0.5, 0, 0)
		var neck := p.find_child("neck", true, false) as Node3D
		# face material
		for mi in p.find_children("*", "MeshInstance3D", true, false):
			var m := mi as MeshInstance3D
			if m.name.to_lower().contains("neck"):
				for s in m.mesh.get_surface_count():
					var src := m.mesh.surface_get_material(s)
					var mat := StandardMaterial3D.new()
					if src is BaseMaterial3D:
						mat.albedo_texture = (src as BaseMaterial3D).albedo_texture
					if src.resource_name == "face":
						var t: Color = tones[i % 3]
						var b := Color8(209, 152, 119)
						mat.albedo_color = Color(t.r / b.r, t.g / b.g, t.b / b.b)
					else:
						mat.albedo_color = tones[i % 3]
					m.set_surface_override_material(s, mat)
		if hairs[i] != "bald":
			var h := (load("res://models/hair_%s.glb" % hairs[i]) as PackedScene).instantiate() as Node3D
			var hm := StandardMaterial3D.new()
			hm.albedo_color = Color(0.08, 0.06, 0.05) if i % 2 == 0 else Color(0.5, 0.3, 0.1)
			_tint(h, hm)
			h.scale = HAIR_S
			h.position = HAIR_O
			neck.add_child(h)
		if i % 2 == 1 or i == 4:
			var b := (load("res://models/beard_%s.glb" % ["short", "full", "stubble"][i % 3]) as PackedScene).instantiate() as Node3D
			var bm := StandardMaterial3D.new()
			bm.albedo_color = Color(0.08, 0.06, 0.05)
			_tint(b, bm)
			b.scale = BEARD_S
			b.position = BEARD_O
			neck.add_child(b)
	var cam := Camera3D.new()
	vp.add_child(cam)
	cam.look_at_from_position(Vector3(1.25, 1.75, 2.4), Vector3(1.25, 1.6, 0))
	cam.fov = 30
	cam.current = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	for k in 8:
		await process_frame
	await RenderingServer.frame_post_draw
	var img := vp.get_texture().get_image()
	img.save_png("e:/dzfoot/head_shot.png")
	quit()
