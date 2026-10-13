extends SceneTree

func _init() -> void:
	var models := ["pitch", "goals", "ball", "player_base", "stadium_test"]
	for name in models:
		var path := "res://models/%s.glb" % name
		if not ResourceLoader.exists(path):
			print("[DUMP] %s: NOT FOUND" % name)
			continue
		var res = load(path)
		if not (res is PackedScene):
			print("[DUMP] %s: not a PackedScene" % name)
			continue
		var inst: Node = (res as PackedScene).instantiate()
		print("[DUMP] === %s ===" % name)
		_walk(inst, 0, inst)
	quit()

func _walk(n: Node, depth: int, root: Node) -> void:
	var indent := "  ".repeat(depth)
	if n is MeshInstance3D:
		var mi := n as MeshInstance3D
		var xform := _world_xform(mi, root)
		var aabb := mi.mesh.get_aabb() if mi.mesh else AABB()
		var pos := xform.origin
		print("[DUMP] %sMESH %s | pos=%v size=%v | surfaces=%d" % [indent, mi.name, pos, aabb.size, mi.mesh.get_surface_count() if mi.mesh else 0])
		if mi.mesh:
			for s in mi.mesh.get_surface_count():
				var m := mi.get_active_material(s)
				var desc := "null"
				if m is BaseMaterial3D:
					var bm := m as BaseMaterial3D
					var tex := "no_tex"
					if bm.albedo_texture:
						tex = bm.albedo_texture.resource_path.get_file()
						if tex == "":
							tex = "embedded"
					desc = "%s albedo=%s tex=%s vc_albedo=%s" % [m.get_class(), bm.albedo_color, tex, bm.vertex_color_use_as_albedo]
				elif m != null:
					desc = m.get_class()
				print("[DUMP] %s  surf%d: %s" % [indent, s, desc])
	elif n is Skeleton3D or n is AnimationPlayer:
		print("[DUMP] %s%s %s" % [indent, n.get_class(), n.name])
	for c in n.get_children():
		_walk(c, depth + 1, root)

func _world_xform(n: Node3D, root: Node) -> Transform3D:
	var t := Transform3D.IDENTITY
	var cur: Node = n
	while cur != null and cur != root:
		if cur is Node3D:
			t = (cur as Node3D).transform * t
		cur = cur.get_parent()
	if cur is Node3D:
		t = (cur as Node3D).transform * t
	return t
