extends Node3D
class_name DZFootScene

# === DZFoot protocol constants (must match DZFootProtocol.h) ===
const DZ_MAX_PLAYERS = 22
const ANIM_IDLE = 0
const ANIM_WALK = 1
const ANIM_RUN = 2
const ANIM_SPRINT = 3
const ANIM_SHOOT_R = 4
const ANIM_SHOOT_L = 5
const ANIM_PASS_S = 6
const ANIM_PASS_L = 7
const ANIM_HEADER = 8
const ANIM_TACKLE = 9
const ANIM_DRIBBLE = 10
const ANIM_FALL = 11
const ANIM_CELEBRATE = 12
const ANIM_GK_IDLE = 13
const ANIM_GK_DIVE_L = 14
const ANIM_GK_DIVE_R = 15
const ANIM_GK_CATCH = 16

# Button bitmask constants
const BUTTON_KICK = 1
const BUTTON_PASS = 2
const BUTTON_HIGH_PASS = 4
const BUTTON_SHOT = 8
const BUTTON_SLIDING = 16
const BUTTON_DRIBBLE = 32
const BUTTON_SPRINT = 64
const BUTTON_SWITCH_PLAYER = 128

# === Scene nodes ===
var camera: Camera3D
var directional_light: DirectionalLight3D
var world_env: WorldEnvironment
var stadium: Node3D
var ball: MeshInstance3D
var players: Array[Node3D] = []
var officials: Array[Node3D] = []

# === Animation name mapping ===
const ANIM_NAMES = [
	"idle", "walk", "run", "sprint",
	"shoot_right", "shoot_left", "pass_short", "pass_long",
	"header", "tackle", "dribble", "fall",
	"celebrate", "gk_idle", "gk_dive_left", "gk_dive_right", "gk_catch"
]

# === Plugin reference (DZFootPlugin singleton from Kotlin) ===
var dz_plugin: Object = null

# === Dynamic animation collection (desktop GF AnimCollection port) ===
const DZAnimCollectionScript = preload("res://scripts/DZAnimCollection.gd")
const DZRadarScript = preload("res://scripts/DZRadar.gd")
var _anim_collection: RefCounted = null  # DZAnimCollection
var _bone_paths := {}  # bone name -> NodePath (from GLB clip track paths)
var _anim_lib_initialized := false
# Clips are identical match to match: baked at build time (tools/bake_anims.gd)
# into one binary library, streamed in on a worker thread at scene start.
const BAKED_ANIM_PATH := "res://baked/dzdyn.res"
var _baked_anim_requested := false


# === DZFootBridge (GDExtension or GDScript fallback) ===
var dz_bridge: Object = null

# === State ===
var last_tick: int = 0
var _active_player_idx: int = 0
var _active_team: int = 0

# Coordinate mapping: packets carry GF env coords — pos x = GF_x/55 (length ∈ [-1,1]),
# y = GF_y/83.6 (width ∈ [-0.43,0.43]), z = height in meters. GF is Z-up, Godot is Y-up.
# Godot(x,y,z) = (env.x*55, env.z, -env.y*83.6); quat = (qx, qz, -qy, qw).
const ENV_X_SCALE := 55.0
const ENV_Y_SCALE := 83.6

func _gf_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x * ENV_X_SCALE, p.z, -p.y * ENV_Y_SCALE)

# === Interpolation state ===
var _prev_positions: Array = []  # Array[Vector3] for players
var _curr_positions: Array = []
var _prev_ball_pos: Vector3 = Vector3.ZERO
var _curr_ball_pos: Vector3 = Vector3.ZERO
var _state_interp_time: float = 0.0
var _state_interp_period: float = 0.05  # 50ms between states (20Hz update)
var _state_received: bool = false
var _cam_diag_printed: bool = false
var _heartbeat: float = 0.0
var _heartbeat_total: int = 0
var _states_received: int = 0

# Crash bisection: skip every 2D CanvasLayer (touch UI, overlay, broadcast HUD)
const DEBUG_NO_UI := false
# Crash bisection: skip ALL AnimationPlayer play/seek calls
const DEBUG_NO_ANIM := false
# Crash bisection: skip dynamic clip generation — use only the 17 static GLB clips
const DEBUG_NO_DYNCLIP := false
# Runtime clip mutations corrupt AnimationMixer caches → GLThread SIGSEGV
# (confirmed: even private-lib add_animation during playback crashes ~10s in).
# Fix: ALL clips are generated ONCE at setup into a shared immutable library.
var _dyn_lib: AnimationLibrary = null

# Runtime verification of facing/motion/anim consistency (logs [DZ_VERIFY] every 2s)
const DEBUG_VERIFY := true
var _verify_t := 0.0
var _verify_prev: Array = []
var _verify_shoulders: Array = []
var _bad_clip_logs := 0
var _clip_misses := 0
var _clip_hits := 0
var _bad_clip_window := 0.0

# === Touch UI ===
var _software_gl: bool = false  # SwiftShader/llvmpipe: canvas blend shader variant fails → force opaque HUD
var _high_quality: bool = false  # real shadows + tonemap + bloom + 4x MSAA (capable GPUs only)
var _blob_texture: ImageTexture = null
var _touch_canvas: CanvasLayer
var _joy_base: Control
var _joy_knob: Control
var _joy_active: bool = false
var _joy_pointer: int = -1
var _joy_center: Vector2 = Vector2.ZERO
var _joy_radius: float = 120.0
var _joy_dir: Vector2 = Vector2.ZERO
var _action_buttons: Dictionary = {}  # name -> {control, mask, pressed, pointer}
var _button_pointers: Dictionary = {}  # pointer_id -> button_name
var _ui_visible: bool = true

# === Loading screen ===
var _loading_layer: CanvasLayer = null
var _loading_label: Label = null
var _loading_bar: ProgressBar = null
# Scene fully initialized — plugin signal handlers must not mutate scene
# (kits, appearance, process loop) while _ready() is still mid-await loading
# assets. Emission into a half-built scene SIGSEGVs in nativeEmitSignal.
var _scene_ready: bool = false

# Loaded + displayed + settled. Input packets (= server start-gate signal) and
# the kickoff auto-start countdown only begin once this is true.
var _display_ok: bool = false
const SHADOW_MAX_DIST := 45.0      # metres; players only, broadcast cam
const SHADOW_ATLAS_SIZE := 1024    # small directional atlas (secondary quality)
const DISPLAY_SETTLE_S := 1.5
const KICKOFF_AUTO_DELAY_MS := 6000   # no touch after display -> auto kickoff
var _load_t0_ms: int = 0
var _load_phase_ms: int = 0

# === UI Overlay ===
var _ui_layer: CanvasLayer
var _score_label: Label
var _timer_label: Label
var _team_a_label: Label
var _team_b_label: Label
var _score_a: int = 0
var _score_b: int = 0
var _game_timer: float = 0.0
var _team_a_name: String = "Team A"
var _team_b_name: String = "Team B"

# === Demo mode fallback when no LiveKit data arrives ===
var _demo_timer: Timer = null

# === Camera ===
# Fraction of original distance kept after pulling camera toward look point.
const CAM_DIST_SCALE: float = 0.78
const CAM_MIN_HEIGHT: float = 8.0
const CAM_SMOOTH_RATE: float = 10.0
var _cam_raw_pos := Vector3.ZERO
var _cam_look_pt := Vector3.ZERO
var _cam_target_quat := Quaternion.IDENTITY
var _cam_target_fov := 55.0
var _cam_zoom := 1.0            # temporary goal-celebration zoom (<1 = closer)
var _cam_have_target := false
var _goal_cam_until_ms := 0
var _goal_scorer_idx := -1
var _cam_mode := 0               # 0 = broadcast (server), 1 = action (ball follow)
var _cam_btn: Control = null
var _cam_btn_label: Label = null
var _last_touch_ms := 0
var _set_piece_type := -1
var _set_piece_since_ms := 0
# Auto-take retries while the set piece is pending: input packets ride an
# unreliable channel, a single 3-packet burst can be fully lost.
var _auto_take_next_ms: int = 0
var _auto_dir_x: float = 0.0
var _auto_dir_ms: int = 0
# Set on EVENT_END_MATCH — blocks input/auto-take and keeps the end banner up.
var _match_over: bool = false

# === Player appearance (MatchSetup PlayerStaticInfo) ===
var _body_templates: Array = []     # [thin, average, muscular, heavy] Node3D or null
var _hair_templates: Array = []     # hair_short/long/mohawk/curly/ponytail (bald = none)
var _beard_templates: Array = []    # beard_stubble/short/full (none = 0)
var _skin_textures: Array = []      # skin_0..6
var _hair_tints := [                # fallback tints for hair_0..7
	Color(0.08, 0.06, 0.05), Color(0.20, 0.12, 0.07), Color(0.35, 0.20, 0.10),
	Color(0.55, 0.38, 0.20), Color(0.80, 0.65, 0.35), Color(0.75, 0.30, 0.10),
	Color(0.70, 0.70, 0.70), Color(0.92, 0.92, 0.90)]
var _appearance_done := false
var _player_names: Array = []       # String per player idx (from setup)
var _player_numbers: Array = []     # int per player idx
var _player_heights: Array = []     # float scale per player idx

# === Active-player marker ===
var _active_ring: MeshInstance3D = null
var _active_label: Label3D = null

# === Ball extras ===
var _ball_shadow: MeshInstance3D = null
var _trail_quads: Array = []          # pooled trail ribbon quads (MeshInstance3D)
var _trail_points: Array = []       # recent ball Vector3 positions (world)
var _ball_spin := Vector3.ZERO     # accumulated euler rotation (visual roll)
var _pitch_top_y := 0.06

# === Broadcast HUD ===
var _sb_panel: Panel = null
var _sb_timer: Label = null
var _sb_score: Label = null
var _sb_team_a: Label = null
var _sb_team_b: Label = null
var _sb_color_a: ColorRect = null
var _sb_color_b: ColorRect = null
var _radar: Control = null
var _radar_timer := 0.0
var _event_label: Label = null
var _event_tween: Tween = null
var _goal_banner: Panel = null
var _goal_label: Label = null
var _goal_tween: Tween = null
var _goal_zoom_tween: Tween = null
var _official_positions: Array = [Vector3.ZERO, Vector3.ZERO, Vector3.ZERO]
var _official_rot: Array = [0.0, 0.0, 0.0]

# === Input state ===
var _move_dir := Vector2.ZERO
var _buttons: int = 0
var _btn_latch: Dictionary = {}  # mask -> release deadline (ms)

# Button definitions: name -> {mask, label, color, offset}
const BUTTON_DEFS = {
	"pass":     {"mask": BUTTON_PASS,           "label": "PASSE",  "color": Color(0.15, 0.75, 0.25, 0.65), "offset": Vector2(0, 0)},
	"shot":     {"mask": BUTTON_SHOT,           "label": "TIR",    "color": Color(0.85, 0.15, 0.15, 0.65), "offset": Vector2(-110, -80)},
	"kick":     {"mask": BUTTON_KICK,           "label": "LONG",   "color": Color(0.90, 0.80, 0.15, 0.65), "offset": Vector2(95, -60)},
	"sprint":   {"mask": BUTTON_SPRINT,         "label": "SPRINT", "color": Color(0.15, 0.55, 0.95, 0.65), "offset": Vector2(-165, 25)},
	"switch":   {"mask": BUTTON_SWITCH_PLAYER,  "label": "CHANG",  "color": Color(0.85, 0.85, 0.85, 0.65), "offset": Vector2(70, -150)},
	"dribble":  {"mask": BUTTON_DRIBBLE,        "label": "DRIB",   "color": Color(0.70, 0.15, 0.75, 0.65), "offset": Vector2(150, 30)},
	"sliding":  {"mask": BUTTON_SLIDING,        "label": "TACLE",  "color": Color(0.95, 0.45, 0.05, 0.65), "offset": Vector2(-75, 95)},
	"highpass": {"mask": BUTTON_HIGH_PASS,      "label": "LOB",    "color": Color(0.40, 0.20, 0.85, 0.65), "offset": Vector2(50, 105)},
}
const BTN_RADIUS := 52.0
const BTN_HIT_RADIUS := 85.0
const JOY_DEADZONE := 0.08

func _show_loading_screen() -> void:
	_loading_layer = CanvasLayer.new()
	_loading_layer.layer = 30
	add_child(_loading_layer)
	var panel := Panel.new()
	panel.set_anchors_preset(Control.PRESET_FULL_RECT)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.05, 0.06, 0.09, 1.0)
	panel.add_theme_stylebox_override("panel", style)
	_loading_layer.add_child(panel)
	_loading_label = Label.new()
	_loading_label.set_anchors_preset(Control.PRESET_CENTER)
	_loading_label.position = Vector2(-320, -80)
	_loading_label.size = Vector2(640, 70)
	_loading_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_loading_label.add_theme_font_size_override("font_size", 34)
	_loading_label.add_theme_color_override("font_color", Color.WHITE)
	panel.add_child(_loading_label)
	_loading_bar = ProgressBar.new()
	_loading_bar.set_anchors_preset(Control.PRESET_CENTER)
	_loading_bar.position = Vector2(-250, 0)
	_loading_bar.size = Vector2(500, 34)
	_loading_bar.min_value = 0
	_loading_bar.max_value = 100
	_loading_bar.show_percentage = true
	panel.add_child(_loading_bar)

func _set_loading(text: String, pct: float) -> void:
	if _loading_label:
		_loading_label.text = text
	if _loading_bar:
		_loading_bar.value = pct

func _hide_loading_screen() -> void:
	if _loading_layer:
		_loading_layer.queue_free()
		_loading_layer = null
		_loading_label = null
		_loading_bar = null

func _phase(label: String) -> void:
	var now := Time.get_ticks_msec()
	print("[DZ_LOAD] %-22s +%dms (total %dms)" % [label, now - _load_phase_ms, now - _load_t0_ms])
	_load_phase_ms = now

