extends SceneTree

func _init() -> void:
	for n in ["hair_short", "hair_long", "hair_mohawk", "hair_curly", "hair_ponytail", "beard_stubble", "beard_short", "beard_full"]:
		var res = load("res://models/%s.glb" % n)
		if not (res is PackedScene):
			print("[HAIR] %s missing" % n)
			continue
		var inst: Node = (res as PackedScene).instantiate()
		_walk(n, inst, Transform3D.IDENTITY)
	var pb: Node = (load("res://models/player_base.glb") as PackedScene).instantiate()
	var neck := pb.find_child("neck", true, false) as MeshInstance3D
	print("[HAIR] neck local pos=%v aabb=%s" % [neck.position, str(neck.get_aabb())])
	quit()

func _walk(tag: String, n: Node, parent_t: Transform3D) -> void:
	var t := parent_t
	if n is Node3D:
		t = parent_t * (n as Node3D).transform
	if n is MeshInstance3D and (n as MeshInstance3D).mesh:
		var a: AABB = t * (n as MeshInstance3D).mesh.get_aabb()
		print("[HAIR] %s %s aabb pos=%v size=%v" % [tag, n.name, a.position, a.size])
	for c in n.get_children():
		_walk(tag, c, t)
