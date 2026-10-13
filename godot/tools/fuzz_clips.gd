# Fuzz: generate + play every clip in DZAnimCollection on the real player scene.
# Reproduces the live path: _play_clip -> add_animation -> play() -> mixer eval.
# Crash here = the clip pipeline itself is broken (bad track path -> UAF).
extends SceneTree

const DZAnimCollectionScript = preload("res://scripts/DZAnimCollection.gd")

var collection: RefCounted
var bone_paths := {}
var player: Node3D
var anim_player: AnimationPlayer
var clip_idx := 0
var clip_count := 0
var frames_in_clip := 0
var errors := 0

func _initialize() -> void:
	var player_scene: PackedScene = load("res://models/player_base.glb")
	if player_scene == null:
		print("FUZZ: no player_base.glb, trying alternatives")
		quit(1)
		return
	player = player_scene.instantiate()
	root.add_child(player)
	anim_player = _find_anim(player)
	if anim_player == null:
		print("FUZZ: no AnimationPlayer in player")
		quit(1)
		return
	# Rebuild bone paths like _setup_animation_library
	for anim_name in anim_player.get_animation_list():
		var anim: Animation = anim_player.get_animation(anim_name)
		if anim == null:
			continue
		for i in anim.get_track_count():
			if anim.track_get_type(i) == Animation.TYPE_ROTATION_3D:
				var p: NodePath = anim.track_get_path(i)
				if p.get_name_count() > 0:
					bone_paths[p.get_name(p.get_name_count() - 1)] = p
	print("FUZZ: bone_paths=%d" % bone_paths.size())
	collection = DZAnimCollectionScript.new("res://anim_data")
	clip_count = collection.get_clip_count()
	print("FUZZ: %d clips to test" % clip_count)

func _find_anim(node: Node) -> AnimationPlayer:
	if node is AnimationPlayer:
		return node
	for c in node.get_children():
		var r := _find_anim(c)
		if r:
			return r
	return null

func _process(_delta: float) -> bool:
	frames_in_clip += 1
	if frames_in_clip >= 6:
		frames_in_clip = 0
		clip_idx += 1
		if clip_idx >= clip_count:
			print("FUZZ: DONE all %d clips OK, errors=%d" % [clip_count, errors])
			quit(0)
			return true
		_next_clip()
	return false

func _next_clip() -> void:
	var clip_name := "c%05d" % clip_idx
	if not anim_player.has_animation(clip_name):
		var animation: Animation = collection.get_animation(clip_idx, bone_paths)
		if animation == null:
			print("FUZZ: clip %d -> null animation" % clip_idx)
			return
		animation.loop_mode = Animation.LOOP_LINEAR
		anim_player.get_animation_library("").add_animation(clip_name, animation)
	anim_player.play(clip_name)
	if clip_idx % 100 == 0:
		print("FUZZ: progress %d/%d" % [clip_idx, clip_count])