func _ready() -> void:
	_last_touch_ms = Time.get_ticks_msec()
	_load_t0_ms = _last_touch_ms
	_load_phase_ms = _last_touch_ms
	if ResourceLoader.exists(BAKED_ANIM_PATH):
		_baked_anim_requested = ResourceLoader.load_threaded_request(BAKED_ANIM_PATH) == OK
	# Get the DZFootPlugin singleton from Kotlin
	dz_plugin = Engine.get_singleton("DZFootPlugin")
	if dz_plugin:
		dz_plugin.game_state_received.connect(_on_game_state_received)
		dz_plugin.match_setup_received.connect(_on_match_setup_received)
		dz_plugin.match_event_received.connect(_on_match_event_received)
		if dz_plugin.has_signal("tactical_state_received"):
			dz_plugin.tactical_state_received.connect(_on_tactical_state_received)
		print("[DZFoot] DZFootPlugin connected")
	else:
		push_warning("[DZFoot] DZFootPlugin singleton not found — data won't flow")

	# Create the bridge: use GDExtension if available, otherwise pure GDScript fallback
	if ClassDB.class_exists("DZFootBridge"):
		dz_bridge = ClassDB.instantiate("DZFootBridge")
		print("[DZFoot] Using DZFootBridge GDExtension")
		add_child(dz_bridge)
	else:
		dz_bridge = DZFootBridgeGD.new()
		print("[DZFoot] Using DZFootBridgeGD (pure GDScript fallback — no native .so found)")

	# Init interpolation + appearance arrays
	for i in range(DZ_MAX_PLAYERS):
		_prev_positions.append(Vector3.ZERO)
		_curr_positions.append(Vector3.ZERO)
		_player_names.append("")
		_player_numbers.append(0)
		_player_heights.append(1.0)

	_show_loading_screen()
	await get_tree().process_frame

	_set_loading("Chargement du stade...", 5)
	_setup_camera()
	_setup_lighting()
	_phase("camera+light")
	await get_tree().process_frame
	_set_loading("Chargement du stade...", 15)
	await _setup_stadium()
	_phase("stadium")
	await get_tree().process_frame
	_set_loading("Chargement du ballon...", 45)
	_setup_ball()
	_setup_ball_extras()
	_phase("ball")
	await get_tree().process_frame
	# Paint the label BEFORE the heavy sync work — otherwise the previous
	# frame ("ballon 45%") stays on screen for the whole players+anims step.
	_set_loading("Chargement des joueurs...", 50)
	await get_tree().process_frame
	await _load_appearance_assets()
	_phase("appearance assets")
	await _setup_players()
	_phase("players+anims")
	await get_tree().process_frame
	_set_loading("Chargement des arbitres...", 85)
	_setup_officials()
	_setup_active_marker()
	if not DEBUG_NO_UI:
		_setup_touch_ui()
		_setup_ui_overlay()
		_setup_broadcast_hud()
	_phase("officials+ui")
	_set_loading("Préparation du rendu...", 95)
	await get_tree().process_frame
	# First real 3D frames (shader compile hitch) happen here, behind the overlay.
	await get_tree().process_frame
	await get_tree().process_frame
	_phase("first 3D frames")
	_set_loading("Terminé", 100)
	await get_tree().process_frame
	_hide_loading_screen()
	_scene_ready = true
	# A match_setup packet may have arrived during loading — apply it now
	# that players/appearance exist (bridge buffers it via _has_match_setup).
	_apply_match_setup()

	# If no match setup arrives within 30s, show a demo formation so the
	# scene is visible even without a running server. (GF container spawn
	# can take ~2min — Kotlin shows the loading overlay meanwhile.)
	_demo_timer = Timer.new()
	_demo_timer.wait_time = 30.0
	_demo_timer.one_shot = true
	_demo_timer.timeout.connect(_setup_demo_mode)
	add_child(_demo_timer)
	_demo_timer.start()

	print("[DZFoot] Scene initialized — waiting for GameStatePackets (demo in 8s if none)")
	print("[DZ_DIAG] ready vp=%s stretch=%s" % [str(get_viewport().size), str(get_window().content_scale_size)])
	# Defer UI rect diagnostic until after first layout pass
	await get_tree().process_frame
	await get_tree().process_frame
	if _joy_base:
		print("[DZ_DIAG] joy_base rect=%s global=%s visible=%s" % [str(_joy_base.get_rect()), str(_joy_base.global_position), str(_joy_base.is_visible_in_tree())])
	# Loaded AND displayed: let the pitch settle for a moment, then open input.
	# The first input packet is the server's "client ready" signal (start gate);
	# the kickoff auto-start countdown only begins from this moment.
	await get_tree().create_timer(DISPLAY_SETTLE_S).timeout
	_display_ok = true
	_last_touch_ms = Time.get_ticks_msec()
	_set_piece_since_ms = _last_touch_ms
	_auto_take_next_ms = 0
	_phase("display ok")
	print("[DZ_LOAD] input enabled — kickoff auto-start in %dms if no touch" % KICKOFF_AUTO_DELAY_MS)
	await get_tree().create_timer(2.0).timeout
	_log_render_diag()

func _first_mesh_of(node: Node) -> MeshInstance3D:
	if node == null:
		return null
	if node is MeshInstance3D:
		return node as MeshInstance3D
	for c in node.get_children():
		var m := _first_mesh_of(c)
		if m:
			return m
	return null

func _add_test_cubes(cam: Camera3D) -> void:
	var img := Image.create(8, 8, false, Image.FORMAT_RGBA8)
	for y in 8:
		for x in 8:
			img.set_pixel(x, y, Color.WHITE if (x + y) % 2 == 0 else Color(0.1, 0.4, 0.9))
	var tex := ImageTexture.create_from_image(img)
	var defs := [
		{"x": -3.0, "unshaded": true, "color": Color(1, 0, 0), "tex": null},
		{"x": 0.0, "unshaded": false, "color": Color(0, 1, 0), "tex": null},
		{"x": 3.0, "unshaded": false, "color": Color(1, 1, 1), "tex": tex},
	]
	for d in defs:
		var mi := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = Vector3(1.5, 1.5, 1.5)
		mi.mesh = bm
		var mat := StandardMaterial3D.new()
		mat.albedo_color = d["color"]
		if d["unshaded"]:
			mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		if d["tex"] != null:
			mat.albedo_texture = d["tex"]
		mi.material_override = mat
		mi.position = Vector3(d["x"], 0.0, -8.0)
		cam.add_child(mi)

func _log_render_diag() -> void:
	var vp := get_viewport()
	var cam := vp.get_camera_3d()
	if cam:
		var fwd := -cam.global_transform.basis.z
		print("[DZ_RDIAG] cam pos=%s fwd=%s fov=%.1f near=%.2f far=%.1f cull=%d" % [str(cam.global_position), str(fwd), cam.fov, cam.near, cam.far, cam.cull_mask])
	print("[DZ_RDIAG] objs=%d prims=%d draws=%d disable_3d=%s msaa=%d scale3d=%.2f" % [
		RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_OBJECTS_IN_FRAME),
		RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME),
		RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME),
		str(vp.disable_3d), vp.msaa_3d, vp.scaling_3d_scale])
	var targets := {"pitch": stadium.get_node_or_null("Pitch") if stadium else null,
		"stadium": stadium.get_node_or_null("StadiumModel") if stadium else null,
		"player0": players[0] if not players.is_empty() else null}
	for k in targets:
		var mi := _first_mesh_of(targets[k])
		if mi == null:
			print("[DZ_RDIAG] %s: no mesh" % k)
			continue
		var m := mi.get_active_material(0)
		print("[DZ_RDIAG] %s: mesh=%s vis=%s layers=%d aabb=%s gpos=%s skel=%s mat=%s tex=%s" % [k, mi.name, str(mi.is_visible_in_tree()), mi.layers,
			str(mi.global_transform * mi.get_aabb()), str(mi.global_position), str(mi.skeleton), str(m.get_class() if m else "null"),
			str((m as BaseMaterial3D).albedo_texture != null) if m is BaseMaterial3D else "n/a"])

# === Plugin signal handlers ===
func _on_game_state_received(data: PackedByteArray) -> void:
	# Real server data — cancel the demo fallback immediately (don't wait
	# for match setup; gf.setup can be lost, gamestates still flow).
	if _demo_timer and is_instance_valid(_demo_timer):
		_demo_timer.stop()
		_demo_timer = null
	if dz_bridge:
		dz_bridge.set_game_state_bytes(data)

func _on_match_setup_received(data: PackedByteArray) -> void:
	if _demo_timer and is_instance_valid(_demo_timer):
		_demo_timer.stop()
		_demo_timer = null
	if dz_bridge:
		dz_bridge.set_match_setup_bytes(data)
	if _scene_ready:
		_apply_match_setup()

func _on_match_event_received(data: PackedByteArray) -> void:
	if dz_bridge:
		dz_bridge.set_match_event_bytes(data)

func _on_tactical_state_received(data: PackedByteArray) -> void:
	if dz_bridge:
		dz_bridge.set_tactical_state_bytes(data)

# Consume tactical packets once per frame — cheap, data is tiny.
func _consume_tactical() -> void:
	if not dz_bridge or not dz_bridge.has_new_tactical():
		return
	var sp: int = dz_bridge.get_set_piece_type()
	if sp != _set_piece_type:
		_set_piece_type = sp
		_set_piece_since_ms = Time.get_ticks_msec()
		_auto_take_next_ms = 0
		_auto_dir_ms = 0
	var traps = dz_bridge.get_offside_traps()
	if traps != null and _radar:
		_radar.offside_trap_a = traps["trap_a"]
		_radar.offside_trap_b = traps["trap_b"]

# === Scene setup ===

func _setup_camera() -> void:
	camera = Camera3D.new()
	camera.fov = 55.0
	camera.near = 0.1
	camera.far = 500.0
	camera.position = Vector3(0, 40, 50)
	add_child(camera)
	camera.look_at(Vector3.ZERO)
	camera.make_current()

func _gpu_supports_shadows(adapter_name: String) -> bool:
	if "NVIDIA" in adapter_name or "GeForce" in adapter_name or "Quadro" in adapter_name:
		return true
	var re := RegEx.new()
	re.compile("Adreno \\(TM\\) [6-9]\\d\\d|Mali-G[7-9]\\d|Apple")
	return re.search(adapter_name) != null

func _setup_lighting() -> void:
	directional_light = DirectionalLight3D.new()
	directional_light.light_energy = 1.1
	# Shadows require a real GL driver — SwiftShader emulator mode only exposes
	# 261 fragment uniform vectors and fails to link shadow shader variants
	# (null shader → GL thread SIGSEGV as soon as a shadow split updates).
	# Detect software rasterizers and keep shadows off there.
	var adapter_name: String = RenderingServer.get_video_adapter_name()
	var software_gl: bool = "SwiftShader" in adapter_name or "llvmpipe" in adapter_name or "Software" in adapter_name
	# The emulator's GL translator reliably SIGSEGVs during shadow-map passes
	# (GLThread fault 0x220 after a few minutes). Treat it like software GL for shadows.
	var emulator_gl: bool = "Emulator" in adapter_name or "Translator" in adapter_name
	_software_gl = software_gl
	if software_gl or emulator_gl:
		# CPU skinning: the emulator translator crashes on per-frame bone
		# texture/UBO uploads for skinned meshes (GLThread SIGSEGV on first
		# live packet). Property exists on Viewport in Godot 4.3+.
		var vp := get_viewport()
		for p in vp.get_property_list():
			if p.name == "software_skinning":
				vp.software_skinning = true
				print("[DZFoot] software_skinning enabled (emulator/software GL)")
				break
		# Emulator running on a real host GPU (-gpu host): shadows are OK to try.
		var host_gpu: bool = ("NVIDIA" in adapter_name or "Quadro" in adapter_name or "GeForce" in adapter_name) and not software_gl
		directional_light.shadow_enabled = host_gpu
		if host_gpu:
			directional_light.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
			directional_light.directional_shadow_max_distance = SHADOW_MAX_DIST
		print("[DZFoot] Shadows %s on %s (software=%s emulator=%s)" % [directional_light.shadow_enabled, adapter_name, software_gl, emulator_gl])
	else:
		# Shadows are secondary: ONE orthogonal split, short range, small atlas,
		# players/ball only (stadium never casts — see _strip_stadium_shadows).
		# Low-end mobile GPUs (PowerVR GE8320: lit shader variants with shadows
		# fail -> only unlit geometry draws, rest stays background colour) keep
		# the cheap blob shadows instead.
		var shadows_ok := _gpu_supports_shadows(adapter_name)
		directional_light.shadow_enabled = shadows_ok
		if shadows_ok:
			directional_light.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
			directional_light.directional_shadow_max_distance = SHADOW_MAX_DIST
		print("[DZFoot] Shadows %s on %s" % [shadows_ok, adapter_name])
	_high_quality = directional_light.shadow_enabled and not software_gl
	if _high_quality:
		directional_light.shadow_bias = 0.03
		directional_light.shadow_normal_bias = 1.2
		directional_light.shadow_blur = 1.6
		directional_light.directional_shadow_fade_start = 0.8
		get_viewport().msaa_3d = Viewport.MSAA_4X
	RenderingServer.directional_shadow_atlas_set_size(2048 if _high_quality else SHADOW_ATLAS_SIZE, true)
	directional_light.position = Vector3(30, 60, 30)
	add_child(directional_light)
	directional_light.look_at(Vector3.ZERO)

	world_env = WorldEnvironment.new()
	var env := Environment.new()
	# LIGHTWEIGHT environment (load-time + fill-rate): flat colour background
	# and flat ambient instead of ProceduralSky (radiance cubemap + sky shader),
	# no fog (extra code in every material variant), no glow / colour grading
	# (full-screen post passes + shader variants). The camera is a high
	# broadcast view so the sky is almost never visible anyway.
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.35, 0.48, 0.66)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.86, 0.92, 1.0)
	env.ambient_light_energy = 0.45
	env.fog_enabled = false
	env.ssr_enabled = false
	env.ssil_enabled = false
	env.ssao_enabled = false
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.tonemap_exposure = 1.0
	env.adjustment_enabled = true
	env.adjustment_brightness = 1.0
	env.adjustment_contrast = 1.05
	env.adjustment_saturation = 0.95
	env.glow_enabled = false
	if not software_gl:
		get_viewport().msaa_3d = Viewport.MSAA_4X
	world_env.environment = env
	add_child(world_env)

func _setup_stadium() -> void:
	stadium = Node3D.new()
	stadium.name = "Stadium"
	add_child(stadium)

	# Try loading real pitch GLB
	var pitch_scene = _load_glb("res://models/pitch.glb")
	if pitch_scene:
		pitch_scene.name = "Pitch"
		# pitch.glb is already authored in meters (AABB 120x80) — no rescale.
		# Lift it slightly above the stadium's flat ground slabs (stadium_test
		# contains a 128x88 white slab at y=0 that would cover the grass).
		pitch_scene.position.y = 0.05
		stadium.add_child(pitch_scene)
		_override_pitch_texture(pitch_scene)
		# Measure real pitch surface height so overlays sit above the geometry
		# (pitch GLB lifted +0.05; its top face may be higher than y=0.06).
		var pitch_top := 0.06
		var pmi := _find_mesh_in_node(pitch_scene)
		if pmi:
			pitch_top = pmi.to_global(pmi.get_aabb().end).y + 0.005
		_pitch_top_y = pitch_top
		print("[DZFoot] Pitch top surface y=%.3f" % pitch_top)
		_draw_pitch_lines(stadium)
		print("[DZFoot] Pitch GLB loaded")
	else:
		# Fallback: procedural pitch
		var pitch_mesh := PlaneMesh.new()
		pitch_mesh.size = Vector2(110, 72)
		pitch_mesh.material = _create_pitch_material()
		var pitch := MeshInstance3D.new()
		pitch.mesh = pitch_mesh
		stadium.add_child(pitch)
		_draw_pitch_lines(stadium)
		print("[DZFoot] Pitch fallback (procedural)")

	# Load goals GLB
	var goals_scene = _load_glb("res://models/goals.glb")
	if goals_scene:
		goals_scene.name = "Goals"
		# goals.glb is already in meters — no rescale.
		stadium.add_child(goals_scene)
		# The GLB net panels are zero-thickness quads with an opaque white
		# texture (render as white boxes) — hide them, keep posts/bar.
		var gstack: Array = [goals_scene]
		while not gstack.is_empty():
			var gn: Node = gstack.pop_back()
			gstack.append_array(gn.get_children())
			if gn is MeshInstance3D and (gn as MeshInstance3D).mesh:
				var gsz: Vector3 = (gn as MeshInstance3D).mesh.get_aabb().size
				if minf(gsz.x, minf(gsz.y, gsz.z)) < 0.01:
					(gn as MeshInstance3D).visible = false
		print("[DZFoot] Goals GLB loaded")

	# Load stadium GLB (stadium_minimal.glb is a broken stub — try real model first)
	var stad_scene = await _load_glb_threaded("res://models/stadium_test.glb")
	if not stad_scene:
		stad_scene = _load_glb("res://models/stadium_minimal.glb")
	if stad_scene:
		stad_scene.name = "StadiumModel"
		# stadium_test.glb is already in meters (208x39x168) — no rescale.
		stadium.add_child(stad_scene)
		_fix_stadium_ground(stad_scene)
		print("[DZFoot] Stadium GLB loaded")
		_setup_goal_nets()
		_setup_corner_flags()
		_setup_ad_boards()
		_setup_crowd()
	_strip_stadium_shadows(stadium)

