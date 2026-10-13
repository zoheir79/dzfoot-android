extends SceneTree
## Dev-time verification: compares the client-side DZAnimCollection port
## against the server-side AnimCollection export (GF_EXPORT_ANIMS fixture).
##
## Usage:
##   Godot_v4.5.1-stable_win64_console.exe --headless --path <godot project> \
##       --script res://tools/test_anim_collection.gd

const REGISTRY := "e:/dzfoot/anim_export/anim_registry.txt"
const EXPORT_DIR := "e:/dzfoot/anim_export"


func _init() -> void:
	var col = load("res://scripts/DZAnimCollection.gd").new("res://anim_data")

	var reg_lines: Array = []
	var f := FileAccess.open(REGISTRY, FileAccess.READ)
	if f == null:
		push_error("Cannot open server registry " + REGISTRY)
		quit(1)
		return
	while not f.eof_reached():
		var l := f.get_line()
		if not l.is_empty():
			reg_lines.append(l)
	f.close()

	var n_autogen := 0
	for l in reg_lines:
		if l.find("autogen") != -1:
			n_autogen += 1
	var server_specs: int = n_autogen / 2
	var server_disk: int = (reg_lines.size() - n_autogen) / 2

	print("server: total=%d autogen_specs=%d disk_files=%d" % [reg_lines.size(), server_specs, server_disk])
	print("client: total=%d autogen_specs=%d disk_files=%d" % [col.get_clip_count(), col._specs.size(), col._disk_files.size()])

	# --- 1. autogen sequence (server line i*2+1 = ORIGINAL of spec i) ---
	var mismatches := 0
	var spec_count: int = col._specs.size()
	for i in range(mini(spec_count, server_specs)):
		var spec: Dictionary = col._specs[i]
		var name_c := "autogen [v%d b%d] => [v%d b%d a%d]" % [
			col._velocity_id(spec.t1.in_vel), int(spec.t1.in_body_angle / PI * 180.0),
			col._velocity_id(spec.t2.out_vel), int(spec.t2.out_body_angle / PI * 180.0),
			int(spec.angle / PI * 180.0)]
		var parts := (reg_lines[i * 2 + 1] as String).split("\t")
		var name_s: String = parts[1]
		if name_s != name_c:
			mismatches += 1
			if mismatches <= 8:
				print("  MISMATCH @%d:\n    server: %s\n    client: %s" % [i, name_s, name_c])
	print("autogen name mismatches: %d / %d" % [mismatches, spec_count])

	# --- 2. disk file sequence ---
	var disk_start := n_autogen
	var dm := 0
	var client_disk: int = col._disk_files.size()
	for i in range(mini(client_disk, server_disk)):
		var parts := (reg_lines[disk_start + i * 2] as String).split("\t")
		var rel: String = (parts[1] as String).replace("media/animations/", "")
		var client_rel: String = col._disk_files[i]
		if rel != client_rel:
			dm += 1
			if dm <= 8:
				print("  DISK MISMATCH @%d: server=%s client=%s" % [i, rel, client_rel])
	print("disk mismatches: %d / %d" % [dm, client_disk])

	# --- 3. deep keyframe comparison (autogen clips) ---
	# clipId 1 = spec 0 original; clipId 3 = spec 1 original
	var deep_ok := true
	for spec_i in [0, 1, 5, 42]:
		if spec_i >= spec_count:
			continue
		var clip_id: int = spec_i * 2 + 1
		if not _deep_compare(col, col._specs[spec_i], clip_id):
			deep_ok = false
	print("deep autogen keyframe comparison: %s" % ("OK" if deep_ok else "FAILED"))

	# --- 4. deep comparison of a disk clip (original + mirror) ---
	var disk_first_clip: int = server_specs * 2
	if server_disk > 0:
		_deep_compare_disk(col, disk_first_clip, reg_lines, disk_start)
		_deep_compare_disk(col, disk_first_clip + 1, reg_lines, disk_start)

	quit(0 if (mismatches == 0 and dm == 0 and deep_ok) else 1)


func _deep_compare(col, spec: Dictionary, clip_id: int) -> bool:
	var tracks: Dictionary = col._generate_autogen(spec)
	return _compare_tracks_with_export(col, tracks, clip_id, false)


func _deep_compare_disk(col, clip_id: int, reg_lines: Array, disk_start: int) -> void:
	# clip_id - n_autogen_lines = index into the disk section; /2 = file, %2 = mirror
	var idx: int = clip_id - _count_autogen(reg_lines)
	var anim: Dictionary = col._parse_anim_file("res://anim_data/" + col._disk_files[idx / 2])
	var mirrored := idx % 2 == 1
	var tracks: Dictionary = anim.tracks
	if mirrored:
		tracks = col._mirror_tracks(tracks)
	if _compare_tracks_with_export(col, tracks, clip_id, mirrored):
		print("deep disk clip %d (%s): OK" % [clip_id, "mirror" if mirrored else "orig"])
	else:
		print("deep disk clip %d (%s): FAILED" % [clip_id, "mirror" if mirrored else "orig"])


func _count_autogen(reg_lines: Array) -> int:
	var n := 0
	for l in reg_lines:
		if l.find("autogen") != -1:
			n += 1
	return n


func _compare_tracks_with_export(col, tracks: Dictionary, clip_id: int, mirrored: bool) -> bool:
	var path := "%s/c%05d.anim" % [EXPORT_DIR, clip_id]
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		push_error("Cannot open " + path)
		return false
	# parse export (reference)
	var ref := {}
	while not f.eof_reached():
		var line := f.get_line().strip_edges()
		if line.is_empty() or line.begins_with("<") or line.begins_with("extension"):
			break
		var parts := line.split(",")
		var node: String = parts[0].strip_edges()
		var kfs: Array = []
		var i := 1
		var vals := 3 if node == "player" else 4
		while i + vals <= parts.size():
			var frame := int(parts[i])
			var vals_arr := []
			for k in vals:
				vals_arr.append(float(parts[i + 1 + k].strip_edges()))
			kfs.append({"f": frame, "v": vals_arr})
			i += 1 + vals
		ref[node] = kfs
	f.close()

	var max_diff := 0.0
	var compared := 0
	for bone in ref:
		if bone == "player":
			continue
		if not tracks.has(bone):
			print("  clip %d: missing bone %s" % [clip_id, bone])
			return false
		var client_kfs: Array = tracks[bone]
		var ref_kfs: Array = ref[bone]
		if client_kfs.size() != ref_kfs.size():
			print("  clip %d bone %s: keyframe count %d != server %d" % [clip_id, bone, client_kfs.size(), ref_kfs.size()])
			return false
		for i in ref_kfs.size():
			var rk: Dictionary = ref_kfs[i]
			var ck: Dictionary = client_kfs[i]
			if int(rk.f) != int(ck.f):
				print("  clip %d bone %s: frame %d != %d" % [clip_id, bone, rk.f, ck.f])
				return false
			var q: Quaternion = ck.q
			var diff := maxf(
				absf(rk.v[0] - q.x),
				maxf(absf(rk.v[1] - q.y),
				maxf(absf(rk.v[2] - q.z), absf(rk.v[3] - q.w))))
			max_diff = maxf(max_diff, diff)
			compared += 1
	print("  clip %d (%s): %d keyframes compared, max quaternion diff = %.6f" % [clip_id, "mirror" if mirrored else "orig", compared, max_diff])
	return max_diff < 0.001
