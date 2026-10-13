# Bench: time generation of ALL 1452 clips (decides if eager pre-gen is viable)
extends SceneTree

const DZAnimCollectionScript = preload("res://scripts/DZAnimCollection.gd")

func _initialize() -> void:
	var player_scene: PackedScene = load("res://models/player_base.glb")
	if player_scene == null:
		print("BENCH: no player_base.glb")
		quit(1)
		return
	var player: Node3D = player_scene.instantiate()
	root.add_child(player)
	var anim_player := _find_anim(player)
	if anim_player == null:
		print("BENCH: no AnimationPlayer")
		quit(1)
		return
	var bone_paths := {}
	for anim_name in anim_player.get_animation_list():
		var anim: Animation = anim_player.get_animation(anim_name)
		if anim == null:
			continue
		for i in anim.get_track_count():
			if anim.track_get_type(i) == Animation.TYPE_ROTATION_3D:
				var p: NodePath = anim.track_get_path(i)
				if p.get_name_count() > 0:
					bone_paths[p.get_name(p.get_name_count() - 1)] = p
	print("BENCH: bone_paths=%d" % bone_paths.size())
	var collection: RefCounted = DZAnimCollectionScript.new("res://anim_data")
	var n: int = collection.get_clip_count()
	var t0 := Time.get_ticks_msec()
	var ok := 0
	var bad := 0
	for cid in range(n):
		var a: Animation = collection.get_animation(cid, bone_paths)
		if a != null:
			ok += 1
		else:
			bad += 1
	var dt := Time.get_ticks_msec() - t0
	print("BENCH: generated %d clips in %dms (bad=%d) => %.2f ms/clip" % [ok, dt, bad, float(dt) / maxf(1.0, ok)])
	quit(0)

func _find_anim(node: Node) -> AnimationPlayer:
	if node is AnimationPlayer:
		return node
	for c in node.get_children():
		var r := _find_anim(c)
		if r:
			return r
	return null
