extends SceneTree

func _aabbs(n: Node, depth: int = 0) -> void:
	if n is MeshInstance3D and (n as MeshInstance3D).mesh:
		var mi := n as MeshInstance3D
		print("  ", "  ".repeat(depth), n.name, " aabb=", mi.get_aabb(), " surfs=", mi.mesh.get_surface_count(), " xf=", mi.transform.origin)
	for c in n.get_children():
		_aabbs(c, depth + 1)

func _init() -> void:
	for f in ["player_base", "player_base_backup", "hair_short", "hair_long", "hair_mohawk", "hair_curly", "hair_ponytail", "beard_stubble", "beard_short", "beard_full"]:
		var ps := load("res://models/%s.glb" % f) as PackedScene
		print("[HEAD] ", f)
		if ps:
			var inst := ps.instantiate()
			if f.begins_with("player_base"):
				var neck := inst.find_child("neck", true, false)
				if neck:
					print("  neck pos=", (neck as Node3D).position)
					_aabbs(neck)
			else:
				_aabbs(inst)
	quit()