func _strip_stadium_shadows(root: Node) -> void:
	# Stadium / pitch / goals / boards never cast shadows: only players and the
	# ball do. Removes the whole stadium from the shadow pass (big saving in
	# both shadow-caster variants to compile and per-frame shadow draw calls).
	var stack: Array = [root]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		if n is GeometryInstance3D:
			(n as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF

func _load_glb(path: String) -> Node3D:
	# Exported pck: .glb are imported resources -> load via ResourceLoader (remap-aware).
	# FileAccess/GLTFDocument only work for raw unpacked files (editor/dev).
	if ResourceLoader.exists(path):
		var res := load(path)
		if res is PackedScene:
			var inst: Node = (res as PackedScene).instantiate()
			if inst is Node3D:
				return inst as Node3D
			inst.free()
		push_warning("[DZFoot] GLB resource is not a 3D scene: %s" % path)
		return null
	if not FileAccess.file_exists(path):
		push_warning("[DZFoot] GLB not found: %s" % path)
		return null
	var gltf := GLTFDocument.new()
	var state := GLTFState.new()
	var err := gltf.append_from_file(path, state)
	if err != OK:
		push_warning("[DZFoot] Failed to parse GLB %s: err=%d" % [path, err])
		return null
	var scene: Node = state.generate_scene()
	if scene is Node3D:
		return scene as Node3D
	return null

func _load_glb_threaded(path: String) -> Node3D:
	# Load a GLB in the background so the loading screen keeps updating.
	if not ResourceLoader.exists(path):
		return _load_glb(path)
	var err := ResourceLoader.load_threaded_request(path)
	if err != OK:
		return _load_glb(path)
	while true:
		var status := ResourceLoader.load_threaded_get_status(path)
		if status == ResourceLoader.THREAD_LOAD_LOADED:
			var res := ResourceLoader.load_threaded_get(path)
			if res is PackedScene:
				var inst: Node = (res as PackedScene).instantiate()
				if inst is Node3D:
					return inst as Node3D
				inst.free()
			return null
		elif status == ResourceLoader.THREAD_LOAD_FAILED or status == ResourceLoader.THREAD_LOAD_INVALID_RESOURCE:
			return _load_glb(path)
		await get_tree().process_frame
	return null

func _create_line_box(parent: Node, pos: Vector3, size: Vector3, mat: Material) -> void:
	var box := BoxMesh.new()
	box.size = size
	var mi := MeshInstance3D.new()
	mi.mesh = box
	mi.material_override = mat
	mi.position = pos
	parent.add_child(mi)

func _create_pitch_material() -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.10, 0.34, 0.08, 1.0)
	mat.roughness = 0.85
	mat.metallic = 0.0
	return mat

func _draw_pitch_lines(parent: Node) -> void:
	var line_mat := StandardMaterial3D.new()
	line_mat.albedo_color = Color.WHITE
	line_mat.roughness = 0.9
	var line_w := 0.12
	# Pitch sits at y=0.05 (above stadium slabs) — lines go just on top of it.
	var ly := 0.065
	_create_line_box(parent, Vector3(55.0, ly, 0), Vector3(line_w, 0.02, 72), line_mat)
	_create_line_box(parent, Vector3(-55.0, ly, 0), Vector3(line_w, 0.02, 72), line_mat)
	_create_line_box(parent, Vector3(0, ly, 36), Vector3(110, 0.02, line_w), line_mat)
	_create_line_box(parent, Vector3(0, ly, -36), Vector3(110, 0.02, line_w), line_mat)
	_create_line_box(parent, Vector3(0, ly, 0), Vector3(line_w, 0.02, 72), line_mat)
	# Center circle approximation — a flat ring via a torus isn't in core; use
	# a thin stretched box per octant for a readable circle instead.
	var circle_mat := line_mat
	var r := 9.15
	for i in range(24):
		var a := TAU * float(i) / 24.0
		var seg_len := r * TAU / 24.0 * 1.15
		var pos := Vector3(cos(a) * r, ly, sin(a) * r)
		var mi := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = Vector3(seg_len, 0.02, line_w)
		mi.mesh = bm
		mi.material_override = circle_mat
		mi.position = pos
		mi.rotation.y = -(a + PI / 2)  # tangent to the circle, not radial
		parent.add_child(mi)
	# Penalty areas (16.5m deep x 40.32m wide) + six-yard boxes + penalty spot.
	var pa_d := 16.5; var pa_w := 20.16
	var sa_d := 5.5;  var sa_w := 9.16
	for side in [-1.0, 1.0]:
		var gx: float = 55.0 * side
		# Penalty box: front line + two side lines
		_create_line_box(parent, Vector3(gx - pa_d * side, ly, 0), Vector3(line_w, 0.02, pa_w * 2), line_mat)
		_create_line_box(parent, Vector3(gx - pa_d * side / 2, ly, pa_w), Vector3(pa_d, 0.02, line_w), line_mat)
		_create_line_box(parent, Vector3(gx - pa_d * side / 2, ly, -pa_w), Vector3(pa_d, 0.02, line_w), line_mat)
		# Six-yard box
		_create_line_box(parent, Vector3(gx - sa_d * side, ly, 0), Vector3(line_w, 0.02, sa_w * 2), line_mat)
		_create_line_box(parent, Vector3(gx - sa_d * side / 2, ly, sa_w), Vector3(sa_d, 0.02, line_w), line_mat)
		_create_line_box(parent, Vector3(gx - sa_d * side / 2, ly, -sa_w), Vector3(sa_d, 0.02, line_w), line_mat)
		# Penalty spot (11m) — small square marker
		_create_line_box(parent, Vector3(gx - 11.0 * side, ly, 0), Vector3(0.3, 0.02, 0.3), line_mat)
		# Penalty arc ("the D"): r=9.15 centered on spot, only the part outside the box.
		var spot := Vector3(gx - 11.0 * side, ly, 0)
		var arc_r := 9.15
		# Box front line is 5.5m from the spot -> arc spans |theta| <= acos(5.5/9.15).
		var th_max := acos((pa_d - 11.0) / arc_r)
		var arc_n := 16
		for i in range(arc_n + 1):
			var th: float = lerpf(-th_max, th_max, float(i) / float(arc_n))
			var seg_len := 2.0 * arc_r * sin(th_max / float(arc_n)) * 1.08
			var pos := spot + Vector3(-side * cos(th) * arc_r, 0, sin(th) * arc_r)
			var tx: float = side * sin(th)
			var tz: float = cos(th)
			var mi := MeshInstance3D.new()
			var bm := BoxMesh.new()
			bm.size = Vector3(seg_len, 0.02, line_w)
			mi.mesh = bm
			mi.material_override = line_mat
			mi.position = pos
			mi.rotation.y = atan2(-tz, tx)
			parent.add_child(mi)
	# Corner quarter circles (r=1m) at all 4 corners.
	var cr := 1.0
	for cx in [-55.0, 55.0]:
		for cz in [-36.0, 36.0]:
			var cx_f: float = cx
			var cz_f: float = cz
			var sx: float = signf(cx_f)
			var sz: float = signf(cz_f)
			for i in range(8):
				var a := (float(i) + 0.5) * (PI / 16.0)
				var pos := Vector3(cx_f - sx * cos(a) * cr, ly, cz_f - sz * sin(a) * cr)
				var mi := MeshInstance3D.new()
				var bm := BoxMesh.new()
				bm.size = Vector3(cr * PI / 16.0 * 1.15, 0.02, line_w)
				mi.mesh = bm
				mi.material_override = line_mat
				mi.position = pos
				mi.rotation.y = atan2(sz * cos(a), sx * sin(a))
				parent.add_child(mi)

func _add_mow_stripes(parent: Node, surface_y: float) -> void:
	# FIFA-style mowing stripes: alternating translucent dark bands across
	# the full pitch width, every 8m along the length.
	var stripe_mat := StandardMaterial3D.new()
	# Opaque darker green — alpha-blended planes don't render on Compatibility.
	# Reference look: alternating bands slightly DARKER than the base grass.
	stripe_mat.albedo_color = Color(0.86, 0.90, 0.84, 1.0)
	var grass_tex: Texture2D = load("res://textures/grass.png")
	if grass_tex:
		stripe_mat.albedo_texture = grass_tex
		stripe_mat.uv1_scale = Vector3(0.5, 4.0, 1.0)
	stripe_mat.roughness = 0.95
	var x := -55.0
	var stripe := 0
	while x < 55.0:
		if stripe % 2 == 0:
			var mi := MeshInstance3D.new()
			var pm := PlaneMesh.new()
			pm.size = Vector2(8.0, 72.0)
			pm.material = stripe_mat
			mi.mesh = pm
			mi.position = Vector3(x + 4.0, surface_y, 0)
			parent.add_child(mi)
		stripe += 1
		x += 8.0

func _fix_stadium_ground(stad_root: Node) -> void:
	# stadium_test.glb ships giant flat slabs (stadium_00: 128x88, _02/_04:
	# 180x140, _49: 200x160) at y≈0 with a white texture — they bury the grass.
	# Tint any flat horizontal slab: pitch-area apron green, surroundings asphalt.
	var apron := StandardMaterial3D.new()
	apron.albedo_color = Color(0.12, 0.38, 0.10, 1.0)  # darker green than pitch
	apron.roughness = 0.95
	var asphalt := StandardMaterial3D.new()
	asphalt.albedo_color = Color(0.30, 0.30, 0.33, 1.0)
	asphalt.roughness = 0.95
	var count := 0
	var stack: Array = [stad_root]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		if n is MeshInstance3D:
			var mi := n as MeshInstance3D
			if mi.mesh == null:
				continue
			var sz := mi.mesh.get_aabb().size
			# Flat horizontal slab: big XZ, negligible Y.
			if sz.y < 0.3 and sz.x > 60.0 and sz.z > 60.0:
				var inner := sz.x <= 150.0
				for s in mi.mesh.get_surface_count():
					mi.set_surface_override_material(s, apron if inner else asphalt)
				count += 1
	print("[DZFoot] Stadium ground slabs tinted: %d" % count)

func _override_pitch_texture(pitch_root: Node) -> void:
	# pitch.glb embeds a placeholder albedo (pitch_image_*.png with "01" tiles).
	# Replace it with the real grass texture, tiled across the pitch.
	var grass: Texture2D = load("res://textures/grass.png")
	if grass == null:
		push_warning("[DZFoot] grass.png not found, keeping GLB texture")
		return
	var stack: Array = [pitch_root]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		if n is MeshInstance3D:
			var mi := n as MeshInstance3D
			for s in mi.mesh.get_surface_count():
				var surf_mat := mi.get_active_material(s)
				if surf_mat is BaseMaterial3D:
					var tex := (surf_mat as BaseMaterial3D).albedo_texture
					if tex and tex.resource_path.contains("pitch_image"):
						mi.set_surface_override_material(s, _make_pitch_shader_material(grass))
						print("[DZFoot] Pitch texture overridden with grass.png on %s" % mi.name)

const PITCH_SHADER_CODE := """
shader_type spatial;
render_mode diffuse_lambert, specular_disabled;
uniform sampler2D grass_tex : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D noise_tex : filter_linear, repeat_enable;
varying vec3 wpos;
void vertex() {
	wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}
float mow(float coord) {
	float b = sin(fract(coord) * 6.2831853) * 0.5 + 0.5;
	b = smoothstep(0.0, 1.0, b);
	b = smoothstep(0.0, 1.0, b);
	b = smoothstep(0.0, 1.0, b);
	return b * 2.0 - 1.0;
}
void fragment() {
	vec2 p = wpos.xz;
	vec3 base = pow(vec3(0.243, 0.361, 0.165), vec3(2.2));
	vec3 tex = texture(grass_tex, p * 0.25).rgb;
	vec3 col = mix(base, tex, 0.15);
	float n = texture(noise_tex, p * 0.02).r - 0.5;
	col *= 1.0 + n * 0.08;
	float lit = 1.0 + 0.5 * (mow(p.x / 11.0) * 0.12 + mow(p.y / 11.0) * 0.10);
	col *= lit;
	float ao = 1.0 - pow(clamp(length(p / vec2(55.0, 36.0)) * 0.7, 0.0, 1.0), 1.5) * 0.18;
	col *= ao;
	col = mix(vec3(dot(col, vec3(0.2125, 0.7154, 0.0721))), col, 0.72);
	ALBEDO = col * 1.75;
	ROUGHNESS = 1.0;
}
"""

var _pitch_noise_tex: ImageTexture = null

func _make_pitch_shader_material(grass: Texture2D) -> ShaderMaterial:
	if _pitch_noise_tex == null:
		var fn := FastNoiseLite.new()
		fn.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
		fn.frequency = 0.012
		fn.fractal_type = FastNoiseLite.FRACTAL_FBM
		fn.fractal_octaves = 3
		var img := fn.get_seamless_image(256, 256)
		img.convert(Image.FORMAT_L8)
		_pitch_noise_tex = ImageTexture.create_from_image(img)
	var sh := Shader.new()
	sh.code = PITCH_SHADER_CODE
	var m := ShaderMaterial.new()
	m.shader = sh
	m.set_shader_parameter("grass_tex", grass)
	m.set_shader_parameter("noise_tex", _pitch_noise_tex)
	return m

func _setup_ball() -> void:
	# Try loading real ball GLB
	var ball_scene = _load_glb("res://models/ball.glb")
	if ball_scene:
		ball = _find_mesh_in_node(ball_scene)
		if ball:
			ball.name = "Ball"
			# ball.glb is 0.22m — too small to read on the broadcast camera.
			# FIFA-style readability: render it ~2x (0.44m) with strong emission.
			ball.scale = Vector3(1.05, 1.05, 1.05)
			# Force a clean white material — GLB may ship dark/untextured.
			var ball_mat := StandardMaterial3D.new()
			ball_mat.albedo_color = Color.WHITE
			ball_mat.roughness = 0.3
			ball_mat.metallic = 0.0
			ball_mat.emission_enabled = true
			ball_mat.emission = Color(0.45, 0.45, 0.45)
			for s in ball.mesh.get_surface_count():
				ball.set_surface_override_material(s, ball_mat)
			add_child(ball_scene)
			print("[DZFoot] Ball GLB loaded")
			return
		add_child(ball_scene)
	# Fallback: procedural ball
	var ball_mesh := SphereMesh.new()
	ball_mesh.radius = 0.11
	ball_mesh.height = 0.22
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color.WHITE
	mat.roughness = 0.3
	ball_mesh.material = mat
	ball = MeshInstance3D.new()
	ball.mesh = ball_mesh
	ball.name = "Ball"
	add_child(ball)
	print("[DZFoot] Ball fallback (procedural)")

func _find_mesh_in_node(node: Node) -> MeshInstance3D:
	if node is MeshInstance3D:
		return node as MeshInstance3D
	for child in node.get_children():
		var mesh := _find_mesh_in_node(child)
		if mesh:
			return mesh
	return null

var _player_template: Node3D = null

func _setup_players() -> void:
	# Try to load player_base.glb as a scene template (threaded: it is the
	# biggest model and embeds the animation clips)
	var player_scene = await _load_glb_threaded("res://models/player_base.glb")
	if player_scene:
		_player_template = player_scene
		print("[DZFoot] Player base GLB loaded, instancing %d players" % DZ_MAX_PLAYERS)
		for i in range(DZ_MAX_PLAYERS):
			var player := _clone_player(player_scene, i)
			players.append(player)
			add_child(player)
			if i % 6 == 5:
				await get_tree().process_frame
		await _setup_animation_library()
		return

	# Fallback: capsule players
	print("[DZFoot] Player GLB not found, using capsule fallback")
	for i in range(DZ_MAX_PLAYERS):
		var player := Node3D.new()
		player.name = "Player_%d" % i
		var body_mesh := CapsuleMesh.new()
		body_mesh.radius = 0.25
		body_mesh.height = 1.7
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color(0.2, 0.4, 0.9) if i < 11 else Color(0.9, 0.4, 0.2)
		mat.roughness = 0.7
		body_mesh.material = mat
		var body := MeshInstance3D.new()
		body.mesh = body_mesh
		body.position.y = 0.85
		player.add_child(body)
		var anim_player := AnimationPlayer.new()
		anim_player.name = "AnimationPlayer"
		player.add_child(anim_player)
		players.append(player)
		add_child(player)

const SKIN := Color(0.82, 0.65, 0.52)
const BOOT := Color(0.08, 0.08, 0.10)

