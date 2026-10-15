extends RefCounted
## DZAnimCollection — dynamic animation generation, Android port of the
## desktop GameplayFootball AnimCollection.
##
## Replicates the desktop pipeline exactly:
##   - loads the .anim templates shipped in the pck (res://anim_data/templates)
##   - replays GenerateAutoAnims(): every legal (template1, template2, direction)
##     combination generates a 25-frame blended clip (slerp + body rotation + lean)
##   - mirrors every clip (left/right swap + quaternion y/z negation)
##   - parses the remaining disk .anim clips on demand
##
## clipId == index into the desktop AnimCollection:
##   [0 .. 2A-1]  autogen clips, ordered (t1 asc, t2 asc, direction 0..8),
##                each combo contributing [mirrored, original]
##   [2A .. ]     disk clips (sorted paths, luxury/templates excluded),
##                each file contributing [original, mirrored]
##
## The server sends this clipId (anim low byte | enumVelocity high byte).
##
## Poses are generated in GF Z-up space, mirrored there, then converted to
## Godot Y-up by quaternion conjugation (B * q * B^-1, B = rotX(-90°)).
##
## Clips are generated lazily on first use and cached (shared Animation
## resources across all player AnimationPlayers).

const GF_FPS := 100.0
const GEN_FRAME_COUNT := 25
const MARGIN := 0.01
const LEAN_AMOUNT := 0.001
const SPRINT_VELOCITY := 8.0

const BONES := [
	"body", "middle", "neck",
	"left_shoulder", "left_elbow", "right_shoulder", "right_elbow",
	"left_thigh", "left_knee", "left_ankle",
	"right_thigh", "right_knee", "right_ankle",
]

# Z-up -> Y-up basis (x, y, z, w)
const B_YUP := Quaternion(-0.7071067811865476, 0.0, 0.0, 0.7071067811865476)
const B_YUP_INV := Quaternion(0.7071067811865476, 0.0, 0.0, 0.7071067811865476)

var _templates: Array = []   # parsed template dictionaries (see _parse_anim_file + meta)
var _specs: Array = []       # legal autogen combinations, in generation order
var _disk_files: Array = []  # sorted relative paths of disk clips (luxury/templates excluded)
var _clip_cache := {}       # clip_id -> Animation
var _root := ""              # anim data root (res://anim_data)
var ready := false


func _init(anim_root := "res://anim_data") -> void:
	_root = anim_root
	_build(anim_root)


# ------------------------------------------------------------------
# Public API
# ------------------------------------------------------------------

## Number of clips in the (virtual) collection — autogen pairs + disk pairs.
func get_clip_count() -> int:
	return _specs.size() * 2 + _disk_files.size() * 2


## Build (or fetch from cache) the Godot Animation for a clipId.
## bone_paths: {bone_name: NodePath} extracted from an existing GLB animation.
func get_animation(clip_id: int, bone_paths: Dictionary) -> Animation:
	if clip_id < 0:
		return null
	if _clip_cache.has(clip_id):
		return _clip_cache[clip_id]
	var animation := _generate_animation(clip_id, bone_paths)
	if animation:
		_clip_cache[clip_id] = animation
	return animation


## Thread-safe worker entry point: skips _clip_cache entirely. All reads hit
## immutable _templates/_specs/_disk_files — safe to call concurrently from
## WorkerThreadPool on ONE shared instance (no per-worker re-parse).
func generate_for_worker(clip_id: int, bone_paths: Dictionary) -> Animation:
	if clip_id < 0:
		return null
	return _generate_animation(clip_id, bone_paths)


