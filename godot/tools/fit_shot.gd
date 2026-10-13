extends SceneTree

const HAIRS := ["short", "long", "mohawk", "curly", "ponytail"]
const BEARDS := ["stubble", "short", "full"]

func _tint(n: Node, mat: Material) -> void:
	if n is MeshInstance3D and (n as MeshInstance3D).mesh:
		for s in (n as MeshInstance3D).mesh.get_surface_count():
			(n as MeshInstance3D).set_surface_override_material(s, mat)
	for c in n.get_children():
		_tint(c, mat)

func _aabb(n: Node) -> AABB:
	var r := AABB()
	var first := true
	for m in n.find_children("*", "MeshInstance3D", true, false):
		var mi := m as MeshInstance3D
		var a := mi.transform * mi.mesh.get_aabb()
		r = a if first else r.merge(a)
		first = false
	return r

func _fit(n: Node3D, tmin: Vector3, tmax: Vector3) -> void:
	var a := _aabb(n)
	var sc := (tmax - tmin) / a.size
	n.scale = sc
	n.position = tmin - a.position * sc

func _init() -> void:
	var cfg := {}
	var f := FileAccess.open("res://tools/fit_cfg.json", FileAccess.READ)
	cfg = JSON.parse_string(f.get_as_text())
	var vp := SubViewport.new()
	vp.size = Vector2i(2000, 900)
	vp.own_world_3d = true
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
	var items: Array = []
	for h in HAIRS:
		items.append(["hair", h])
	for b in BEARDS:
		items.append(["beard", b])
	var head_y := 0.0
	for i in items.size():
		for view in 1:
			var p := base_ps.instantiate() as Node3D
			vp.add_child(p)
			p.position = Vector3(i * 0.42, -view * 0.5, 0)
			p.rotation_degrees.y = float(OS.get_cmdline_user_args()[0])
			var neck := p.find_child("neck", true, false) as Node3D
			head_y = 1.9
			for mi in p.find_children("*", "MeshInstance3D", true, false):
				var m := mi as MeshInstance3D
				if m.name.to_lower().contains("neck"):
					for s in m.mesh.get_surface_count():
						var mat := StandardMaterial3D.new()
						mat.albedo_color = Color8(209, 152, 119)
						m.set_surface_override_material(s, mat)
			var it: Array = items[i]
			var key: String = "%s_%s" % [it[0], it[1]]
			var node := (load("res://models/%s_%s.glb" % [it[0], it[1]]) as PackedScene).instantiate() as Node3D
			var hm := StandardMaterial3D.new()
			hm.albedo_color = Color(0.08, 0.06, 0.05) if it[0] == "hair" else Color(0.45, 0.2, 0.05)
			hm.cull_mode = BaseMaterial3D.CULL_DISABLED
			_tint(node, hm)
			var c: Array = cfg[key]
			_fit(node, Vector3(c[0], c[1], c[2]), Vector3(c[3], c[4], c[5]))
			neck.add_child(node)
	var cam := Camera3D.new()
	vp.add_child(cam)
	var cx := (items.size() - 1) * 0.21
	cam.look_at_from_position(Vector3(cx, 1.5, 3.2), Vector3(cx, 1.5, 0))
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = 2.0
	cam.current = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	for k in 8:
		await process_frame
	await RenderingServer.frame_post_draw
	vp.get_texture().get_image().save_png("e:/dzfoot/fit_shot_%s.png" % OS.get_cmdline_user_args()[0])
	quit()



