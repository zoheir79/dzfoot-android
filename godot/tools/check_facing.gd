extends SceneTree

# Determines which local axis the player model faces (toes vs ankles, nose/face).

func _initialize() -> void:
	var scn: PackedScene = load("res://models/player_base.glb")
	var p: Node3D = scn.instantiate()
	root.add_child(p)
	_walk(p, p)
	quit(0)

func _walk(n: Node, root_node: Node) -> void:
	if n is Node3D and n != root_node:
		var t := Transform3D.IDENTITY
		var cur: Node = n
		while cur != null and cur != root_node:
			if cur is Node3D:
				t = (cur as Node3D).transform * t
			cur = cur.get_parent()
		if n is MeshInstance3D and (n as MeshInstance3D).mesh:
			var a: AABB = (n as MeshInstance3D).mesh.get_aabb()
			var c: Vector3 = t * (a.position + a.size * 0.5)
			print("FACING: MESH ", n.name, " center=", c, " size=", a.size)
		else:
			print("FACING: NODE ", n.get_class(), " ", n.name, " pos=", t.origin)
	for c in n.get_children():
		_walk(c, root_node)

func _find_skel(n: Node) -> Skeleton3D:
	if n is Skeleton3D:
		return n
	for c in n.get_children():
		var r := _find_skel(c)
		if r:
			return r
	return null

func _meshes(n: Node) -> Array:
	var out: Array = []
	if n is MeshInstance3D:
		out.append(n)
	for c in n.get_children():
		out.append_array(_meshes(c))
	return out