func _clone_player(template: Node3D, idx: int) -> Node3D:
	var player := template.duplicate() as Node3D
	player.name = "Player_%d" % idx
	# player_base.glb is already in meters (1.91m tall) — no rescale.
	# Real kit: shirt/shorts/socks/boots/skin mapped by body-part mesh names.
	var is_away := idx >= 11
	var is_gk := (idx == 0 or idx == 11)
	var colored := _apply_kit(player, is_away, is_gk)
	if colored == 0:
		push_warning("[DZFoot] %s: no meshes colored" % player.name)
	return player

# Server-provided kit colors (MatchSetupPacket RGB) — fall back to defaults.
var _team_a_shirt := Color(0.95, 0.80, 0.10)
var _team_a_shorts := Color(0.12, 0.14, 0.45)
var _team_b_shirt := Color(0.10, 0.12, 0.42)
var _team_b_shorts := Color(0.92, 0.92, 0.94)

func _apply_kit(player: Node, is_away: bool, is_gk: bool) -> int:
	var shirt: Color
	var shorts: Color
	if is_gk:
		shirt = Color(0.15, 0.70, 0.20) if not is_away else Color(0.85, 0.35, 0.10)
		shorts = Color(0.15, 0.15, 0.18)
	elif is_away:
		shirt = _team_b_shirt
		shorts = _team_b_shorts
	else:
		shirt = _team_a_shirt
		shorts = _team_a_shorts
	return _apply_kit_to_meshes(player, shirt, shorts)

func _apply_kit_to_meshes(node: Node, shirt: Color, shorts: Color) -> int:
	var n_colored := 0
	for child in node.get_children():
		if child is MeshInstance3D:
			var mi := child as MeshInstance3D
			var name_l := mi.name.to_lower()
			var part_color: Color
			# Body-part kit mapping (mesh names from player_base.glb).
			if "middle" in name_l or "shoulder" in name_l:
				part_color = shirt          # torso + sleeves
			elif "thigh" in name_l or name_l == "body":
				part_color = shorts         # shorts cover pelvis+thighs
			elif "knee" in name_l:
				part_color = shirt.darkened(0.35)  # socks = darker shirt
			elif "ankle" in name_l:
				part_color = BOOT           # boots
			else:
				part_color = SKIN           # neck, elbows(hands), head…
			for s in mi.mesh.get_surface_count():
				if _is_face_surface(mi, s):
					mi.set_surface_override_material(s, _make_face_material(mi, s, 1))
					continue
				var mat := StandardMaterial3D.new()
				mat.roughness = 0.55
				mat.metallic = 0.0
				mat.albedo_color = part_color
				mi.set_surface_override_material(s, mat)
			n_colored += 1
		if child is Node:
			n_colored += _apply_kit_to_meshes(child, shirt, shorts)
	return n_colored

# === Dynamic animation collection setup ===
func _setup_animation_library() -> void:
	if _anim_lib_initialized or players.is_empty():
		return
	_anim_lib_initialized = true
	var anim_player := _find_first_anim_player(players[0])
	if anim_player == null:
		return
	# Collect every clip from every library into the default "" library,
	# so has_animation()/play() always find them.
	var default_lib := anim_player.get_animation_library("")
	for lib_name in anim_player.get_animation_library_list():
		var lib := anim_player.get_animation_library(lib_name)
		for anim_name in lib.get_animation_list():
			if not default_lib.has_animation(anim_name):
				default_lib.add_animation(anim_name, lib.get_animation(anim_name))
	# Rename legacy clips anim_XX -> category names so the 17-category
	# fallback path (ANIM_NAMES) actually finds them.
	for anim_id in range(ANIM_NAMES.size()):
		var legacy := "anim_%02d" % anim_id
		if default_lib.has_animation(legacy) and not default_lib.has_animation(ANIM_NAMES[anim_id]):
			default_lib.add_animation(ANIM_NAMES[anim_id], default_lib.get_animation(legacy))
	var baked := _fetch_baked_anims()
	if baked != null:
		_dyn_lib = baked
		for p in players:
			var bap := _find_first_anim_player(p)
			if bap != null:
				bap.add_animation_library("dzdyn", _dyn_lib)
		return
	# Extract bone track paths (bone name -> NodePath) from an existing clip.
	for anim_name in default_lib.get_animation_list():
		var anim: Animation = default_lib.get_animation(anim_name)
		for i in anim.get_track_count():
			if anim.track_get_type(i) == Animation.TYPE_ROTATION_3D:
				var p: NodePath = anim.track_get_path(i)
				if p.get_name_count() > 0:
					_bone_paths[p.get_name(p.get_name_count() - 1)] = p
	# Build the dynamic collection (parses templates, replays GenerateAutoAnims).
	if _bone_paths.size() >= 5:
		_anim_collection = DZAnimCollectionScript.new("res://anim_data")
		print("[DZFoot] Dynamic animation collection ready: %d clips, %d bone paths" % [_anim_collection.get_clip_count(), _bone_paths.size()])
	else:
		push_warning("[DZFoot] No bone paths found — dynamic animations disabled")
	# Eagerly generate ALL runtime clips into ONE shared "dzdyn" library BEFORE
	# any playback starts (~1ms/clip). Never mutated afterward — mixers never
	# see a library mutation mid-eval, which was the GLThread SIGSEGV cause.
	if _anim_collection != null:
		_dyn_lib = AnimationLibrary.new()
		var t0 := Time.get_ticks_msec()
		var ngen := 0
		var total: int = _anim_collection.get_clip_count()
		# Parallel generation: one collection instance per worker (each re-parses
		# anim_data — but instances share no mutable state, so it is thread-safe).
		# ~Nx speedup on multicore devices vs the 65s serial loop on emulator.
		var workers: int = clampi(OS.get_processor_count() - 2, 1, 8)
		var chunk := int(ceil(float(total) / float(workers)))
		var results: Array = []
		var task_ids: Array = []
		if workers >= 2:
			results.resize(workers)
			for w in range(workers):
				var slot: Array = []
				slot.resize(chunk)
				results[w] = slot
				var start := w * chunk
				var end_ := mini(start + chunk, total)
				if start < end_:
					task_ids.append(WorkerThreadPool.add_task(
						Callable(self, "_gen_clip_range").bind(slot, start, end_, _bone_paths)))
			while true:
				var all_done := true
				for tid in task_ids:
					if not WorkerThreadPool.is_task_completed(tid):
						all_done = false
						break
				var done_clips := 0
				for arr in results:
					if arr == null:
						continue
					for a in arr:
						if a != null:
							done_clips += 1
				_set_loading("Génération des animations %d%%" % int(100.0 * done_clips / total), 55 + 29.0 * float(done_clips) / float(total))
				if all_done:
					break
				await get_tree().process_frame
			for w in range(results.size()):
				var arr: Array = results[w]
				var base := w * chunk
				for i in range(arr.size()):
					var a = arr[i]
					if a != null:
						_dyn_lib.add_animation("c%05d" % (base + i), a)
						ngen += 1
		else:
			# Single-core fallback: serial generation with progress yields.
			for cid in range(total):
				var a: Animation = _anim_collection.get_animation(cid, _bone_paths)
				if a != null:
					_dyn_lib.add_animation("c%05d" % cid, a)
					ngen += 1
				if cid % 64 == 63:
					_set_loading("Génération des animations %d%%" % int(100.0 * cid / total), 55 + 29.0 * float(cid) / float(total))
					await get_tree().process_frame
		for p in players:
			var ap := _find_first_anim_player(p)
			if ap != null:
				ap.add_animation_library("dzdyn", _dyn_lib)
		print("[DZFoot] Pre-generated %d dyn clips in %dms" % [ngen, Time.get_ticks_msec() - t0])

func _fetch_baked_anims() -> AnimationLibrary:
	if not _baked_anim_requested:
		return null
	var t0 := Time.get_ticks_msec()
	var lib := ResourceLoader.load_threaded_get(BAKED_ANIM_PATH) as AnimationLibrary
	if lib == null:
		push_warning("[DZFoot] Baked animations failed to load — generating at runtime")
		return null
	var expected: int = int(lib.get_meta("clip_count", 0))
	var n := lib.get_animation_list().size()
	if n == 0 or (expected > 0 and n < expected - 8):
		push_warning("[DZFoot] Baked animations incomplete (%d/%d) — generating at runtime" % [n, expected])
		return null
	print("[DZFoot] Baked animations ready: %d clips (waited %dms)" % [n, Time.get_ticks_msec() - t0])
	return lib

func _gen_clip_range(slot: Array, start: int, end_: int, bone_paths: Dictionary) -> void:
	# Runs on a WorkerThreadPool thread. Shares the ONE collection via the
	# cache-free entry point — no shared mutable state, no per-worker re-parse.
	for cid in range(start, end_):
		slot[cid - start] = _anim_collection.generate_for_worker(cid, bone_paths)

func _find_first_anim_player(node: Node) -> AnimationPlayer:
	if node is AnimationPlayer:
		return node as AnimationPlayer
	for child in node.get_children():
		var found := _find_first_anim_player(child)
		if found:
			return found
	return null

func _setup_officials() -> void:
	# Referees share the player model — FIFA-style yellow/black kit.
	var ref_shirt := Color(0.95, 0.85, 0.05)
	var ref_shorts := Color(0.08, 0.08, 0.10)
	for i in range(3):
		var official: Node3D
		if _player_template:
			official = _player_template.duplicate() as Node3D
			_apply_kit_to_meshes(official, ref_shirt, ref_shorts)
		else:
			official = Node3D.new()
			var body_mesh := CapsuleMesh.new()
			body_mesh.radius = 0.22
			body_mesh.height = 1.6
			var mat := StandardMaterial3D.new()
			mat.albedo_color = ref_shirt
			mat.roughness = 0.7
			body_mesh.material = mat
			var body := MeshInstance3D.new()
			body.mesh = body_mesh
			body.position.y = 0.8
			official.add_child(body)
		official.name = "Official_%d" % i
		officials.append(official)
		add_child(official)

# === Touch UI ===

func _setup_touch_ui() -> void:
	_touch_canvas = CanvasLayer.new()
	_touch_canvas.layer = 10
	add_child(_touch_canvas)

	var root_control := Control.new()
	root_control.set_anchors_preset(Control.PRESET_FULL_RECT)
	root_control.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_touch_canvas.add_child(root_control)

	# Virtual joystick (bottom-left) — anchored so it tracks the real viewport
	# corner even if get_viewport().size wasn't final when _ready ran.
	_joy_base = _create_circle_control(_joy_radius, Color(1, 1, 1, 0.22))
	_joy_base.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_joy_base.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	_joy_base.position = Vector2(160, -160 - _joy_radius * 2.0)
	root_control.add_child(_joy_base)

	_joy_knob = _create_circle_control(_joy_radius * 0.4, Color(1, 1, 1, 0.5))
	_joy_knob.position = Vector2(_joy_radius - _joy_radius * 0.4, _joy_radius - _joy_radius * 0.4)
	_joy_base.add_child(_joy_knob)

	# Action buttons (bottom-right) — anchored, offsets relative to bottom-right corner
	for btn_name in BUTTON_DEFS:
		var def: Dictionary = BUTTON_DEFS[btn_name]
		var btn := _create_circle_control(BTN_RADIUS, def.color)
		btn.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
		btn.position = Vector2(-290 + def.offset.x, -240 + def.offset.y)
		btn.mouse_filter = Control.MOUSE_FILTER_IGNORE

		var label := Label.new()
		label.text = def.label
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		label.size = Vector2(90, 90)
		label.position = Vector2(0, 0)
		label.add_theme_font_size_override("font_size", 24)
		btn.add_child(label)

		root_control.add_child(btn)
		_action_buttons[btn_name] = {
			"control": btn,
			"mask": def.mask,
			"pressed": false,
			"pointer": -1,
			"base_pos": btn.position,
		}

	# Camera mode toggle (top-right) — client-side only, not sent to server.
	_cam_btn = _create_circle_control(46.0, Color(0.1, 0.1, 0.1, 0.7))
	_cam_btn.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	_cam_btn.position = Vector2(-120, 120)
	_cam_btn.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_cam_btn_label = Label.new()
	_cam_btn_label.text = "CAM"
	_cam_btn_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_cam_btn_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_cam_btn_label.size = Vector2(92, 92)
	_cam_btn_label.add_theme_font_size_override("font_size", 22)
	_cam_btn.add_child(_cam_btn_label)
	root_control.add_child(_cam_btn)

func _create_circle_control(radius: float, color: Color) -> Control:
	# Panel + StyleBoxFlat with fully rounded corners renders a solid circle —
	# no dependency on the CanvasItem 'draw' signal which doesn't fire for
	# empty Controls on all platforms.
	var panel := Panel.new()
	var style := StyleBoxFlat.new()
	if _software_gl:
		# Blend-mode canvas shader variants fail to link on SwiftShader →
		# semi-transparent items never paint. Use solid dark tones instead.
		color.a = 1.0
		color = color.darkened(0.25)
	style.bg_color = color
	var r := int(radius)
	style.corner_radius_top_left = r
	style.corner_radius_top_right = r
	style.corner_radius_bottom_left = r
	style.corner_radius_bottom_right = r
	panel.add_theme_stylebox_override("panel", style)
	panel.size = Vector2(radius * 2.0, radius * 2.0)
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return panel

# === UI Overlay (score, timer, team names) ===

func _setup_ui_overlay() -> void:
	_ui_layer = CanvasLayer.new()
	_ui_layer.name = "UIOverlay"
	_ui_layer.layer = 10
	add_child(_ui_layer)

	# Score label (top center)
	_score_label = Label.new()
	_score_label.name = "ScoreLabel"
	_score_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_score_label.position = Vector2(-100, 10)
	_score_label.size = Vector2(200, 40)
	_score_label.add_theme_font_size_override("font_size", 28)
	_score_label.add_theme_color_override("font_color", Color.WHITE)
	_score_label.add_theme_color_override("font_shadow_color", Color.BLACK)
	_score_label.add_theme_constant_override("shadow_offset_x", 2)
	_score_label.add_theme_constant_override("shadow_offset_y", 2)
	_score_label.text = "0 - 0"
	_ui_layer.add_child(_score_label)

	# Timer label (top center, below score)
	_timer_label = Label.new()
	_timer_label.name = "TimerLabel"
	_timer_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_timer_label.position = Vector2(-100, 48)
	_timer_label.size = Vector2(200, 30)
	_timer_label.add_theme_font_size_override("font_size", 20)
	_timer_label.add_theme_color_override("font_color", Color(0.9, 0.9, 0.9))
	_timer_label.add_theme_color_override("font_shadow_color", Color.BLACK)
	_timer_label.add_theme_constant_override("shadow_offset_x", 1)
	_timer_label.add_theme_constant_override("shadow_offset_y", 1)
	_timer_label.text = "00:00"
	_ui_layer.add_child(_timer_label)

	# Team A name (top left)
	_team_a_label = Label.new()
	_team_a_label.name = "TeamALabel"
	_team_a_label.position = Vector2(10, 10)
	_team_a_label.size = Vector2(200, 30)
	_team_a_label.add_theme_font_size_override("font_size", 18)
	_team_a_label.add_theme_color_override("font_color", Color(0.4, 0.6, 1.0))
	_team_a_label.add_theme_color_override("font_shadow_color", Color.BLACK)
	_team_a_label.add_theme_constant_override("shadow_offset_x", 1)
	_team_a_label.add_theme_constant_override("shadow_offset_y", 1)
	_team_a_label.text = _team_a_name
	_ui_layer.add_child(_team_a_label)

	# Team B name (top right)
	_team_b_label = Label.new()
	_team_b_label.name = "TeamBLabel"
	_team_b_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_team_b_label.position = Vector2(-210, 10)
	_team_b_label.size = Vector2(200, 30)
	_team_b_label.add_theme_font_size_override("font_size", 18)
	_team_b_label.add_theme_color_override("font_color", Color(1.0, 0.4, 0.3))
	_team_b_label.add_theme_color_override("font_shadow_color", Color.BLACK)
	_team_b_label.add_theme_constant_override("shadow_offset_x", 1)
	_team_b_label.add_theme_constant_override("shadow_offset_y", 1)
	_team_b_label.text = _team_b_name
	_ui_layer.add_child(_team_b_label)