func _generate_animation(clip_id: int, bone_paths: Dictionary) -> Animation:
	var tracks: Dictionary
	if clip_id < _specs.size() * 2:
		var spec: Dictionary = _specs[clip_id / 2]
		var mirrored := (clip_id % 2) == 0
		tracks = _generate_autogen(spec)
		if mirrored:
			tracks = _mirror_tracks(tracks)
	else:
		var idx := clip_id - _specs.size() * 2
		var file_idx := idx / 2
		if file_idx >= _disk_files.size():
			return null
		var mirrored := (idx % 2) == 1
		var anim := _parse_anim_file(_root + "/" + _disk_files[file_idx])
		tracks = anim.get("tracks", {})
		if mirrored:
			tracks = _mirror_tracks(tracks)

	var animation := Animation.new()
	var last_time := 0.0
	if tracks.has("player") and bone_paths.has("player"):
		var p_track_idx := animation.add_track(Animation.TYPE_POSITION_3D)
		animation.track_set_path(p_track_idx, bone_paths["player"])
		for kf: Dictionary in tracks["player"]:
			var pv: Vector3 = kf.get("p", Vector3.ZERO)
			var t_p: float = kf.f / GF_FPS
			# Apply vertical height offset (GF Z -> Godot Y); horizontal X/Z comes from server position
			animation.track_insert_key(p_track_idx, t_p, Vector3(0.0, pv.z, 0.0))
			if t_p > last_time:
				last_time = t_p
	for bone in BONES:
		if not tracks.has(bone):
			continue
		var track_idx := animation.add_track(Animation.TYPE_ROTATION_3D)
		animation.track_set_path(track_idx, bone_paths[bone])
		for kf: Dictionary in tracks[bone]:
			var q_yup: Quaternion = (B_YUP * kf.q) * B_YUP_INV
			var t: float = kf.f / GF_FPS
			animation.track_insert_key(track_idx, t, q_yup)
			if t > last_time:
				last_time = t
	animation.length = maxf(last_time, 1.0 / GF_FPS)
	animation.loop_mode = Animation.LOOP_LINEAR
	return animation


func has_clip(clip_id: int) -> bool:
	return clip_id >= 0 and clip_id < get_clip_count()


func find_disk_clip_id(substr: String) -> int:
	for i in range(_disk_files.size()):
		if str(_disk_files[i]).find(substr) != -1:
			return _specs.size() * 2 + i * 2
	return -1


# ------------------------------------------------------------------
# Build: templates, specs, disk file list
# ------------------------------------------------------------------

func _build(anim_root: String) -> void:
	var tmpl_files: Array = _list_anim_files(anim_root + "/templates", false)
	tmpl_files.sort()
	for path in tmpl_files:
		var t := _parse_anim_file(anim_root + "/templates/" + path)
		if not t.is_empty():
			_compute_meta(t)
			_templates.append(t)

	# Replay the GenerateAutoAnims loop: t1 asc, t2 asc, direction 0..8.
	for t1 in _templates:
		for t2 in _templates:
			for direction in 9:
				var spec := _make_spec(t1, t2, direction)
				if not spec.is_empty():
					_specs.append(spec)

	var all_files: Array = _list_anim_files(anim_root, true)
	for rel in all_files:
		if rel.find("luxury") != -1 or rel.find("templates") != -1:
			continue
		_disk_files.append(rel)
	_disk_files.sort()

	ready = true
	print("[DZAnim] Collection built: %d templates, %d autogen specs, %d disk clips, total %d clips" % [
		_templates.size(), _specs.size(), _disk_files.size(), get_clip_count()])


func _list_anim_files(dir_path: String, recursive: bool, base := "") -> Array:
	var out: Array = []
	var dir := DirAccess.open(dir_path)
	if dir == null:
		return out
	dir.list_dir_begin()
	var name := dir.get_next()
	while name != "":
		if name.begins_with("."):
			name = dir.get_next()
			continue
		var rel := name if base.is_empty() else base + "/" + name
		if dir.current_is_dir():
			if recursive:
				out.append_array(_list_anim_files(dir_path + "/" + name, true, rel))
		elif name.ends_with(".anim"):
			out.append(rel)
		name = dir.get_next()
	dir.list_dir_end()
	return out


# ------------------------------------------------------------------
# .anim file parsing (CSV part; XML/extension sections are irrelevant for posing)
# ------------------------------------------------------------------

