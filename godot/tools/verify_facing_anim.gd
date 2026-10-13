extends SceneTree

# Verifies visual facing of an ANIMATED player vs the direction the server sends.
# Server: dir=(dx,dy) GF, rotY=atan2(dx,dy). Expected Godot world forward = (dx,0,-dy) = (sin rotY, 0, -cos rotY).
# Render rule under test: player.rotation.y = ROT_SIGN * rotY + ROT_OFFSET

const DZAnimCollectionScript = preload("res://scripts/DZAnimCollection.gd")

func _initialize() -> void:
	var scn: PackedScene = load("res://models/player_base.glb")
	var p: Node3D = scn.instantiate()
	root.add_child(p)
	await process_frame
	await process_frame
	var ap: AnimationPlayer = _find_ap(p)
	var bone_paths := {}
	for an in ap.get_animation_list():
		var a: Animation = ap.get_animation(an)
		for i in a.get_track_count():
			if a.track_get_type(i) == Animation.TYPE_ROTATION_3D:
				var np: NodePath = a.track_get_path(i)
				if np.get_name_count() > 0:
					bone_paths[np.get_name(np.get_name_count() - 1)] = np
	var coll = DZAnimCollectionScript.new("res://anim_data")
	var lib := AnimationLibrary.new()
	var ids: Array = []
	var n: int = coll.get_clip_count()
	for k in range(0, n, 29):
		ids.append(k)
	for cid in ids:
		lib.add_animation("c%d" % cid, coll.get_animation(cid, bone_paths))
	ap.add_animation_library("t", lib)
	var ls := p.find_child("left_shoulder", true, false) as Node3D
	var rs := p.find_child("right_shoulder", true, false) as Node3D
	var rots := [0.0, 0.7, 1.57, 2.5, 3.14, -1.0, -2.2]
	for mode in ["neg_rotY (current)", "pi_minus_rotY"]:
		var sum := 0.0
		var cnt := 0
		var bad := 0
		for cid in ids:
			ap.play("t/c%d" % cid)
			ap.advance(0.12)
			for ry in rots:
				if mode == "neg_rotY (current)":
					p.rotation.y = -ry
				else:
					p.rotation.y = PI - ry
				var expect := Vector3(sin(ry), 0.0, -cos(ry))
				var r: Vector3 = rs.global_position - ls.global_position
				var fwd := Vector3.UP.cross(r)
				fwd.y = 0.0
				if fwd.length() < 0.001:
					continue
				var c := fwd.normalized().dot(expect)
				sum += c
				cnt += 1
				if c < 0.0:
					bad += 1
		print("VERIFYFACE: mode=%s samples=%d avgCos=%.3f neg=%d" % [mode, cnt, sum / maxf(1.0, cnt), bad])
	quit(0)

func _find_ap(n: Node) -> AnimationPlayer:
	if n is AnimationPlayer:
		return n
	for c in n.get_children():
		var r := _find_ap(c)
		if r:
			return r
	return null