func _update_ui_overlay() -> void:
	if not _ui_layer:
		return
	var s := "%d - %d" % [_score_a, _score_b]
	if _score_label.text != s:
		_score_label.text = s
	var t := "%02d:%02d" % [int(_game_timer) / 60, int(_game_timer) % 60]
	if _timer_label.text != t:
		_timer_label.text = t
	if _team_a_label.text != _team_a_name:
		_team_a_label.text = _team_a_name
	if _team_b_label.text != _team_b_name:
		_team_b_label.text = _team_b_name

# === Process loop ===

func _process(delta: float) -> void:
	if not _scene_ready:
		# Still in async _ready() — ball/players/camera/UI may not exist yet.
		return
	_heartbeat += delta
	if _heartbeat >= 60.0:
		_heartbeat = 0.0
		_heartbeat_total += 60
		print("[DZFoot] heartbeat t=%ds states=%d" % [_heartbeat_total, _states_received])
	# Process incoming game state
	if dz_bridge and dz_bridge.has_new_state():
		_store_interpolation_state(dz_bridge)
		_states_received += 1
		_apply_game_state(dz_bridge)
		dz_bridge.consume_state()

	# Consume match events (goal banner, cards, corners...)
	_consume_events()
	_consume_tactical()

	# Apply interpolation
	_apply_interpolation(delta)
	if DEBUG_VERIFY:
		_verify_motion(delta)

	# Camera: smooth-follow server target (position pull + zoom + quat slerp)
	_update_camera_smooth(delta)

	# Ball roll / blob shadow / shot trail
	_update_ball_extras(delta)
	_update_player_blobs()

	# Active player ring + name label
	_update_active_marker()

	# Radar redraw at ~10Hz
	_radar_timer += delta
	if _radar_timer > 0.1 and _radar:
		_radar_timer = 0.0
		_radar.queue_redraw()

	# Build and send input packet — only once the pitch is actually displayed,
	# so the server never kicks off before the player can see the game.
	if _display_ok:
		_update_and_send_input()

	# Broadcast scoreboard refresh
	_update_broadcast_hud()

	# Redraw touch UI if needed
	if _touch_canvas:
		_touch_canvas.visible = _ui_visible

func _verify_motion(delta: float) -> void:
	if dz_bridge == null or not _state_received or players.size() < DZ_MAX_PLAYERS:
		return
	_verify_t += delta
	if _verify_t < 2.0:
		return
	var dt := _verify_t
	_verify_t = 0.0
	_bad_clip_logs = 0
	if camera != null:
		print("[DZ_CAM] pos=%s fov=%.1f tgtFov=%.1f raw=%s look=%s dist=%.1f" % [str(camera.position), camera.fov, _cam_target_fov, str(_cam_raw_pos), str(_cam_look_pt), camera.position.distance_to(_cam_look_pt)])
	if _verify_prev.is_empty():
		for i in range(DZ_MAX_PLAYERS):
			_verify_prev.append(players[i].position)
			_verify_shoulders.append([players[i].find_child("left_shoulder", true, false), players[i].find_child("right_shoulder", true, false)])
		return
	if dz_bridge.get("_state_data") != null:
		var sd: PackedByteArray = dz_bridge.get("_state_data")
		for pi in [1, 6]:
			var o: int = 72 + pi * 48
			if sd.size() >= o + 48:
				var fl := ""
				for k in range(0, 48, 4):
					fl += "%.3f " % sd.decode_float(o + k)
				var by := ""
				for k in range(32, 48):
					by += "%02X " % sd[o + k]
				print("[DZ_RAW] p%d floats[0..11]=%s | bytes[32..47]=%s" % [pi, fl, by])
		var oo: int = 1128
		if sd.size() >= oo + 32:
			var ofl := ""
			for k in range(0, 28, 4):
				ofl += "%.3f " % sd.decode_float(oo + k)
			print("[DZ_RAW] referee floats[0..6]=%s | bytes[28..31]=%02X %02X %02X %02X" % [ofl, sd[oo + 28], sd[oo + 29], sd[oo + 30], sd[oo + 31]])
	var n := 0
	var sum_srv := 0.0
	var sum_vis := 0.0
	var neg_srv := 0
	var neg_vis := 0
	var sample := ""
	for i in range(DZ_MAX_PLAYERS):
		var disp: Vector3 = players[i].position - _verify_prev[i]
		_verify_prev[i] = players[i].position
		disp.y = 0.0
		var spd := disp.length() / dt
		if spd < 3.0:
			continue
		var mdir := disp.normalized()
		var rot_y: float = dz_bridge.get_player_rotation_y(i)
		var sdir := Vector3(sin(rot_y), 0.0, -cos(rot_y))
		var c_srv := mdir.dot(sdir)
		var sh: Array = _verify_shoulders[i]
		var c_vis := 0.0
		if sh[0] != null and sh[1] != null:
			var r: Vector3 = (sh[1] as Node3D).global_position - (sh[0] as Node3D).global_position
			var fwd := Vector3.UP.cross(r)
			fwd.y = 0.0
			if fwd.length() > 0.001:
				c_vis = mdir.dot(fwd.normalized())
		n += 1
		sum_srv += c_srv
		sum_vis += c_vis
		if c_srv < 0.0:
			neg_srv += 1
		if c_vis < 0.0:
			neg_vis += 1
		if sample == "":
			sample = "p%d spd=%.1f clip=%d ft=%d cSrv=%.2f cVis=%.2f" % [i, spd, dz_bridge.get_player_clip_id(i), dz_bridge.get_player_function_type(i), c_srv, c_vis]
	if n > 0:
		print("[DZ_VERIFY] moving=%d avgCosSrvDirVsMotion=%.2f (neg=%d) avgCosVisualFacingVsMotion=%.2f (neg=%d) clipHit=%d miss=%d | %s" % [n, sum_srv / n, neg_srv, sum_vis / n, neg_vis, _clip_hits, _clip_misses, sample])
	else:
		print("[DZ_VERIFY] no player >3m/s this window; clipHit=%d miss=%d" % [_clip_hits, _clip_misses])

func _update_camera_smooth(delta: float) -> void:
	if not _cam_have_target or camera == null:
		return
	# Pull raw position toward look point (broadcast framing fix), then apply
	# goal-zoom factor on top of the fixed distance scale.
	var k: float = 1.0 - exp(-CAM_SMOOTH_RATE * delta)
	if _cam_mode == 1 and ball != null:
		# Action cam: closer, lower, follows the ball from the +Z touchline side.
		var bpos := ball.position
		var apos := bpos + Vector3(0, 7.0, 18.0)
		camera.position = camera.position.lerp(apos, k)
		var aq := Basis.looking_at((bpos - apos).normalized(), Vector3.UP).get_rotation_quaternion()
		camera.quaternion = camera.quaternion.slerp(aq, k)
		camera.fov = lerpf(camera.fov, 45.0, k)
		return
	if Time.get_ticks_msec() < _goal_cam_until_ms and players.size() >= DZ_MAX_PLAYERS:
		# Goal celebration: frame the scorer (fall back to the ball).
		var focus := Vector3(ball.position.x, 0.0, ball.position.z)
		if _goal_scorer_idx >= 0 and _goal_scorer_idx < players.size():
			var sc := players[_goal_scorer_idx].position
			focus = Vector3(sc.x, 0.0, sc.z)
		focus.y = 1.0
		var gpos := focus + Vector3(-signf(focus.x) * 9.0, 6.0, 12.0)
		camera.position = camera.position.lerp(gpos, k)
		var gq := Basis.looking_at((focus - gpos).normalized(), Vector3.UP).get_rotation_quaternion()
		camera.quaternion = camera.quaternion.slerp(gq, k)
		camera.fov = lerpf(camera.fov, 42.0, k)
		return
	var dist_scale: float = CAM_DIST_SCALE
	var target_pos := _cam_look_pt + (_cam_raw_pos - _cam_look_pt) * dist_scale
	if target_pos.y < CAM_MIN_HEIGHT:
		target_pos.y = CAM_MIN_HEIGHT
	camera.position = camera.position.lerp(target_pos, k)
	camera.quaternion = camera.quaternion.slerp(_cam_target_quat, k)
	camera.fov = lerpf(camera.fov, _cam_target_fov, k)

func _store_interpolation_state(bridge: Object) -> void:
	# Store current positions as previous, capture new as current
	for i in range(DZ_MAX_PLAYERS):
		_prev_positions[i] = _curr_positions[i]
		var pos = bridge.get_player_position(i)
		_curr_positions[i] = _gf_to_godot(pos)

	_prev_ball_pos = _curr_ball_pos
	var bpos = bridge.get_ball_position()
	_curr_ball_pos = _gf_to_godot(bpos)

	_state_interp_time = 0.0
	_state_received = true

func _apply_interpolation(delta: float) -> void:
	if not _state_received:
		return
	_state_interp_time += delta
	var alpha: float = clampf(_state_interp_time / _state_interp_period, 0.0, 1.0)

	# Interpolate ball
	ball.position = _prev_ball_pos.lerp(_curr_ball_pos, alpha)
	# Ball is rendered at 2x scale (radius 0.15) — keep it resting on the turf.
	ball.position.y = maxf(ball.position.y, _pitch_top_y + 0.12)

	# Interpolate players
	for i in range(DZ_MAX_PLAYERS):
		players[i].position = _prev_positions[i].lerp(_curr_positions[i], alpha)

func _apply_game_state(bridge: Object) -> void:
	var tick: int = bridge.get_tick()
	if tick == last_tick:
		return
	last_tick = tick

	# Detect active player from flags (flag bit 2 = 0x04 = human-controlled).
	# Team is derived from index: players 0-10 = team A, 11-21 = team B
	# (GameServer writes idx = i + teamId*11). Reading the team byte from
	# state is fragile — a stale layout once produced team=15, which the
	# server drops (input.team <= 1), freezing the match at kickoff.
	for i in range(DZ_MAX_PLAYERS):
		var flags: int = bridge.get_player_flags(i)
		if flags & 4:
			if i != _active_player_idx:
				print("[DZ_ACTIVE] %d -> %d" % [_active_player_idx, i])
			_active_player_idx = i
			_active_team = i / 11
			bridge.set_active_player(i, _active_team)
			break

	# Players: rotation, animation (positions handled by interpolation)
	for i in range(DZ_MAX_PLAYERS):
		var p := players[i]
		var rot_y: float = bridge.get_player_rotation_y(i)
		# Model faces +Z; GF heading rotY=atan2(dx,dy) -> Godot forward (dx,0,-dy)
		# => rotation.y = PI - rotY (verified headless: avg cos = +1.000 vs server dir).
		p.rotation.y = PI - rot_y

		var anim_id: int = bridge.get_player_anim(i)
		var clip_id: int = bridge.get_player_clip_id(i)
		var progress: float = bridge.get_player_anim_progress(i) / 65535.0
		if clip_id >= 0xFF00:
			# Legacy 17-category fallback (e.g. forced celebration)
			_play_anim(p, clip_id & 0xFF, progress)
		else:
			# Exact desktop-selected clip from the GF AnimCollection
			_play_clip(p, clip_id, progress, anim_id)

	# Officials — position, rotation AND animation category (idle/walk/run/sprint)
	for i in range(3):
		var o := officials[i]
		var pos = bridge.get_official_position(i)
		o.position = _gf_to_godot(pos)
		o.rotation.y = PI - bridge.get_official_rotation_y(i)
		_official_positions[i] = o.position
		_play_anim(o, bridge.get_official_anim(i))

	# Camera — store raw targets; actual transform applied smoothly in _process.
	var cam_pos = bridge.get_camera_position()
	var cam_rot = bridge.get_camera_rotation()
	_cam_target_fov = clampf(bridge.get_camera_fov(), 15.0, 70.0)
	_cam_raw_pos = _gf_to_godot(cam_pos)
	var quat := Quaternion(cam_rot.x, cam_rot.y, cam_rot.z, cam_rot.w)
	_cam_target_quat = _convert_gf_quaternion(quat)
	_cam_have_target = true

	# GF broadcast camera sits very high — compute look point on the pitch so
	# we can pull the smoothed position toward it each frame.
	var tmp_quat := _cam_target_quat
	var fwd := -(Basis(tmp_quat).z)
	if fwd.y < -0.01:
		var t := _cam_raw_pos.y / -fwd.y
		_cam_look_pt = _cam_raw_pos + fwd * t
	else:
		_cam_look_pt = _cam_raw_pos
	if not _cam_diag_printed:
		_cam_diag_printed = true
		print("[DZ_DIAG] cam_pos=%s fwd=%s fov=%s env=%s quat=%s" % [str(_cam_raw_pos), str(fwd), str(_cam_target_fov), str(cam_pos), str(cam_rot)])

	# Update score and timer from game state
	_score_a = bridge.get_score(0)
	_score_b = bridge.get_score(1)
	_game_timer = bridge.get_timer()

func _convert_gf_quaternion(q: Quaternion) -> Quaternion:
	# GF Z-up -> Godot Y-up (same basis change as _gf_to_godot), then rotate
	# the local frame by -90° around X: GF camera looks along its local -Z which
	# maps to Godot -Y; Godot Camera3D looks along -Z, so compensate locally.
	return Quaternion(q.x, q.z, -q.y, q.w) * Quaternion(Vector3(1, 0, 0), -PI / 2.0)

func _play_anim(player: Node3D, anim_id: int, progress: float = -1.0) -> void:
	if DEBUG_NO_ANIM:
		return
	if anim_id < 0 or anim_id >= ANIM_NAMES.size():
		return
	var anim_player := _find_first_anim_player(player)
	if not anim_player:
		return
	var anim_name: String = ANIM_NAMES[anim_id]
	if anim_player.has_animation(anim_name):
		if anim_player.current_animation != anim_name:
			anim_player.play(anim_name)
			# Seek to the server's current frame so the anim doesn't restart from 0
			if progress >= 0.0:
				var anim_len: float = anim_player.current_animation_length
				if anim_len > 0.0:
					anim_player.seek(progress * anim_len, true)
	elif anim_player.current_animation != "idle" and anim_player.has_animation("idle"):
		anim_player.play("idle")

func _play_clip(player: Node3D, clip_id: int, progress: float, fallback_cat: int) -> void:
	if DEBUG_NO_ANIM:
		return
	if DEBUG_NO_DYNCLIP:
		_play_anim(player, fallback_cat, progress)
		return
	# Play the exact server-selected clip from the GF AnimCollection.
	var anim_player := _find_first_anim_player(player)
	if not anim_player:
		return
	var clip_key := "dzdyn/c%05d" % clip_id
	if not anim_player.has_animation(clip_key):
		# All valid clips are pre-generated at setup — a miss means the server
		# sent an invalid clip_id. Use the 17-category fallback.
		_clip_misses += 1
		_play_anim(player, fallback_cat, progress)
		return
	_clip_hits += 1
	var anim := anim_player.get_animation(clip_key)
	if anim.loop_mode != Animation.LOOP_LINEAR:
		anim.loop_mode = Animation.LOOP_LINEAR
	var clip_len: float = anim.length
	if clip_len <= 0.0:
		return
	if anim_player.current_animation != clip_key:
		anim_player.play(clip_key)
		if progress >= 0.0:
			anim_player.seek(progress * clip_len, false)
	elif progress >= 0.0:
		# Drift correction on clip requeue: resync when the local playback phase
		# drifted too far from the server frame (circular distance).
		var cur: float = fmod(anim_player.current_animation_position, clip_len)
		var target: float = progress * clip_len
		var diff: float = absf(cur - target)
		if diff > clip_len * 0.5:
			diff = clip_len - diff
		if diff > clip_len * 0.3:
			anim_player.seek(target, false)

func _is_usable_color(c: Color) -> bool:
	# Uninitialized server colors come as pure white (255,255,255) or black.
	return c.v > 0.05 and c.v < 0.95