func _parse_anim_file(path: String) -> Dictionary:
	var tracks := {}
	var frame_count := 0
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		push_warning("[DZAnim] Cannot open " + path)
		return {}
	while not f.eof_reached():
		var line := f.get_line().strip_edges()
		if line.is_empty() or line.begins_with("<") or line.begins_with("extension"):
			break
		var parts := line.split(",")
		if parts.size() < 2:
			continue
		var node_name: String = parts[0].strip_edges()
		var is_player := node_name == "player"
		var vals := 3 if is_player else 4
		var kfs: Array = []
		var i := 1
		while i + vals <= parts.size():
			var frame := int(parts[i])
			if is_player:
				var p := Vector3(
					float(parts[i + 1].strip_edges()),
					float(parts[i + 2].strip_edges()),
					float(parts[i + 3].strip_edges()))
				kfs.append({"f": frame, "q": Quaternion.IDENTITY, "p": p})
			else:
				var q := Quaternion(
					float(parts[i + 1].strip_edges()),
					float(parts[i + 2].strip_edges()),
					float(parts[i + 3].strip_edges()),
					float(parts[i + 4].strip_edges()))
				kfs.append({"f": frame, "q": q, "p": Vector3.ZERO})
			if frame >= frame_count:
				frame_count = frame + 1
			i += 1 + vals
		kfs.sort_custom(func(a, b): return a.f < b.f)
		tracks[node_name] = kfs
	f.close()
	return {"tracks": tracks, "frame_count": frame_count}


# ------------------------------------------------------------------
# Template metadata (ports Animation::GetIncoming/OutgoingVelocity etc.)
# ------------------------------------------------------------------

func _compute_meta(t: Dictionary) -> void:
	var player: Array = t.tracks.get("player", [])
	var body: Array = t.tracks.get("body", [])

	# incoming / outgoing movement + velocity (from player track keyframes)
	if player.size() > 1:
		var p0: Dictionary = player[0]
		var p1: Dictionary = player[1]
		var mv: Vector3 = _movement_between(p0, p1)
		t.in_movement = mv
		t.in_vel = _quantize_velocity(mv.length())
		var pn1: Dictionary = player[player.size() - 1]
		var pn2: Dictionary = player[player.size() - 2]
		mv = _movement_between(pn2, pn1)
		t.out_movement = mv
		t.out_vel = _quantize_velocity(mv.length())
	else:
		t.in_movement = Vector3.ZERO
		t.in_vel = 0.0
		t.out_movement = Vector3.ZERO
		t.out_vel = 0.0

	# incoming body angle: body track first keyframe z-euler
	if body.size() > 0:
		t.in_body_angle = _modulate_range(-PI, PI, _quat_euler_z(body[0].q))
	else:
		t.in_body_angle = 0.0

	# outgoing body angle
	if t.out_vel >= 1.8 and body.size() > 0:
		var z := _modulate_range(-PI, PI, _quat_euler_z(body[body.size() - 1].q))
		t.out_body_angle = _modulate_range(-PI, PI, z - _template_outgoing_angle(t))
	else:
		t.out_body_angle = 0.0


func _movement_between(a: Dictionary, b: Dictionary) -> Vector3:
	# (pos_b - pos_a) / (frame_b - frame_a) * 100, z = 0
	var mv: Vector3 = (b.p - a.p) / float(b.f - a.f) * 100.0
	mv.z = 0.0
	return mv


func _quantize_velocity(v: float) -> float:
	if v < 1.8:
		return 0.0
	elif v < 4.2:
		return 3.5
	elif v < 6.0:
		return 5.0
	return 7.0


