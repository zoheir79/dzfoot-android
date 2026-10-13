extends SceneTree

func _init() -> void:
	var inst: Node = (load("res://models/stadium_test.glb") as PackedScene).instantiate()
	_walk(inst, Transform3D.IDENTITY)
	quit()

func _walk(n: Node, parent_t: Transform3D) -> void:
	var t := parent_t
	if n is Node3D:
		t = parent_t * (n as Node3D).transform
	if n is MeshInstance3D and (n as MeshInstance3D).mesh:
		var a: AABB = t * (n as MeshInstance3D).mesh.get_aabb()
		if (a.size.x > 40.0 and a.size.z < 12.0) or (a.size.z > 40.0 and a.size.x < 12.0):
			print("[STAND] %s pos=%v size=%v" % [n.name, a.position, a.size])
	for c in n.get_children():
		_walk(c, t)