func _apply_match_setup() -> void:
	if not dz_bridge or not dz_bridge.has_match_setup():
		return

	_team_a_name = dz_bridge.get_team_a_name()
	_team_b_name = dz_bridge.get_team_b_name()
	print("[DZFoot] Match setup: %s vs %s" % [_team_a_name, _team_b_name])

	# Pull real kit colors from the server (RGB in MatchSetupPacket).
	if dz_bridge.has_method("get_team_a_color"):
		var ca: Color = dz_bridge.get_team_a_color(true)
		var ca2: Color = dz_bridge.get_team_a_color(false)
		var cb: Color = dz_bridge.get_team_b_color(true)
		var cb2: Color = dz_bridge.get_team_b_color(false)
		if _is_usable_color(ca):
			_team_a_shirt = ca
			_team_a_shorts = ca2 if _is_usable_color(ca2) else ca.darkened(0.5)
		if _is_usable_color(cb) and Vector3(cb.r, cb.g, cb.b).distance_to(Vector3(_team_a_shirt.r, _team_a_shirt.g, _team_a_shirt.b)) > 0.25:
			_team_b_shirt = cb
			_team_b_shorts = cb2 if _is_usable_color(cb2) else cb.darkened(0.5)
		print("[DZFoot] Kit colors A=%s/%s B=%s/%s" % [_team_a_shirt, _team_a_shorts, _team_b_shirt, _team_b_shorts])

	# Apply team kits to player meshes (server tells which team each player is on)
	for i in range(DZ_MAX_PLAYERS):
		_apply_kit(players[i], i >= 11, i == 0 or i == 11)
		_apply_player_appearance(i)

func _setup_demo_mode() -> void:
	# A match setup may already be buffered (it arrived while _demo_timer
	# didn't exist yet during async _ready()). Don't stomp real data with demo.
	if dz_bridge and dz_bridge.has_match_setup():
		print("[DZFoot] Demo skipped — match setup already buffered")
		return
	print("[DZFoot] No server data received — activating demo mode")
	if _demo_timer and is_instance_valid(_demo_timer):
		_demo_timer.stop()
		_demo_timer = null

	_team_a_name = "DEMO A"
	_team_b_name = "DEMO B"
	_score_a = 0
	_score_b = 0
	_game_timer = 0.0

	# Demo formations (4-4-2). Pitch roughly 100x60 units.
	var team_a_positions = [
		Vector3(-45, 0, 0),      # GK
		Vector3(-35, 0, -15), Vector3(-35, 0, -5), Vector3(-35, 0, 5), Vector3(-35, 0, 15),
		Vector3(-20, 0, -15), Vector3(-20, 0, -5), Vector3(-20, 0, 5), Vector3(-20, 0, 15),
		Vector3(-10, 0, -8), Vector3(-10, 0, 8)
	]
	var team_b_positions = [
		Vector3(45, 0, 0),       # GK
		Vector3(35, 0, -15), Vector3(35, 0, -5), Vector3(35, 0, 5), Vector3(35, 0, 15),
		Vector3(20, 0, -15), Vector3(20, 0, -5), Vector3(20, 0, 5), Vector3(20, 0, 15),
		Vector3(10, 0, -8), Vector3(10, 0, 8)
	]

	for i in range(DZ_MAX_PLAYERS):
		if i < 11:
			_apply_kit(players[i], false, i == 0)
			players[i].position = team_a_positions[i]
			players[i].rotation.y = 0.0
		else:
			_apply_kit(players[i], true, i == 11)
			players[i].position = team_b_positions[i - 11]
			players[i].rotation.y = PI
		_prev_positions[i] = players[i].position
		_curr_positions[i] = players[i].position
		_play_anim(players[i], ANIM_IDLE)

	# Ball at center
	ball.position = Vector3.ZERO
	_prev_ball_pos = Vector3.ZERO
	_curr_ball_pos = Vector3.ZERO

	# Officials around the pitch
	if officials.size() >= 3:
		officials[0].position = Vector3(0, 0, -20)
		officials[1].position = Vector3(0, 0, 35)
		officials[2].position = Vector3(0, 0, -35)

	# Pick a human-controlled player (Team A forward #10)
	_active_player_idx = 9
	_active_team = 0
	if dz_bridge:
		dz_bridge.set_active_player(_active_player_idx, _active_team)

	_state_received = true
	_state_interp_time = 0.0
	_update_ui_overlay()

# === Input handling ===

func _input(event: InputEvent) -> void:
	if event is InputEventScreenTouch:
		_handle_screen_touch(event)
	elif event is InputEventScreenDrag:
		_handle_screen_drag(event)
	elif event is InputEventKey:
		_handle_key_input(event)

func _handle_screen_touch(event: InputEventScreenTouch) -> void:
	if _joy_base == null:
		return
	var pos := event.position
	print("[DZ_TOUCH] idx=%d pressed=%s pos=(%.0f,%.0f)" % [event.index, str(event.pressed), pos.x, pos.y])
	if event.pressed:
		_last_touch_ms = Time.get_ticks_msec()
		# Camera mode toggle
		if _cam_btn != null:
			var cam_pos := _cam_btn.global_position + Vector2(46, 46)
			if pos.distance_to(cam_pos) < 90:
				_cam_mode = (_cam_mode + 1) % 2
				if _cam_btn_label:
					_cam_btn_label.text = "CAM" if _cam_mode == 0 else "ACT"
				print("[DZ_CAM] mode=%d" % _cam_mode)
				return
		# Check joystick
		var joy_screen_pos := _joy_base.global_position + Vector2(_joy_radius, _joy_radius)
		if pos.distance_to(joy_screen_pos) < _joy_radius * 1.5 and _joy_pointer == -1:
			_joy_pointer = event.index
			_joy_active = true
			_joy_center = joy_screen_pos
			_update_joystick(pos)
			return

		# Check action buttons
		for btn_name in _action_buttons:
			var btn_info: Dictionary = _action_buttons[btn_name]
			var btn_ctrl: Control = btn_info.control
			var btn_pos := btn_ctrl.global_position + Vector2(45, 45)
			if pos.distance_to(btn_pos) < 80 and btn_info.pointer == -1:
				btn_info.pointer = event.index
				btn_info.pressed = true
				_buttons |= btn_info.mask
				_btn_latch[btn_info.mask] = Time.get_ticks_msec() + 200
				_button_pointers[event.index] = btn_name
				_update_button_visual(btn_name, true)
				return
	else:
		# Release joystick
		if event.index == _joy_pointer:
			_joy_pointer = -1
			_joy_active = false
			_joy_dir = Vector2.ZERO
			_move_dir = Vector2.ZERO
			_reset_joystick_visual()
			return

		# Release button
		if _button_pointers.has(event.index):
			var btn_name: String = _button_pointers[event.index]
			var btn_info: Dictionary = _action_buttons[btn_name]
			btn_info.pressed = false
			btn_info.pointer = -1
			_buttons &= ~btn_info.mask
			_button_pointers.erase(event.index)
			_update_button_visual(btn_name, false)
			return

func _handle_screen_drag(event: InputEventScreenDrag) -> void:
	if event.index == _joy_pointer and _joy_active:
		_update_joystick(event.position)

func _update_joystick(pos: Vector2) -> void:
	var delta := pos - _joy_center
	var len := delta.length()
	if len > _joy_radius:
		delta = delta.normalized() * _joy_radius
		len = _joy_radius
	_joy_dir = delta / _joy_radius
	if _joy_dir.length() < JOY_DEADZONE:
		_joy_dir = Vector2.ZERO
	if _joy_knob:
		_joy_knob.position = Vector2(_joy_radius, _joy_radius) + delta - Vector2(_joy_radius * 0.4, _joy_radius * 0.4)

	# Map to game direction: screen X = world X, screen Y down = world Z forward
	_move_dir.x = _joy_dir.x
	_move_dir.y = -_joy_dir.y  # invert Y: up on stick = forward

func _reset_joystick_visual() -> void:
	if _joy_knob:
		_joy_knob.position = Vector2(_joy_radius - _joy_radius * 0.4, _joy_radius - _joy_radius * 0.4)

func _update_button_visual(btn_name: String, pressed: bool) -> void:
	var btn_info: Dictionary = _action_buttons[btn_name]
	var ctrl: Control = btn_info.control
	if pressed:
		ctrl.modulate = Color(1.5, 1.5, 1.5, 1.0)
	else:
		ctrl.modulate = Color(1.0, 1.0, 1.0, 1.0)
	ctrl.queue_redraw()

func _handle_key_input(event: InputEventKey) -> void:
	if event.pressed:
		match event.physical_keycode:
			KEY_A, KEY_LEFT: _move_dir.x -= 1
			KEY_D, KEY_RIGHT: _move_dir.x += 1
			KEY_W, KEY_UP: _move_dir.y -= 1
			KEY_S, KEY_DOWN: _move_dir.y += 1
			KEY_J: _buttons |= BUTTON_PASS
			KEY_K: _buttons |= BUTTON_KICK
			KEY_L: _buttons |= BUTTON_SHOT
			KEY_SHIFT: _buttons |= BUTTON_SPRINT
			KEY_SPACE: _buttons |= BUTTON_SWITCH_PLAYER
			KEY_Q: _buttons |= BUTTON_HIGH_PASS
			KEY_E: _buttons |= BUTTON_DRIBBLE
			KEY_F: _buttons |= BUTTON_SLIDING
	else:
		match event.physical_keycode:
			KEY_A, KEY_LEFT: _move_dir.x += 1
			KEY_D, KEY_RIGHT: _move_dir.x -= 1
			KEY_W, KEY_UP: _move_dir.y += 1
			KEY_S, KEY_DOWN: _move_dir.y -= 1
			KEY_J: _buttons &= ~BUTTON_PASS
			KEY_K: _buttons &= ~BUTTON_KICK
			KEY_L: _buttons &= ~BUTTON_SHOT
			KEY_SHIFT: _buttons &= ~BUTTON_SPRINT
			KEY_SPACE: _buttons &= ~BUTTON_SWITCH_PLAYER
			KEY_Q: _buttons &= ~BUTTON_HIGH_PASS
			KEY_E: _buttons &= ~BUTTON_DRIBBLE
			KEY_F: _buttons &= ~BUTTON_SLIDING

	_move_dir = _move_dir.clampf(-1.0, 1.0)

func _update_and_send_input() -> void:
	if not dz_bridge or not dz_plugin:
		return

	# Apply camera-relative rotation to input direction.
	# Godot Camera3D looks along -Z, so forward is -basis.z (basis.z is backward).
	var cam_forward := -camera.global_transform.basis.z
	var cam_fwd_xz := Vector2(cam_forward.x, cam_forward.z).normalized()
	if cam_fwd_xz.length() > 0.001:
		var cam_right := Vector2(-cam_fwd_xz.y, cam_fwd_xz.x)
		var world_x := _move_dir.x * cam_right.x + _move_dir.y * cam_fwd_xz.x
		var world_z := _move_dir.x * cam_right.y + _move_dir.y * cam_fwd_xz.y
		# Godot (x,z) -> GF env direction (x, -y): Godot z = -GF y
		dz_bridge.set_input_direction(world_x, -world_z)
	else:
		dz_bridge.set_input_direction(_move_dir.x, -_move_dir.y)

	var eff_buttons: int = _buttons
	var now_ms: int = Time.get_ticks_msec()
	if _match_over:
		eff_buttons = 0
		dz_bridge.set_input_direction(0.0, 0.0)
	# If a set piece is waiting and the user hasn't touched for a while, auto-take
	# it with a short pass so the match doesn't freeze (AI takes over). Retries
	# every 2.5s until the set piece resolves — a single burst can be lost.
	var auto_wait_ms: int = KICKOFF_AUTO_DELAY_MS if _set_piece_type == 1 else 3000
	if _set_piece_type > 0 and now_ms - _last_touch_ms > auto_wait_ms and now_ms - _set_piece_since_ms > auto_wait_ms and now_ms >= _auto_take_next_ms:
		_auto_take_next_ms = now_ms + 2500
		_btn_latch[BUTTON_PASS] = now_ms + 600
		_auto_dir_x = 1.0 if _active_team == 0 else -1.0
		_auto_dir_ms = now_ms + 600
		print("[DZ_AUTO] auto-taking set piece type=%d dir=%.1f" % [_set_piece_type, _auto_dir_x])
	for m in _btn_latch.keys():
		if now_ms < int(_btn_latch[m]):
			eff_buttons |= int(m)
		else:
			_btn_latch.erase(m)
	# Hold the attack direction for the whole press window — otherwise only the
	# first packet carries it, the following ones go out with dir=(0,0).
	if now_ms < _auto_dir_ms:
		dz_bridge.set_input_direction(_auto_dir_x, 0.0)
	dz_bridge.set_input_buttons(eff_buttons)
	dz_bridge.set_active_player(_active_player_idx, _active_team)

	# Serialize and send to Kotlin via plugin
	var input_bytes = dz_bridge.get_input_bytes()
	if input_bytes.size() > 0:
		dz_plugin.set_input_packet(input_bytes)

	# Update UI overlay
	_update_ui_overlay()

# ============================================================================
# TASK B — PLAYER APPEARANCE (body / skin / hair / beard / height / name)
# ============================================================================

func _load_appearance_assets() -> void:
	# 12 GLBs + 7 skin textures. Sequential ResourceLoader.load() serializes
	# pck I/O + GLB parse on the main thread (seconds on slow devices) — fire
	# all threaded requests at once so they overlap on worker cores.
	var paths: Array = []
	for n in ["body_thin", "body_average", "body_muscular", "body_heavy",
			"hair_short", "hair_long", "hair_mohawk", "hair_curly", "hair_ponytail",
			"beard_stubble", "beard_short", "beard_full"]:
		paths.append("res://models/%s.glb" % n)
	for i in range(7):
		paths.append("res://textures/skin_%d.png" % i)
	for p in paths:
		if ResourceLoader.exists(p):
			ResourceLoader.load_threaded_request(p)
	while true:
		var pending := 0
		for p in paths:
			if ResourceLoader.exists(p) and ResourceLoader.load_threaded_get_status(p) == ResourceLoader.THREAD_LOAD_IN_PROGRESS:
				pending += 1
		if pending == 0:
			break
		await get_tree().process_frame
	for i in range(4):
		_body_templates.append(_glb_res_to_node(paths[i]))
	for i in range(4, 9):
		_hair_templates.append(_glb_res_to_node(paths[i]))
	for i in range(9, 12):
		_beard_templates.append(_glb_res_to_node(paths[i]))
	for i in range(12, 19):
		_skin_textures.append(_threaded_or_sync_load(paths[i]))

func _threaded_or_sync_load(path: String) -> Resource:
	if ResourceLoader.exists(path) and ResourceLoader.load_threaded_get_status(path) == ResourceLoader.THREAD_LOAD_LOADED:
		return ResourceLoader.load_threaded_get(path)
	return load(path)

func _glb_res_to_node(path: String) -> Node3D:
	var res := _threaded_or_sync_load(path)
	if res is PackedScene:
		var inst: Node = (res as PackedScene).instantiate()
		if inst is Node3D:
			return inst as Node3D
		inst.free()
	return null