func _template_outgoing_angle(t: Dictionary) -> float:
	# port of Animation::GetOutgoingAngle()
	if t.out_vel >= 1.8:
		var player: Array = t.tracks["player"]
		var last: Dictionary = player[player.size() - 1]
		var prev: Dictionary = player[player.size() - 2]
		var last_move := Vector2(last.p.x - prev.p.x, last.p.y - prev.p.y)
		var angle := _fix_angle(_angle2d(last_move))
		if angle < -0.95 * PI or angle > 0.95 * PI:
			var body: Array = t.tracks.get("body", [])
			var z := 0.0
			if body.size() > 0:
				z = _quat_euler_z(body[body.size() - 1].q)
			if signf(angle) != signf(z):
				angle = PI * 0.99 * signf(z)
			else:
				angle = clampf(angle, -0.99 * PI, 0.99 * PI)
		return angle
	else:
		var body: Array = t.tracks.get("body", [])
		if body.size() > 0:
			return _modulate_range(-PI, PI, _quat_euler_z(body[body.size() - 1].q))
		return 0.0


# ------------------------------------------------------------------
# Autogen spec construction (legalAnim checks)
# ------------------------------------------------------------------

func _make_spec(t1: Dictionary, t2: Dictionary, direction: int) -> Dictionary:
	var angle := _get_angle(direction)
	var in_velo_id := _velocity_id(t1.in_vel)
	var out_velo_id := _velocity_id(t2.out_vel)
	var avg_velo_factor := _normalized_clamp(in_velo_id + out_velo_id, 0.0, 6.0)

	var legal := true

	# max acceleration
	if out_velo_id - in_velo_id > 1:
		legal = false

	# max deceleration (dot product keeps 180-degree turns from counting as
	# "keeping the same velocity")
	var dot := Vector2(0.0, -1.0).dot(_rotated2d(Vector2(0.0, -1.0), angle))
	var velo_id_diff_dotted := int(round(out_velo_id * dot - in_velo_id))
	if velo_id_diff_dotted < -3:
		legal = false

	# no sprint to dribble with backwards body angle
	if in_velo_id == 3 and out_velo_id == 1:
		if absf(t2.out_body_angle) > 0.25 * PI + MARGIN or absf(angle) > 0.25 * PI + MARGIN:
			legal = false

	if in_velo_id > 0 and out_velo_id > 0 and absf(angle) > 0.50 * PI + MARGIN:
		legal = false
	if in_velo_id + out_velo_id > 5 and absf(angle) > 0.25 * PI + MARGIN:
		legal = false
	if in_velo_id + out_velo_id > 4 and absf(angle) > 0.25 * PI + MARGIN:
		legal = false

	var body_angle_delta := _modulate_range(-PI, PI, t2.out_body_angle - t1.in_body_angle)
	if absf(angle + body_angle_delta) > 1.0 * PI + MARGIN:
		legal = false

	if not legal:
		return {}

	return {
		"t1": t1, "t2": t2, "angle": angle,
		"avg_velo_factor": avg_velo_factor,
	}


func _velocity_id(vel: float) -> int:
	# matches GetVelocityID(FloatToEnumVelocity(v)): 0=idle 1=dribble 2=walk 3=sprint
	if vel == 0.0:
		return 0
	elif vel == 3.5:
		return 1
	elif vel == 5.0:
		return 2
	return 3


func _get_angle(direction: int) -> float:
	match direction:
		0: return 0.0
		1: return 0.25 * PI
		2: return -0.25 * PI
		3: return 0.50 * PI
		4: return -0.50 * PI
		5: return 0.75 * PI
		6: return -0.75 * PI
		7: return 0.99 * PI
		8: return -0.99 * PI
	return 0.0


# ------------------------------------------------------------------
# Autogen keyframe generation (ports GenerateAutoAnims inner loop)
# ------------------------------------------------------------------

