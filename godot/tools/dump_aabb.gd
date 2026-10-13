extends SceneTree

func _init() -> void:
	var models := ["pitch", "goals", "ball", "player_base", "stadium_test", "stadium_minimal"]
	for name in models:
		var path := "res://models/%s.glb" % name
		if not ResourceLoader.exists(path):
			print("[AABB] %s: NOT FOUND" % name)
			continue
		var res = load(path)
		if not (res is PackedScene):
			print("[AABB] %s: not a PackedScene" % name)
			continue
		var inst: Node = (res as PackedScene).instantiate()
		var aabb := _merged_aabb(inst)
		print("[AABB] %s: size=%s center=%s meshes=%d" % [name, str(aabb.size), str(aabb.position + aabb.size * 0.5), _count_meshes(inst)])
		inst.free()
	quit()

func _merged_aabb(node: Node) -> AABB:
	var result := AABB()
	var found := false
	var stack: Array = [node]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		if n is MeshInstance3D:
			var mi := n as MeshInstance3D
			var xform: Transform3D = mi.transform
			var p: Node = mi.get_parent()
			while p and p != node:
				if p is Node3D:
					xform = (p as Node3D).transform * xform
				p = p.get_parent()
			var a: AABB = xform * mi.get_aabb()
			if not found:
				result = a
				found = true
			else:
				result = result.merge(a)
	return result

func _count_meshes(node: Node) -> int:
	var count := 0
	var stack: Array = [node]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		if n is MeshInstance3D:
			count += 1
	return count