func _apply_player_appearance(idx: int) -> void:
	if idx < 0 or idx >= players.size() or not dz_bridge:
		return
	var p: Node3D = players[idx]
	var height: float = dz_bridge.get_match_player_height(idx)
	if height < 1.2 or height > 2.3:
		height = 1.80
	var sc: float = height / 1.91
	_player_heights[idx] = sc
	# bodyType (0=thin,1=average,2=muscular,3=heavy): width factor — the GLB
	# body templates share one rig, so a lateral scale approximates the swap.
	var bw: float = [0.92, 1.0, 1.08, 1.17][clampi(dz_bridge.get_match_player_body_type(idx), 0, 3)]
	p.scale = Vector3(sc * bw, sc, sc * bw)
	var skin_idx: int = dz_bridge.get_match_player_skin(idx)
	var skin_tex: Texture2D = _skin_textures[skin_idx] if skin_idx < _skin_textures.size() else null
	_apply_skin_texture(p, skin_tex, skin_idx)
	var hair_style: int = dz_bridge.get_match_player_hair_style(idx)
	var hair_col: int = dz_bridge.get_match_player_hair_color(idx)
	var beard_style: int = dz_bridge.get_match_player_beard(idx)
	_attach_head_accessory(p, hair_style, hair_col, beard_style)
	_player_names[idx] = dz_bridge.get_match_player_name(idx)
	_player_numbers[idx] = dz_bridge.get_match_player_number(idx)
	_attach_back_number(p, str(_player_numbers[idx]))
	if idx == 0 or idx == 11 or idx == 9 or idx == 20:
		print("[DZ_APPEAR] p%d '%s' #%d h=%.2f skin=%d hair=%d col=%d beard=%d body=%d" % [idx, _player_names[idx], _player_numbers[idx], height, skin_idx, hair_style, hair_col, beard_style, dz_bridge.get_match_player_body_type(idx)])

func _attach_back_number(player: Node3D, txt: String) -> void:
	var lb := player.find_child("NumBack", true, false) as Label3D
	if lb == null:
		var torso := player.find_child("middle", true, false) as Node3D
		if torso == null:
			return
		lb = Label3D.new()
		lb.name = "NumBack"
		lb.font_size = 64
		lb.pixel_size = 0.0045
		lb.outline_size = 8
		lb.outline_modulate = Color(0, 0, 0, 1)
		lb.modulate = Color(1, 1, 1)
		lb.shaded = false
		lb.rotation.y = PI
		lb.position = Vector3(0, 0.30, -0.14)
		torso.add_child(lb)
	lb.text = txt

const SKIN_TONES := [
	Color8(242, 209, 183), Color8(209, 165, 127), Color8(191, 153, 107), Color8(165, 122, 81),
	Color8(114, 76, 45), Color8(71, 45, 25), Color8(45, 28, 15)]
const FACE_BASE_TONE := Color8(209, 152, 119)

func _is_face_surface(mi: MeshInstance3D, s: int) -> bool:
	if mi.mesh == null:
		return false
	var m := mi.mesh.surface_get_material(s)
	return m != null and m.resource_name == "face"

func _make_face_material(mi: MeshInstance3D, s: int, skin_idx: int) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	var src := mi.mesh.surface_get_material(s)
	if src is BaseMaterial3D:
		mat.albedo_texture = (src as BaseMaterial3D).albedo_texture
	var tone: Color = SKIN_TONES[clampi(skin_idx, 0, SKIN_TONES.size() - 1)]
	mat.albedo_color = Color(tone.r / FACE_BASE_TONE.r, tone.g / FACE_BASE_TONE.g, tone.b / FACE_BASE_TONE.b, 1.0)
	mat.roughness = 0.6
	return mat

func _apply_skin_texture(node: Node, tex: Texture2D, skin_idx: int = 1) -> void:
	if tex == null:
		return
	for child in node.get_children():
		if child is MeshInstance3D:
			var mi := child as MeshInstance3D
			var name_l := mi.name.to_lower()
			var is_skin := not ("middle" in name_l or "shoulder" in name_l \
				or "thigh" in name_l or name_l == "body" or "knee" in name_l or "ankle" in name_l)
			if is_skin:
				for s in mi.mesh.get_surface_count():
					if _is_face_surface(mi, s):
						mi.set_surface_override_material(s, _make_face_material(mi, s, skin_idx))
						continue
					var mat := StandardMaterial3D.new()
					mat.albedo_texture = tex
					mat.roughness = 0.6
					mi.set_surface_override_material(s, mat)
		_apply_skin_texture(child, tex, skin_idx)

func _find_head_bone_attachment(player: Node3D) -> Node3D:
	var stack: Array = [player]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		if n is Skeleton3D:
			var sk := n as Skeleton3D
			var head_idx := -1
			for b in sk.get_bone_count():
				var bn := sk.get_bone_name(b).to_lower()
				if "head" in bn or "crane" in bn or "skull" in bn:
					head_idx = b
					break
			if head_idx < 0:
				return null
			var att := BoneAttachment3D.new()
			att.name = "HeadAttach"
			att.bone_idx = head_idx
			sk.add_child(att)
			return att
	# Flat body-part hierarchy (no Skeleton3D): the animated 'neck' node carries
	# the head; hair/beard templates are authored relative to it.
	var neck := player.find_child("neck", true, false)
	if neck is Node3D:
		return neck as Node3D
	return null

const HAIR_FIT_BOX := [
	[-0.100, 0.13, -0.10, 0.100, 0.30, 0.08],
	[-0.104, 0.00, -0.13, 0.104, 0.30, 0.04],
	[-0.014, 0.22, -0.075, 0.014, 0.31, 0.06],
	[-0.108, 0.10, -0.108, 0.108, 0.315, 0.085],
	[-0.100, 0.07, -0.125, 0.100, 0.30, 0.08],
]
const BEARD_FIT_BOX := [
	[-0.055, 0.04, 0.05, 0.055, 0.085, 0.098],
	[-0.058, 0.040, 0.05, 0.058, 0.092, 0.108],
	[-0.072, 0.030, 0.045, 0.072, 0.105, 0.112],
]

func _mesh_aabb(n: Node) -> AABB:
	var r := AABB()
	var first := true
	for m in n.find_children("*", "MeshInstance3D", true, false):
		var mi := m as MeshInstance3D
		if mi.mesh == null:
			continue
		var a := mi.transform * mi.mesh.get_aabb()
		r = a if first else r.merge(a)
		first = false
	if n is MeshInstance3D and (n as MeshInstance3D).mesh:
		var a2 := (n as MeshInstance3D).mesh.get_aabb()
		r = a2 if first else r.merge(a2)
	return r

func _fit_to_box(n: Node3D, box: Array) -> void:
	var a := _mesh_aabb(n)
	if a.size.x <= 0.0 or a.size.y <= 0.0 or a.size.z <= 0.0:
		return
	var tmin := Vector3(box[0], box[1], box[2])
	var tmax := Vector3(box[3], box[4], box[5])
	var sc := (tmax - tmin) / a.size
	n.scale = sc
	n.position = tmin - a.position * sc

func _attach_head_accessory(player: Node3D, hair_style: int, hair_col: int, beard_style: int) -> void:
	if player.find_child("HeadDecorMarker", true, false) != null:
		return
	hair_col = clampi(hair_col, 0, _hair_tints.size() - 1)
	var att := _find_head_bone_attachment(player)
	if att == null:
		push_warning("[DZ_APPEAR] no head attachment found for %s" % player.name)
		return
	var marker := Node3D.new()
	marker.name = "HeadDecorMarker"
	att.add_child(marker)
	if hair_style >= 0 and hair_style <= 4 and hair_style < _hair_templates.size() and _hair_templates[hair_style] != null:
		var h: Node3D = (_hair_templates[hair_style] as Node3D).duplicate()
		var hmat := StandardMaterial3D.new()
		var htex: Texture2D = load("res://textures/hair_%d.png" % hair_col)
		if htex:
			hmat.albedo_texture = htex
		else:
			hmat.albedo_color = _hair_tints[hair_col]
		hmat.roughness = 0.85
		_tint_meshes(h, hmat)
		_fit_to_box(h, HAIR_FIT_BOX[hair_style])
		marker.add_child(h)
	if beard_style >= 1 and beard_style <= 3 and (beard_style - 1) < _beard_templates.size() and _beard_templates[beard_style - 1] != null:
		var b: Node3D = (_beard_templates[beard_style - 1] as Node3D).duplicate()
		var bmat := StandardMaterial3D.new()
		bmat.albedo_color = _hair_tints[hair_col]
		bmat.roughness = 0.9
		_tint_meshes(b, bmat)
		_fit_to_box(b, BEARD_FIT_BOX[beard_style - 1])
		marker.add_child(b)

func _tint_meshes(node: Node, mat: Material) -> void:
	if node is MeshInstance3D and (node as MeshInstance3D).mesh:
		var root_mi := node as MeshInstance3D
		for rs in root_mi.mesh.get_surface_count():
			root_mi.set_surface_override_material(rs, mat)
	for child in node.get_children():
		if child is MeshInstance3D:
			var mi := child as MeshInstance3D
			for s in mi.mesh.get_surface_count():
				mi.set_surface_override_material(s, mat)
		_tint_meshes(child, mat)

# ============================================================================
# TASK C — BROADCAST HUD (scoreboard + radar + event feed + active marker)
# ============================================================================

func _setup_broadcast_hud() -> void:
	_sb_panel = Panel.new()
	var sb_style := StyleBoxFlat.new()
	var sb_bg := Color(0.03, 0.03, 0.05, 0.78)
	if _software_gl:
		sb_bg.a = 1.0
	sb_style.bg_color = sb_bg
	sb_style.corner_radius_top_left = 6
	sb_style.corner_radius_top_right = 6
	sb_style.corner_radius_bottom_left = 6
	sb_style.corner_radius_bottom_right = 6
	_sb_panel.add_theme_stylebox_override("panel", sb_style)
	_sb_panel.set_anchors_preset(Control.PRESET_TOP_LEFT)
	_sb_panel.position = Vector2(16, 14)
	_sb_panel.size = Vector2(300, 46)
	_ui_layer.add_child(_sb_panel)

	_sb_timer = Label.new()
	_sb_timer.position = Vector2(10, 6)
	_sb_timer.size = Vector2(70, 34)
	_sb_timer.add_theme_font_size_override("font_size", 24)
	_sb_timer.add_theme_color_override("font_color", Color(0.95, 0.95, 0.95))
	_sb_timer.text = "00:00"
	_sb_panel.add_child(_sb_timer)

	_sb_color_a = ColorRect.new()
	_sb_color_a.color = _team_a_shirt
	_sb_color_a.position = Vector2(88, 8)
	_sb_color_a.size = Vector2(10, 30)
	_sb_panel.add_child(_sb_color_a)

	_sb_team_a = Label.new()
	_sb_team_a.position = Vector2(102, 6)
	_sb_team_a.size = Vector2(46, 34)
	_sb_team_a.add_theme_font_size_override("font_size", 22)
	_sb_team_a.add_theme_color_override("font_color", Color.WHITE)
	_sb_team_a.text = "AAA"
	_sb_panel.add_child(_sb_team_a)

	_sb_score = Label.new()
	_sb_score.position = Vector2(148, 6)
	_sb_score.size = Vector2(52, 34)
	_sb_score.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_sb_score.add_theme_font_size_override("font_size", 24)
	_sb_score.add_theme_color_override("font_color", Color.WHITE)
	_sb_score.text = "0-0"
	_sb_panel.add_child(_sb_score)

	_sb_team_b = Label.new()
	_sb_team_b.position = Vector2(202, 6)
	_sb_team_b.size = Vector2(46, 34)
	_sb_team_b.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_sb_team_b.add_theme_font_size_override("font_size", 22)
	_sb_team_b.add_theme_color_override("font_color", Color.WHITE)
	_sb_team_b.text = "BBB"
	_sb_panel.add_child(_sb_team_b)

	_sb_color_b = ColorRect.new()
	_sb_color_b.color = _team_b_shirt
	_sb_color_b.position = Vector2(252, 8)
	_sb_color_b.size = Vector2(10, 30)
	_sb_panel.add_child(_sb_color_b)

	if _score_label: _score_label.visible = false
	if _timer_label: _timer_label.visible = false
	if _team_a_label: _team_a_label.visible = false
	if _team_b_label: _team_b_label.visible = false

	_radar = DZRadarScript.new()
	_radar.scene = self
	_radar.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	_radar.custom_minimum_size = Vector2(230, 150)
	_radar.size = Vector2(230, 150)
	_radar.position = Vector2(-115, -170)
	_radar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_ui_layer.add_child(_radar)

	_event_label = Label.new()
	_event_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_event_label.set_anchors_preset(Control.PRESET_CENTER_TOP)
	_event_label.position = Vector2(-300, 70)
	_event_label.size = Vector2(600, 50)
	_event_label.add_theme_font_size_override("font_size", 30)
	_event_label.add_theme_color_override("font_color", Color.WHITE)
	_event_label.add_theme_color_override("font_shadow_color", Color.BLACK)
	_event_label.add_theme_constant_override("shadow_offset_x", 2)
	_event_label.add_theme_constant_override("shadow_offset_y", 2)
	_event_label.modulate.a = 0.0
	_ui_layer.add_child(_event_label)

	_goal_banner = Panel.new()
	var gb_style := StyleBoxFlat.new()
	var gb_bg := Color(0.05, 0.4, 0.15, 0.9)
	if _software_gl:
		gb_bg.a = 1.0
	gb_style.bg_color = gb_bg
	_goal_banner.add_theme_stylebox_override("panel", gb_style)
	_goal_banner.set_anchors_preset(Control.PRESET_CENTER)
	_goal_banner.custom_minimum_size = Vector2(700, 90)
	_goal_banner.size = Vector2(700, 90)
	_goal_banner.position = Vector2(-350, -45)
	_goal_banner.visible = false
	_ui_layer.add_child(_goal_banner)
	_goal_label = Label.new()
	_goal_label.set_anchors_preset(Control.PRESET_FULL_RECT)
	_goal_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_goal_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_goal_label.add_theme_font_size_override("font_size", 40)
	_goal_label.add_theme_color_override("font_color", Color.WHITE)
	_goal_banner.add_child(_goal_label)

func _team_code(name: String) -> String:
	var n := name.strip_edges()
	if n.length() >= 3:
		return n.substr(0, 3).to_upper()
	return n.to_upper()

func _update_broadcast_hud() -> void:
	if _sb_timer:
		var minutes: int = int(_game_timer) / 60
		var seconds: int = int(_game_timer) % 60
		_sb_timer.text = "%02d:%02d" % [minutes, seconds]
		_sb_score.text = "%d-%d" % [_score_a, _score_b]
		_sb_team_a.text = _team_code(_team_a_name)
		_sb_team_b.text = _team_code(_team_b_name)
		_sb_color_a.color = _team_a_shirt
		_sb_color_b.color = _team_b_shirt

func _consume_events() -> void:
	if not dz_bridge or not dz_bridge.has_new_event():
		return
	dz_bridge.consume_event()
	var et: int = dz_bridge.get_event_type()
	var pidx: int = dz_bridge.get_event_player_idx()
	var sa: int = dz_bridge.get_event_score(0)
	var sb: int = dz_bridge.get_event_score(1)
	if sa >= 0: _score_a = sa
	if sb >= 0: _score_b = sb
	var pname := ""
	if pidx >= 0 and pidx < _player_names.size() and _player_names[pidx] != "":
		pname = _player_names[pidx]
	match et:
		0:
			_goal_scorer_idx = pidx
			_show_goal_banner(pname)
		1: _show_event_text("CARTON JAUNE  " + pname)
		2: _show_event_text("CARTON ROUGE  " + pname)
		4: _show_event_text("CORNER  -  PASSE pour tirer")
		5: _show_event_text("TOUCHE  -  PASSE pour lancer")
		6: _show_event_text("COUP FRANC  -  PASSE / TIR")
		7: _show_event_text("PENALTY !  -  TIR")
		8: _show_event_text("COUP D'ENVOI  -  appuie sur PASSE")
		9:
			_match_over = true
			_show_match_over_banner()
		10: _show_event_text("MI-TEMPS")
		11: _show_event_text("DEGAGEMENT  -  PASSE / LONG")
		12: _show_event_text("HORS-JEU")
		13: _show_event_text("FAUTE")

func _show_match_over_banner() -> void:
	if not _event_label:
		return
	_event_label.text = "FIN DU MATCH  %d-%d" % [_score_a, _score_b]
	_event_label.modulate.a = 1.0
	if _event_tween and _event_tween.is_valid():
		_event_tween.kill()

func _show_event_text(txt: String) -> void:
	if not _event_label:
		return
	_event_label.text = txt
	_event_label.modulate.a = 1.0
	if _event_tween and _event_tween.is_valid():
		_event_tween.kill()
	_event_tween = create_tween()
	_event_tween.tween_interval(3.2)
	_event_tween.tween_property(_event_label, "modulate:a", 0.0, 0.8)