func _generate_autogen(spec: Dictionary) -> Dictionary:
	var t1: Dictionary = spec.t1
	var t2: Dictionary = spec.t2
	var angle: float = spec.angle
	var is_pure_idle: bool = (t1.in_vel == 0.0 and t2.out_vel == 0.0 and absf(angle) < 0.05)
	var out := {}

	# Port n == 0 ("player" vertical height interpolation from animcollection.cpp:326)
	var ptrack1: Array = t1.tracks.get("player", [])
	var ptrack2: Array = t2.tracks.get("player", [])
	var pframes := {0: true, 1: true, 12: true, 23: true, 24: true}
	for kf: Dictionary in ptrack1:
		pframes[kf.f] = true
	for kf: Dictionary in ptrack2:
		pframes[kf.f] = true
	for pf in range(2, GEN_FRAME_COUNT - 2, 4):
		pframes[pf] = true
	var pframe_list: Array = pframes.keys()
	pframe_list.sort()
	var pkfs: Array = []
	for frame: int in pframe_list:
		var z1: float = _interp_player_z(ptrack1, frame, int(t1.frame_count))
		var z2: float = _interp_player_z(ptrack2, frame, int(t2.frame_count))
		var orig_bias := clampf(frame - 1.0, 0.0, GEN_FRAME_COUNT - 3.0) / (GEN_FRAME_COUNT - 3.0)
		var bias := _curve(orig_bias, 0.7)
		var h: float = lerpf(z1, z2, bias)
		if is_pure_idle:
			var breath := sin(float(frame) / float(GEN_FRAME_COUNT - 1) * PI)
			h += 0.016 * breath
		pkfs.append({"f": frame, "q": Quaternion.IDENTITY, "p": Vector3(0.0, 0.0, h)})
	out["player"] = pkfs

	for bone in BONES:
		var track1: Array = t1.tracks.get(bone, [])
		var track2: Array = t2.tracks.get(bone, [])

		# union of keyframe frames from both templates
		var frames := {}
		for kf: Dictionary in track1:
			frames[kf.f] = true
		for kf: Dictionary in track2:
			frames[kf.f] = true
		if is_pure_idle:
			frames[6] = true
			frames[12] = true
			frames[18] = true
		var frame_list: Array = frames.keys()
		frame_list.sort()

		var is_body: bool = bone == "body"

		# per-node state (matches C++ declarations inside the node loop)
		var outgoing_movement: Vector3 = _rotated2d3(t2.out_movement, angle)
		var movement_change: Vector3 = (outgoing_movement - t1.in_movement) * (100.0 / GEN_FRAME_COUNT)

		var kfs: Array = []
		for frame: int in frame_list:
			var q1: Quaternion = _interp_orientation(track1, frame, int(t1.frame_count))
			var q2: Quaternion = _interp_orientation(track2, frame, int(t2.frame_count))

			var orig_bias := clampf(frame - 1.0, 0.0, GEN_FRAME_COUNT - 3.0) / (GEN_FRAME_COUNT - 3.0)
			var bias := orig_bias
			bias = pow(bias, 1.0 * (0.3 + 0.4 * spec.avg_velo_factor + 0.3 * _normalized_clamp(movement_change.length(), 0.0, 20.0)))
			bias = _curve(bias, 0.7)
			var orientation := _slerp(q1, q2, bias)

			if is_body:
				# body rotation smuggle
				var angle_quat := _quat_angle_axis(angle * pow(bias * 0.7 + orig_bias * 0.3, 1.0), Vector3(0.0, 0.0, 1.0))
				orientation = angle_quat * orientation

				# leaning (movement_change compounds across keyframes — by design)
				movement_change *= 0.5 + 0.5 * ((t1.in_movement * (1.0 - bias) + outgoing_movement * bias).length() / SPRINT_VELOCITY)
				var axis: Vector3 = movement_change.normalized() if movement_change.length() > 0.0 else Vector3.ZERO
				var lean_quat := _quat_angle_axis(
					movement_change.length() * LEAN_AMOUNT * (0.5 + 0.5 * sin(orig_bias * PI)),
					_rotated2d3(Vector3(0.0, 1.0, 0.0), _angle2d(Vector2(axis.x, axis.y))))
				orientation = lean_quat * orientation

			if is_pure_idle:
				var w := sin(float(frame) / float(GEN_FRAME_COUNT - 1) * PI)
				if bone == "body":
					orientation = _quat_angle_axis(0.022 * w, Vector3(0.0, 1.0, 0.0)) * orientation
				elif bone == "middle":
					orientation = _quat_angle_axis(0.032 * w, Vector3(1.0, 0.0, 0.3).normalized()) * orientation
				elif bone == "neck":
					orientation = _quat_angle_axis(-0.024 * w, Vector3(1.0, 0.0, -0.4).normalized()) * orientation
				elif bone == "left_shoulder":
					orientation = _quat_angle_axis(0.038 * w, Vector3(1.0, 0.2, 0.0).normalized()) * orientation
				elif bone == "right_shoulder":
					orientation = _quat_angle_axis(-0.038 * w, Vector3(1.0, -0.2, 0.0).normalized()) * orientation
				elif bone == "left_knee" or bone == "right_knee":
					orientation = _quat_angle_axis(-0.035 * w, Vector3(1.0, 0.0, 0.0)) * orientation

			kfs.append({"f": frame, "q": orientation})

		out[bone] = kfs
	return out


