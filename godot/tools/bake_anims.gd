# Bake ALL dynamic clips into one binary AnimationLibrary (res://baked/dzdyn.res).
# Clips are identical match to match, so they are generated here at build time
# and simply loaded (not regenerated) when the app starts.
#
# Usage (re-run whenever anim_data/ or models/player_base.glb change):
#   Godot_v4.5.1-stable_win64_console.exe --headless --path <godot dir> \
#       --script res://tools/bake_anims.gd
extends SceneTree

const DZAnimCollectionScript = preload("res://scripts/DZAnimCollection.gd")
const OUT_PATH := "res://baked/dzdyn.res"

func _initialize() -> void:
	var player_scene: PackedScene = load("res://models/player_base.glb")
	if player_scene == null:
		push_error("BAKE: res://models/player_base.glb not loadable")
		quit(1)
		return
	var player: Node3D = player_scene.instantiate()
	root.add_child(player)
	var anim_player := _find_anim(player)
	if anim_player == null:
		push_error("BAKE: no AnimationPlayer in player_base.glb")
		quit(1)
		return
	# Same bone-path extraction as DZFootScene._setup_animation_library.
	var bone_paths := {}
	for lib_name in anim_player.get_animation_library_list():
		var lib := anim_player.get_animation_library(lib_name)
		for anim_name in lib.get_animation_list():
			var anim: Animation = lib.get_animation(anim_name)
			for i in anim.get_track_count():
				if anim.track_get_type(i) == Animation.TYPE_ROTATION_3D:
					var p: NodePath = anim.track_get_path(i)
					if p.get_name_count() > 0:
						bone_paths[p.get_name(p.get_name_count() - 1)] = p
	if bone_paths.size() < 5:
		push_error("BAKE: only %d bone paths found" % bone_paths.size())
		quit(1)
		return
	var collection: RefCounted = DZAnimCollectionScript.new("res://anim_data")
	var total: int = collection.get_clip_count()
	var out_lib := AnimationLibrary.new()
	var t0 := Time.get_ticks_msec()
	var ok := 0
	for cid in range(total):
		var a: Animation = collection.get_animation(cid, bone_paths)
		if a != null:
			out_lib.add_animation("c%05d" % cid, a)
			ok += 1
	print("BAKE: generated %d/%d clips in %dms" % [ok, total, Time.get_ticks_msec() - t0])
	out_lib.set_meta("clip_count", total)
	out_lib.set_meta("bone_count", bone_paths.size())
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("res://baked"))
	var err := ResourceSaver.save(out_lib, OUT_PATH, ResourceSaver.FLAG_COMPRESS)
	if err != OK:
		push_error("BAKE: save failed err=%d" % err)
		quit(1)
		return
	var size := FileAccess.get_file_as_bytes(OUT_PATH).size()
	print("BAKE: wrote %s (%.1f MB)" % [OUT_PATH, size / 1048576.0])
	var t1 := Time.get_ticks_msec()
	var back := ResourceLoader.load(OUT_PATH, "AnimationLibrary", ResourceLoader.CACHE_MODE_IGNORE) as AnimationLibrary
	print("BAKE: reload check %d clips in %dms" % [back.get_animation_list().size() if back else -1, Time.get_ticks_msec() - t1])
	quit(0 if back != null and back.get_animation_list().size() == ok else 1)

func _find_anim(node: Node) -> AnimationPlayer:
	if node is AnimationPlayer:
		return node
	for c in node.get_children():
		var r := _find_anim(c)
		if r:
			return r
	return null