func _show_goal_banner(pname: String) -> void:
	if not _goal_banner:
		return
	_goal_label.text = "BUT !  %s   %d-%d" % [pname, _score_a, _score_b]
	_goal_banner.visible = true
	_goal_banner.scale = Vector2(0.6, 0.6)
	_goal_banner.modulate.a = 1.0
	if _goal_tween and _goal_tween.is_valid():
		_goal_tween.kill()
	_goal_tween = create_tween()
	_goal_tween.tween_property(_goal_banner, "scale", Vector2(1.0, 1.0), 0.35).set_trans(Tween.TRANS_BACK)
	_goal_tween.tween_interval(3.4)
	_goal_tween.tween_property(_goal_banner, "modulate:a", 0.0, 0.6)
	_goal_tween.tween_callback(func(): _goal_banner.visible = false)
	if _goal_zoom_tween and _goal_zoom_tween.is_valid():
		_goal_zoom_tween.kill()
	_goal_cam_until_ms = Time.get_ticks_msec() + 4500

func _setup_active_marker() -> void:
	var ring_mesh := TorusMesh.new()
	ring_mesh.inner_radius = 0.62
	ring_mesh.outer_radius = 0.78
	ring_mesh.rings = 24
	ring_mesh.ring_segments = 4
	var rmat := StandardMaterial3D.new()
	rmat.albedo_color = Color(1, 1, 1, 1.0)
	rmat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	ring_mesh.material = rmat
	_active_ring = MeshInstance3D.new()
	_active_ring.mesh = ring_mesh
	_active_ring.visible = false
	add_child(_active_ring)

	_active_label = Label3D.new()
	_active_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_active_label.font_size = 96
	_active_label.pixel_size = 0.0055
	_active_label.modulate = Color(0.4, 1.0, 0.4)
	_active_label.outline_size = 10
	_active_label.outline_modulate = Color(0, 0, 0, 0.8)
	_active_label.no_depth_test = true
	_active_label.visible = false
	add_child(_active_label)

var _player_blobs: Array = []

func _update_player_blobs() -> void:
	if directional_light == null or directional_light.shadow_enabled or players.size() < DZ_MAX_PLAYERS:
		return
	if _player_blobs.is_empty():
		var disc := PlaneMesh.new()
		disc.size = Vector2(1.9, 1.35)
		disc.material = _make_blob_material(0.78)
		for i in range(DZ_MAX_PLAYERS):
			var mi := MeshInstance3D.new()
			mi.mesh = disc
			mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			add_child(mi)
			_player_blobs.append(mi)
	for i in range(DZ_MAX_PLAYERS):
		var pp: Vector3 = players[i].position
		# Sun sits at (+x,+z): the fake shadow falls to (-x,-z) of the feet.
		(_player_blobs[i] as MeshInstance3D).position = Vector3(pp.x - 0.22, _pitch_top_y + 0.015, pp.z - 0.18)

func _update_active_marker() -> void:
	if _active_player_idx < 0 or _active_player_idx >= players.size():
		return
	var p: Node3D = players[_active_player_idx]
	var pos: Vector3 = p.position
	if _active_ring:
		_active_ring.visible = true
		_active_ring.position = Vector3(pos.x, _pitch_top_y + 0.02, pos.z)
	if _active_label:
		var nm := ""
		if _active_player_idx < _player_names.size():
			nm = _player_names[_active_player_idx]
		if _active_label.text != nm:
			_active_label.text = nm
		_active_label.visible = nm != ""
		var hs := 1.0
		if _active_player_idx < _player_heights.size() and _player_heights[_active_player_idx] > 0.0:
			hs = _player_heights[_active_player_idx]
		_active_label.position = pos + Vector3(0, 2.3 * hs, 0)

# ============================================================================
# TASK F — BALL EXTRAS (roll / shadow / trail)
# ============================================================================

func _make_blob_material(strength: float) -> StandardMaterial3D:
	if _blob_texture == null:
		var n := 64
		var img := Image.create(n, n, false, Image.FORMAT_RGBA8)
		for y in n:
			for x in n:
				var d: float = Vector2(x + 0.5 - n * 0.5, y + 0.5 - n * 0.5).length() / (n * 0.5)
				var a: float = pow(clampf(1.0 - d, 0.0, 1.0), 1.6)
				img.set_pixel(x, y, Color(0, 0, 0, a))
		_blob_texture = ImageTexture.create_from_image(img)
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_texture = _blob_texture
	m.albedo_color = Color(0, 0, 0, strength)
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	m.no_depth_test = false
	return m

func _setup_ball_extras() -> void:
	var sm := PlaneMesh.new()
	sm.size = Vector2(0.7, 0.7)
	sm.material = _make_blob_material(0.7)
	_ball_shadow = MeshInstance3D.new()
	_ball_shadow.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_ball_shadow.mesh = sm
	add_child(_ball_shadow)
	# Shot trail: pooled static quads — ImmediateMesh clear/rebuild every frame races
	# the GL thread on GLES3 and SIGSEGVs after a few minutes of play. Transforms only.
	for i in range(7):
		var mi := MeshInstance3D.new()
		var q := PlaneMesh.new()
		q.size = Vector2(1, 1)
		q.orientation = PlaneMesh.FACE_Y
		mi.mesh = q
		var m := StandardMaterial3D.new()
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		if _software_gl:
			m.albedo_color = Color(0.95, 0.95, 0.98, 1.0)
			m.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED
		else:
			m.albedo_color = Color(0.95, 0.95, 0.98, 0.35)
			m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		mi.material_override = m
		mi.visible = false
		add_child(mi)
		_trail_quads.append(mi)

func _update_ball_extras(delta: float) -> void:
	if ball == null:
		return
	var vel: Vector3 = dz_bridge.get_ball_velocity() if dz_bridge else Vector3.ZERO
	var vel_godot := _gf_to_godot(vel)
	var speed := vel_godot.length()
	if speed > 0.1:
		var axis := Vector3.UP.cross(vel_godot.normalized())
		if axis.length() > 0.01:
			ball.rotate_object_local(axis.normalized(), speed * delta / 0.22)
	if _ball_shadow:
		var h: float = ball.position.y
		_ball_shadow.position = Vector3(ball.position.x, _pitch_top_y + 0.01, ball.position.z)
		var k: float = clampf(1.0 - h * 0.12, 0.25, 1.0)
		_ball_shadow.scale = Vector3(k, 1.0, k)
	if speed > 16.0:
		_trail_points.push_front(ball.position)
		if _trail_points.size() > 8:
			_trail_points.pop_back()
	else:
		if not _trail_points.is_empty():
			_trail_points.pop_back()
	_draw_ball_trail()

func _draw_ball_trail() -> void:
	# Transforms/visibility only — never touches mesh data (GL-thread safe).
	var n := _trail_points.size()
	var seg_count: int = mini(n - 1, _trail_quads.size())
	for i in range(_trail_quads.size()):
		var mi: MeshInstance3D = _trail_quads[i]
		if i >= seg_count:
			mi.visible = false
			continue
		var a: Vector3 = _trail_points[i]
		var b: Vector3 = _trail_points[i + 1]
		var dir := b - a
		var len := dir.length()
		if len < 0.001:
			mi.visible = false
			continue
		mi.visible = true
		mi.global_position = (a + b) * 0.5
		mi.global_position.y = maxf(a.y, b.y) + 0.02
		var w: float = 0.10 * (1.0 - float(i) / float(_trail_quads.size()))
		mi.scale = Vector3(len, 1.0, maxf(w, 0.02))
		mi.rotation.y = atan2(-dir.z, dir.x)

# ============================================================================
# TASK G — GOALS extras (nets + corner flags)
# ============================================================================

func _setup_goal_nets() -> void:
	var img := Image.create(32, 32, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 0, 0))
	for x in range(32):
		for y in range(32):
			if x % 4 == 0 or y % 4 == 0:
				img.set_pixel(x, y, Color(0.95, 0.95, 0.95, 1.0))
	var net_tex := ImageTexture.create_from_image(img)
	var net_mat := StandardMaterial3D.new()
	net_mat.albedo_texture = net_tex
	net_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	net_mat.alpha_scissor_threshold = 0.5
	net_mat.uv1_scale = Vector3(6, 6, 6)
	net_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	net_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	var gw := 7.4
	var gh := 2.5
	var gd := 2.55
	for side in [-1.0, 1.0]:
		var gx: float = 55.0 * side
		var back := MeshInstance3D.new()
		var bp := QuadMesh.new()
		bp.size = Vector2(gw, gh)
		bp.material = net_mat
		back.mesh = bp
		back.position = Vector3(gx + gd * side, _pitch_top_y + gh / 2.0, 0)
		back.rotation.y = PI / 2.0
		stadium.add_child(back)
		for zs in [-1.0, 1.0]:
			var sidem := MeshInstance3D.new()
			var sp := QuadMesh.new()
			sp.size = Vector2(gd, gh)
			sp.material = net_mat
			sidem.mesh = sp
			sidem.position = Vector3(gx + gd * side / 2.0, _pitch_top_y + gh / 2.0, zs * gw / 2.0)
			stadium.add_child(sidem)
		var roof := MeshInstance3D.new()
		var rp := QuadMesh.new()
		rp.size = Vector2(gd, gw)
		rp.material = net_mat
		roof.mesh = rp
		roof.position = Vector3(gx + gd * side / 2.0, _pitch_top_y + gh, 0)
		roof.rotation.x = -PI / 2.0
		stadium.add_child(roof)

func _setup_corner_flags() -> void:
	var pole_mat := StandardMaterial3D.new()
	pole_mat.albedo_color = Color(0.95, 0.85, 0.1)
	pole_mat.roughness = 0.6
	var flag_colors := [Color(0.95, 0.1, 0.1), Color(0.95, 0.8, 0.05)]
	var corners := [Vector2(-55.0, -36), Vector2(-55.0, 36), Vector2(55.0, -36), Vector2(55.0, 36)]
	var ci := 0
	for c in corners:
		var pole := MeshInstance3D.new()
		var pm := CylinderMesh.new()
		pm.top_radius = 0.02
		pm.bottom_radius = 0.02
		pm.height = 1.5
		pole.mesh = pm
		pole.material_override = pole_mat
		pole.position = Vector3(c.x, _pitch_top_y + 0.75, c.y)
		stadium.add_child(pole)
		var flag := MeshInstance3D.new()
		var fp := PlaneMesh.new()
		fp.size = Vector2(0.4, 0.28)
		var fm := StandardMaterial3D.new()
		fm.albedo_color = flag_colors[ci % 2]
		fm.cull_mode = BaseMaterial3D.CULL_DISABLED
		fp.material = fm
		flag.mesh = fp
		flag.position = Vector3(c.x + 0.2, _pitch_top_y + 1.36, c.y)
		stadium.add_child(flag)
		ci += 1

# ============================================================================
# TASK H — STADIUM ambience (crowd quads + ad boards)
# ============================================================================

func _setup_ad_boards() -> void:
	var mats := [
		_make_board_mat(Color(0.05, 0.2, 0.6)),
		_make_board_mat(Color(0.9, 0.9, 0.92)),
		_make_board_mat(Color(0.05, 0.45, 0.2)),
	]
	var d := 3.5
	var mi := 0
	for zs in [-1.0, 1.0]:
		var x := -55.0
		while x < 55.0:
			var seg := MeshInstance3D.new()
			var bm := BoxMesh.new()
			bm.size = Vector3(7.5, 0.9, 0.1)
			seg.mesh = bm
			seg.material_override = mats[mi % 3]
			seg.position = Vector3(x + 3.75, _pitch_top_y + 0.45, zs * (36.0 + d))
			stadium.add_child(seg)
			_add_board_label(seg, mi, 0.0 if zs < 0.0 else PI, Vector3(0, 0, 0.08 if zs < 0.0 else -0.08))
			x += 7.5
			mi += 1
	for xs in [-1.0, 1.0]:
		var z := -36.0
		while z < 36.0:
			var seg := MeshInstance3D.new()
			var bm := BoxMesh.new()
			bm.size = Vector3(0.1, 0.9, 7.5)
			seg.mesh = bm
			seg.material_override = mats[mi % 3]
			seg.position = Vector3(xs * (55.0 + d), _pitch_top_y + 0.45, z + 3.75)
			stadium.add_child(seg)
			_add_board_label(seg, mi, PI / 2.0 if xs < 0.0 else -PI / 2.0, Vector3(0.08 if xs < 0.0 else -0.08, 0, 0))
			z += 7.5
			mi += 1

const BOARD_TEXTS := ["DZFOOT", "ALGERIE", "DZ SPORT", "LIGUE 1", "MOBILIS", "DJEZZY"]

func _add_board_label(seg: Node3D, idx: int, yaw: float, offset: Vector3) -> void:
	var lb := Label3D.new()
	lb.text = BOARD_TEXTS[idx % BOARD_TEXTS.size()]
	lb.font_size = 64
	lb.pixel_size = 0.011
	lb.modulate = Color(1, 1, 1)
	lb.outline_size = 6
	lb.outline_modulate = Color(0, 0, 0, 1)
	lb.shaded = false
	lb.rotation.y = yaw
	lb.position = offset
	seg.add_child(lb)

func _make_board_mat(bg: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = bg
	m.roughness = 0.5
	m.emission_enabled = true
	m.emission = bg.lightened(0.2)
	m.emission_energy_multiplier = 0.15
	return m

func _setup_crowd() -> void:
	# Crowd texture: dense rows of small "people" (head + shoulders) on a dark
	# stand background. Applied to tilted planes around the pitch, like the
	# original GF stadium's textured stands.
	var img := Image.create(256, 256, false, Image.FORMAT_RGB8)
	var rng := RandomNumberGenerator.new()
	rng.seed = 1234
	var palette := [
		Color(0.85, 0.7, 0.55), Color(0.6, 0.45, 0.35), Color(0.35, 0.25, 0.2),
		Color(0.9, 0.9, 0.9), Color(0.2, 0.2, 0.25), Color(0.7, 0.15, 0.15),
		Color(0.15, 0.3, 0.6), Color(0.1, 0.5, 0.2), Color(0.95, 0.8, 0.1)]
	img.fill(Color(0.06, 0.06, 0.08))
	for py in range(0, 256, 4):
		for px in range(0, 256, 4):
			var c: Color = palette[rng.randi() % palette.size()]
			var body := c.darkened(0.25)
			for dy in range(2):
				for dx in range(1, 3):
					img.set_pixel(px + dx, py + dy, c)
			for dy in range(2, 4):
				for dx in range(4):
					img.set_pixel(px + dx, py + dy, body)
	var tex := ImageTexture.create_from_image(img)
	var cmat := StandardMaterial3D.new()
	cmat.albedo_texture = tex
	cmat.roughness = 0.95
	cmat.albedo_color = Color(1, 1, 1)
	cmat.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	cmat.cull_mode = BaseMaterial3D.CULL_DISABLED
	# World-space triplanar mapping: the stadium GLB's stepped stands are plain
	# green boxes without usable UVs, so project the crowd texture on them.
	cmat.uv1_triplanar = true
	cmat.uv1_world_triplanar = true
	cmat.uv1_scale = Vector3(0.04, 0.04, 0.04)
	var model := stadium.get_node_or_null("StadiumModel")
	if model == null:
		return
	var stack: Array = [model]
	var n_applied := 0
	while not stack.is_empty():
		var nd: Node = stack.pop_back()
		stack.append_array(nd.get_children())
		if nd is MeshInstance3D and (nd as MeshInstance3D).mesh:
			var mi := nd as MeshInstance3D
			var sz: Vector3 = mi.mesh.get_aabb().size
			# Skip the big ground slabs; every other piece is stand geometry.
			if maxf(sz.x, sz.z) < 40.0 and mi.global_position.y > -1.0:
				mi.material_override = cmat
				n_applied += 1
	print("[DZFoot] Crowd texture applied on %d stand pieces" % n_applied)