# ------------------------------------------------------------------
# Mirror (ports Animation::Mirror — pose part)
# ------------------------------------------------------------------

func _mirror_tracks(tracks: Dictionary) -> Dictionary:
	var out := {}
	for bone in tracks:
		if bone == "player":
			var pkfs: Array = []
			for kf: Dictionary in tracks["player"]:
				var pv: Vector3 = kf.get("p", Vector3.ZERO)
				pkfs.append({"f": kf.f, "q": Quaternion.IDENTITY, "p": Vector3(-pv.x, pv.y, pv.z)})
			out["player"] = pkfs
			continue
		var src_bone: String = bone
		if bone.begins_with("left_"):
			src_bone = "right_" + bone.substr(5)
		elif bone.begins_with("right_"):
			src_bone = "left_" + bone.substr(6)
		var src: Array = tracks.get(src_bone, tracks[bone])
		var kfs: Array = []
		for kf: Dictionary in src:
			var q: Quaternion = kf.q
			kfs.append({"f": kf.f, "q": Quaternion(q.x, -q.y, -q.z, q.w)})
		out[bone] = kfs
	return out


func _interp_player_z(track: Array, frame: int, template_frame_count: int) -> float:
	if track.is_empty():
		return 0.0
	if frame > 0 and frame < template_frame_count:
		var prev: Dictionary = {}
		var cur: Dictionary = {}
		for kf: Dictionary in track:
			if kf.f < frame:
				prev = kf
			elif kf.f >= frame:
				cur = kf
				break
		if not prev.is_empty() and not cur.is_empty():
			var dist: int = cur.f - prev.f
			var bias: float = float(frame - prev.f) / float(dist)
			return lerpf(prev.p.z, cur.p.z, bias)
		elif not cur.is_empty():
			return cur.p.z
		elif not prev.is_empty():
			return prev.p.z
	elif frame >= template_frame_count:
		return track[track.size() - 1].p.z
	return track[0].p.z


# ------------------------------------------------------------------
# Interpolation (ports Animation::GetInterpolatedValues, orientation only)
# ------------------------------------------------------------------

func _interp_orientation(track: Array, frame: int, template_frame_count: int) -> Quaternion:
	if frame > 0 and frame < template_frame_count:
		var prev: Dictionary = {}
		var cur: Dictionary = {}
		for kf: Dictionary in track:
			if kf.f < frame:
				prev = kf
			elif kf.f >= frame:
				cur = kf
				break
		if not prev.is_empty() and not cur.is_empty():
			var dist: int = cur.f - prev.f
			var bias: float = float(frame - prev.f) / float(dist)
			return _slerp(prev.q, cur.q, bias)
		elif not cur.is_empty():
			return cur.q
		elif not prev.is_empty():
			return prev.q
		return Quaternion.IDENTITY
	elif frame >= template_frame_count:
		# extrapolate beyond animation using the last two keyframes
		if track.size() >= 2:
			var last: Dictionary = track[track.size() - 1]
			var second: Dictionary = track[track.size() - 2]
			var dist1: float = last.f - second.f
			var dist2: float = frame - last.f
			var bias: float = 1.0 + dist2 / dist1
			return _slerp(second.q, last.q, bias)
		elif track.size() == 1:
			return track[0].q
		return Quaternion.IDENTITY
	else:
		# frame <= 0: first key
		if track.size() > 0:
			return track[0].q
		return Quaternion.IDENTITY


# ------------------------------------------------------------------
# Math ports (bluntmath / Quaternion / Vector3)
# ------------------------------------------------------------------

func _slerp(from: Quaternion, to: Quaternion, bias: float) -> Quaternion:
	# exact port of Quaternion::GetSlerped
	var qb := to
	var cos_half: float = from.w * qb.w + from.x * qb.x + from.y * qb.y + from.z * qb.z
	if cos_half < 0.0:
		qb = -qb
		cos_half = -cos_half
	if absf(cos_half) >= 1.0:
		return from
	var half_theta := acos(cos_half)
	var sin_half := sqrt(1.0 - cos_half * cos_half)
	if absf(sin_half) < 0.000001:
		return Quaternion(
			(from.x + qb.x) * 0.5, (from.y + qb.y) * 0.5,
			(from.z + qb.z) * 0.5, (from.w + qb.w) * 0.5)
	var ratio_a := sin((1.0 - bias) * half_theta) / sin_half
	var ratio_b := sin(bias * half_theta) / sin_half
	return Quaternion(
		from.x * ratio_a + qb.x * ratio_b,
		from.y * ratio_a + qb.y * ratio_b,
		from.z * ratio_a + qb.z * ratio_b,
		from.w * ratio_a + qb.w * ratio_b)


func _quat_euler_z(q: Quaternion) -> float:
	# exact port of Quaternion::GetAngles (x=e0, y=e2, z=e1) — Z output only
	var x := q.x
	var y := q.z
	var z := q.y
	var singularity := x * y + z * q.w
	if singularity > 0.49999:
		return 2.0 * atan2(x, z)
	if singularity < -0.49999:
		return -2.0 * atan2(x, z)
	return atan2(2.0 * y * q.w - 2.0 * x * z, 1.0 - 2.0 * y * y - 2.0 * z * z)


func _quat_angle_axis(angle: float, axis: Vector3) -> Quaternion:
	var half := 0.5 * angle
	var s := sin(half)
	return Quaternion(s * axis.x, s * axis.y, s * axis.z, cos(half))


func _curve(source: float, bias: float) -> float:
	return (sin((source - 0.5) * PI) * 0.5 + 0.5) * bias + source * (1.0 - bias)


func _normalized_clamp(value: float, vmin: float, vmax: float) -> float:
	var v := clampf(value, vmin, vmax)
	return (v - vmin) / (vmax - vmin)


func _modulate_range(vmin: float, vmax: float, value: float) -> float:
	var step := vmax - vmin
	var v := value
	while v < vmin:
		v += step
	while v > vmax:
		v -= step
	return v


func _fix_angle(angle: float) -> float:
	return _modulate_range(-PI, PI, angle + 0.5 * PI)


func _angle2d(v: Vector2) -> float:
	var angle := atan2(v.y, v.x)
	if angle < 0.0:
		angle += 2.0 * PI
	return angle


func _rotated2d(v: Vector2, angle: float) -> Vector2:
	return Vector2(
		v.x * cos(angle) - v.y * sin(angle),
		v.y * cos(angle) + v.x * sin(angle))


func _rotated2d3(v: Vector3, angle: float) -> Vector3:
	# Vector3::GetRotated2D (GF Z-up space; rotates in the x/y plane)
	return Vector3(
		v.x * cos(angle) - v.y * sin(angle),
		v.y * cos(angle) + v.x * sin(angle),
		v.z)
