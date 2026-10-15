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
var _prev_positions: Array = []  # Array[Vector3] visual start pos for players
var _curr_positions: Array = []  # Array[Vector3] latest server pos for players
var _net_prev_positions: Array = []  # Array[Vector3] previous server pos
var _player_vels: Array = []     # Array[Vector3] server velocity (m/s) for dead-reckoning
var _prev_rot_y: Array = []      # Array[float] visual start rot.y for players
var _curr_rot_y: Array = []      # Array[float] target server rot.y for players
var _prev_clip_id: Array = []    # Array[int] previous clip_id per player
var _prev_ball_pos: Vector3 = Vector3.ZERO
var _curr_ball_pos: Vector3 = Vector3.ZERO
var _net_prev_ball_pos: Vector3 = Vector3.ZERO
var _ball_vel: Vector3 = Vector3.ZERO
var _prev_official_pos: Array = [Vector3.ZERO, Vector3.ZERO, Vector3.ZERO]
var _curr_official_pos: Array = [Vector3.ZERO, Vector3.ZERO, Vector3.ZERO]
var _prev_official_rot: Array = [0.0, 0.0, 0.0]
var _curr_official_rot: Array = [0.0, 0.0, 0.0]
var _last_interp_tick: int = 0
var _last_interp_ts_us: int = 0
var _state_interp_time: float = 0.0
var _state_interp_period: float = 0.05  # Adaptive: 48ms (Fibre/5G) .. 105ms (high-jitter 3G/4G)
var _last_packet_recv_ms: int = 0
var _jitter_ema_sec: float = 0.008
var _adaptive_extrap_tau: float = 0.042
var _player_func_types: Array = []      # Array[int] per-player current functionType
var _player_anim_progs: Array = []      # Array[float] per-player current anim progress (0..1)
var _state_received: bool = false
var _applied_setup_sig: String = ""
const ANIM_SPEED_SCALE: float = 0.80     # matches server 80Hz/100Hz ratio so client anim & server progress stay 1:1 in sync
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
const CAM_MIN_HEIGHT: float = 7.2
const CAM_SMOOTH_RATE: float = 8.5
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
var _auto_dir_z: float = 0.0
var _auto_dir_ms: int = 0
var _ball_still_since_ms: int = 0
var _ball_still_ref: Vector3 = Vector3.ZERO
var _kickoff_followup_until_ms: int = 0
var _goal_net_mat: ShaderMaterial = null
var _goal_net_impact_pos: Vector3 = Vector3(56.0, 1.2, 0.0)
var _goal_net_impact_strength: float = 0.0
var _goal_net_impact_time: float = 0.0
# Set on EVENT_END_MATCH — blocks input/auto-take and keeps the end banner up.
var _match_over: bool = false

# === Player appearance (MatchSetup PlayerStaticInfo) ===
var _body_templates: Array = []     # [thin, average, muscular, heavy] Node3D or null
var _hair_templates: Array = []     # hair_short/long/mohawk/curly/ponytail (bald = none)
var _beard_templates: Array = []    # beard_stubble/short/full (none = 0)
var _skin_textures: Array = []      # skin_0..6
var _hair_tints := [                # realistic football hair palette (no white/grey/neon yellow)
	Color(0.10, 0.08, 0.07), Color(0.18, 0.12, 0.08), Color(0.28, 0.18, 0.11),
	Color(0.38, 0.25, 0.15), Color(0.52, 0.36, 0.20), Color(0.42, 0.20, 0.11),
	Color(0.14, 0.10, 0.08), Color(0.24, 0.16, 0.10)]
var _appearance_done := false
var _player_names: Array = []       # String per player idx (from setup)
var _player_numbers: Array = []     # int per player idx
var _player_heights: Array = []     # float scale per player idx

# === Active-player marker, Direction Arrow & Power Gauge (shot / pass) ===
var _active_ring: MeshInstance3D = null
var _active_ring_mat: StandardMaterial3D = null
var _opp_active_ring: MeshInstance3D = null
var _opp_active_ring_mat: StandardMaterial3D = null
var _opp_active_player_idx: int = -1
var _dir_arrow: MeshInstance3D = null
var _dir_arrow_mat: StandardMaterial3D = null
var _active_label: Label3D = null
var _power_gauge_bg: Panel = null
var _power_gauge_fill: ColorRect = null
var _power_gauge_start_ms: int = 0
var _power_gauge_hold_until_ms: int = 0
var _power_gauge_value: float = 0.0
var _power_gauge_charging: bool = false
var _is_attacking_mode: bool = true
var _last_mode_switch_ms: int = 0
var _joy_dir_dot: Control = null
var _world_aim_dir: Vector2 = Vector2(1.0, 0.0)

# === Ball extras ===
var _ball_shadow: MeshInstance3D = null
var _trail_quads: Array = []          # pooled trail ribbon quads (MeshInstance3D)
var _trail_points: Array = []       # recent ball Vector3 positions (world)
var _ball_spin := Vector3.ZERO     # accumulated euler rotation (visual roll)
var _prev_ball_render_pos := Vector3.ZERO
var _ball_shadow_mat: StandardMaterial3D = null
var _ad_board_labels: Array = []
var _ad_board_timer: float = 0.0
var _ad_board_cycle: int = 0
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
var _ref_card_mesh: MeshInstance3D = null
var _ref_card_mat: StandardMaterial3D = null
var _ref_right_elbow: Node3D = null
var _ref_left_elbow: Node3D = null
var _showcard_clip_id: int = 1332
var _card_anim_until_ms: int = 0
var _card_is_red: bool = false
var _card_player_idx: int = -1

# === Input state ===
var _move_dir := Vector2.ZERO
var _buttons: int = 0
var _btn_latch: Dictionary = {}  # mask -> release deadline (ms)

# Contextual 4-button arc layout inspired by EA FC Mobile & eFootball Mobile:
# Only 4 ergonomic thumb-arc buttons are shown at once, dynamically switching
# between ATTACK mode (when your team has or contests the ball) and DEFENSE mode.
const BUTTON_DEFS = {
	"btn_main": {
		"radius": 90.0,
		"pos": Vector2(-204, -204),
		"atk_mask": BUTTON_SPRINT,
		"atk_label": "SPRINT",
		"atk_sub": "DRIBBLE",
		"atk_color": Color(0.12, 0.64, 0.98),
		"def_mask": BUTTON_SPRINT | BUTTON_PASS,
		"def_label": "PRESS",
		"def_sub": "CONTENIR",
		"def_color": Color(0.08, 0.80, 0.88),
	},
	"btn_low": {
		"radius": 74.0,
		"pos": Vector2(-406, -168),
		"atk_mask": BUTTON_PASS,
		"atk_label": "PASSE",
		"atk_sub": "COURTE",
		"atk_color": Color(0.16, 0.84, 0.44),
		"def_mask": BUTTON_SWITCH_PLAYER,
		"def_label": "CHANGER",
		"def_sub": "JOUEUR",
		"def_color": Color(0.58, 0.44, 0.98),
	},
	"btn_mid": {
		"radius": 74.0,
		"pos": Vector2(-386, -334),
		"atk_mask": BUTTON_KICK,
		"atk_label": "PROFOND",
		"atk_sub": "AU SOL",
		"atk_color": Color(0.98, 0.74, 0.14),
		"def_mask": BUTTON_SLIDING,
		"def_label": "TACLE",
		"def_sub": "GLISSÉ",
		"def_color": Color(0.98, 0.50, 0.14),
	},
	"btn_lob": {
		"radius": 74.0,
		"pos": Vector2(-282, -468),
		"atk_mask": BUTTON_HIGH_PASS,
		"atk_label": "LOB",
		"atk_sub": "CENTRE",
		"atk_color": Color(0.22, 0.88, 0.94),
		"def_mask": BUTTON_KICK,
		"def_label": "GARDIEN",
		"def_sub": "SORTIE GK",
		"def_color": Color(0.92, 0.68, 0.18),
	},
	"btn_top": {
		"radius": 78.0,
		"pos": Vector2(-124, -412),
		"atk_mask": BUTTON_SHOT,
		"atk_label": "TIR",
		"atk_sub": "FRAPPE",
		"atk_color": Color(0.96, 0.22, 0.28),
		"def_mask": BUTTON_SHOT,
		"def_label": "2e DÉF",
		"def_sub": "APPEL IA",
		"def_color": Color(0.94, 0.32, 0.46),
	},
}
const BTN_RADIUS := 80.0
const BTN_HIT_RADIUS := 120.0
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
		_net_prev_positions.append(Vector3.ZERO)
		_player_vels.append(Vector3.ZERO)
		_prev_rot_y.append(0.0)
		_curr_rot_y.append(0.0)
		_prev_clip_id.append(-1)
		_player_func_types.append(0)
		_player_anim_progs.append(0.0)
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
	if not DEBUG_NO_UI:
		_setup_touch_ui()
		_setup_ui_overlay()
		_setup_broadcast_hud()
	_setup_active_marker()
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
		var prev_sp := _set_piece_type
		_set_piece_type = sp
		_set_piece_since_ms = Time.get_ticks_msec()
		_auto_take_next_ms = 0
		_auto_dir_ms = 0
		# Right after the 1.4m kickoff pass (sp 1 -> 0, or initial packet sp == 0 at match start),
		# the receiving player at the center spot becomes human-controlled; give an immediate
		# forward drive + timed pass impulse when the ball reaches their feet (~320ms) so they never freeze.
		if (prev_sp == 1 or prev_sp == -1) and sp == 0:
			var now_k := Time.get_ticks_msec()
			_kickoff_followup_until_ms = now_k + 1800
			_auto_take_next_ms = now_k + 300
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
	var adapter_name: String = RenderingServer.get_video_adapter_name()
	var software_gl: bool = "SwiftShader" in adapter_name or "llvmpipe" in adapter_name or "Software" in adapter_name
	var emulator_gl: bool = "Emulator" in adapter_name or "Translator" in adapter_name
	_software_gl = software_gl
	if software_gl or emulator_gl:
		var vp := get_viewport()
		for p in vp.get_property_list():
			if p.name == "software_skinning":
				vp.software_skinning = true
				print("[DZFoot] software_skinning enabled (emulator/software GL)")
				break
	# Dynamic shadow maps on high-end GPUs; fast blob + contact AO shadows on mid-range mobile GPUs (e.g. Mali-G57) for locked 60 FPS
	var high_end_gpu: bool = _gpu_supports_shadows(adapter_name) and not emulator_gl
	directional_light.shadow_enabled = high_end_gpu
	directional_light.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS if high_end_gpu else DirectionalLight3D.SHADOW_ORTHOGONAL
	directional_light.directional_shadow_max_distance = 115.0 if high_end_gpu else 90.0
	directional_light.directional_shadow_split_1 = 0.36
	directional_light.directional_shadow_blend_splits = high_end_gpu
	directional_light.shadow_bias = 0.02
	directional_light.shadow_normal_bias = 1.0
	directional_light.shadow_blur = 1.35 if high_end_gpu else 1.0
	directional_light.directional_shadow_fade_start = 0.85
	_high_quality = high_end_gpu
	var atlas_sz: int = 4096 if high_end_gpu else 1024
	RenderingServer.directional_shadow_atlas_set_size(atlas_sz, true)
	directional_light.shadow_opacity = 0.85
	directional_light.light_color = Color(1.0, 0.89, 0.67)
	directional_light.light_energy = 0.91
	directional_light.position = Vector3(48, 31, -22)
	add_child(directional_light)
	directional_light.look_at(Vector3.ZERO)

	# Stadium floodlight rim/fill light on players from opposite stand
	var stadium_spot_light := DirectionalLight3D.new()
	stadium_spot_light.name = "StadiumFloodFill"
	stadium_spot_light.shadow_enabled = false
	stadium_spot_light.light_color = Color(0.80, 0.88, 0.99)
	stadium_spot_light.light_energy = 0.23
	stadium_spot_light.position = Vector3(-48, 38, 28)
	add_child(stadium_spot_light)
	stadium_spot_light.look_at(Vector3.ZERO)
	print("[DZFoot] FULL GRAPHICS: Shadows=%s max_dist=115m atlas=%d on %s" % [directional_light.shadow_enabled, atlas_sz, adapter_name])

	world_env = WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.44, 0.49, 0.54)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.84, 0.88, 0.95)
	env.ambient_light_energy = 0.51
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.tonemap_exposure = 1.0
	env.fog_enabled = false
	env.adjustment_enabled = false
	env.glow_enabled = false
	get_viewport().msaa_3d = Viewport.MSAA_2X if high_end_gpu else Viewport.MSAA_DISABLED
	world_env.environment = env
	add_child(world_env)
	if high_end_gpu:
		_setup_gf_postprocess_layer()

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
		_setup_stadium_floodlights()
	_strip_stadium_shadows(stadium)

func _strip_stadium_shadows(root: Node) -> void:
	# Keep real shadows on goal posts, nets and corner flags; only flat ground
	# slabs (pitch_00..03, stadium slabs) don't need to cast shadows onto themselves.
	var stack: Array = [root]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		if n is MeshInstance3D and (n as MeshInstance3D).mesh:
			var sz: Vector3 = (n as MeshInstance3D).mesh.get_aabb().size
			if sz.y < 0.3 and maxf(sz.x, sz.z) > 20.0:
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
	var grass: Texture2D = load("res://textures/grass.png")
	var pitch_sh_mat: Material = _make_pitch_shader_material(grass) if grass else apron
	var count := 0
	var stack: Array = [stad_root]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		if n is MeshInstance3D:
			var mi := n as MeshInstance3D
			if mi.mesh == null:
				continue
			if mi.name.begins_with("pitch_"):
				mi.visible = false
				continue
			var sz := mi.mesh.get_aabb().size
			# Flat horizontal slab: big XZ, negligible Y.
			if sz.y < 0.3 and sz.x > 60.0 and sz.z > 60.0:
				var inner := sz.x <= 150.0
				for s in mi.mesh.get_surface_count():
					mi.set_surface_override_material(s, pitch_sh_mat if inner else asphalt)
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
uniform sampler2D grass_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D overlay_tex : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D noise_tex : filter_linear, repeat_enable;
varying vec3 wpos;
void vertex() {
	wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}
float mow(float coord) {
	float b = sin(fract(coord) * 6.2831853) * 0.5 + 0.5;
	for (int i = 0; i < 6; i++) {
		b = smoothstep(0.0, 1.0, b);
	}
	return b * 2.0 - 1.0;
}
void fragment() {
	vec2 p = wpos.xz;
	vec3 base = pow(vec3(0.275, 0.358, 0.162), vec3(2.2));
	vec3 tex_fine = texture(grass_tex, p * 0.28).rgb;
	vec3 tex_macro = texture(grass_tex, p * 0.065 + vec2(0.37, 0.71)).rgb;
	vec3 col = mix(base, mix(tex_fine, tex_macro, 0.38), 0.44);

	// Multi-scale organic turf & dry-grass patchiness
	float n_macro = texture(noise_tex, p * 0.020).r - 0.5;
	float n_mid   = texture(noise_tex, p * 0.068 + vec2(0.41, 0.19)).r - 0.5;
	float n_fine  = texture(noise_tex, p * 0.220 + vec2(0.13, 0.63)).r;
	col *= 1.0 + n_macro * 0.22 + n_mid * 0.15;
	float dry_patch = smoothstep(0.05, 0.38, n_macro + n_mid * 0.65);
	col = mix(col, col * vec3(1.12, 0.96, 0.70), dry_patch * 0.42);

	float inside = step(abs(p.x), 55.0) * step(abs(p.y), 36.0);
	float mow_val = inside * (mow(p.x / 11.0) * 0.11 + mow(p.y / 11.0) * 0.08);
	// Break up mower stripes slightly where turf is worn/patchy
	float mow_break = clamp(1.0 - abs(n_macro) * 0.85, 0.45, 1.0);
	col *= 1.0 + 0.48 * mow_val * mow_break;

	float ao = 1.0 - pow(clamp(length(p / vec2(55.0, 36.0)) * 0.7, 0.0, 1.0), 1.5) * 0.18;
	col *= ao;
	col = mix(vec3(dot(col, vec3(0.2125, 0.7154, 0.0721))), col, 0.78);
	col *= 1.42;

	// High-traffic stadium wear zones (goal mouths, penalty spots, center circle, midfield spine)
	float goal_w_l = exp(-length((p - vec2(-52.2, 0.0)) * vec2(0.28, 0.16)));
	float goal_w_r = exp(-length((p - vec2( 52.2, 0.0)) * vec2(0.28, 0.16)));
	float pen_w_l  = exp(-length((p - vec2(-43.8, 0.0)) * vec2(0.32, 0.28))) * 0.65;
	float pen_w_r  = exp(-length((p - vec2( 43.8, 0.0)) * vec2(0.32, 0.28))) * 0.65;
	float center_w = exp(-length(p * vec2(0.11, 0.11))) * 0.58;
	float spine_w  = exp(-pow(p.y * 0.055, 2.0)) * step(abs(p.x), 48.0) * 0.30;
	float divots   = smoothstep(0.62, 0.86, n_fine) * (0.45 + 0.55 * clamp(n_mid + 0.5, 0.0, 1.0));
	float zone_wear = inside * clamp((goal_w_l + goal_w_r + pen_w_l + pen_w_r + center_w + spine_w) * (0.55 + 0.75 * n_fine) + divots * 0.24, 0.0, 0.88);
	vec3 dirt_col = mix(vec3(0.29, 0.23, 0.15), vec3(0.38, 0.31, 0.20), n_fine);
	col = mix(col, dirt_col, zone_wear * 0.62);

	vec2 ouv = vec2(p.x / 120.0 + 0.5, p.y / 80.0 + 0.5);
	if (ouv.x >= 0.0 && ouv.x <= 1.0 && ouv.y >= 0.0 && ouv.y <= 1.0) {
		vec4 ov = textureLod(overlay_tex, ouv, 0.5);
		float is_line = smoothstep(0.58, 0.86, min(ov.r, min(ov.g, ov.b)));
		float wear_a = ov.a * 0.68 * (1.0 - is_line);
		vec3 ov_dirt = mix(col * vec3(0.72, 0.63, 0.46), dirt_col, 0.52);
		col = mix(col, ov_dirt, clamp(wear_a * (0.70 + 0.55 * n_fine), 0.0, 0.82));
		float line_a = clamp(ov.a * 2.15, 0.0, 0.88) * is_line * (0.82 + 0.18 * n_fine);
		col = mix(col, vec3(0.95, 0.93, 0.83), line_a);
	}
	// 16h00 warm afternoon sunlight graze + subtle stadium pools on the turf
	float spot1 = exp(-length(p - vec2(-32.0, -18.0)) * 0.042);
	float spot2 = exp(-length(p - vec2( 32.0, -18.0)) * 0.042);
	float spot3 = exp(-length(p - vec2(-32.0,  18.0)) * 0.042);
	float spot4 = exp(-length(p - vec2( 32.0,  18.0)) * 0.042);
	float spot_center = exp(-length(p) * 0.035) * 0.58;
	float spot_pool = clamp(spot1 + spot2 + spot3 + spot4 + spot_center, 0.0, 1.18);
	col *= 0.98 + spot_pool * 0.09;
	float sun_graze = clamp((p.x * 0.0085 - p.y * 0.010) + 0.50, 0.0, 1.0);
	col = mix(col * vec3(0.95, 0.97, 1.0), col * vec3(1.13, 1.03, 0.78), sun_graze * 0.31);

	// Distance atmospheric afternoon fog on the far side of the pitch
	float cam_dist = length(INV_VIEW_MATRIX[3].xyz - wpos);
	float fog = clamp((cam_dist - 24.0) * 0.0112, 0.0, 0.48);
	vec3 pitch_fog_col = mix(vec3(0.55, 0.60, 0.65), vec3(0.71, 0.67, 0.56), sun_graze * 0.48);
	col = mix(col, pitch_fog_col, fog);
	ALBEDO = col;
	ROUGHNESS = 0.88;
	SPECULAR = 0.18;
}
"""

const GF_POSTPROCESS_SHADER := """
shader_type canvas_item;
uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap;

vec3 csb(vec3 color, float brt, float con, float sat) {
	const vec3 LumCoeff = vec3(0.2125, 0.7154, 0.0721);
	vec3 brtColor = color * brt;
	vec3 intensity = vec3(dot(brtColor, LumCoeff));
	vec3 satColor = mix(intensity, brtColor, sat);
	return mix(vec3(0.5), satColor, con);
}

vec3 alt_contrast(vec3 color, float bias) {
	return color * (1.0 - bias) + (-cos(clamp(color, 0.0, 1.0) * 3.14159265) * 0.5 + 0.5) * bias;
}

vec3 bright_pass(vec3 c) {
	float luma = dot(c, vec3(0.2125, 0.7154, 0.0721));
	float max_ch = max(c.r, max(c.g, c.b));
	return c * smoothstep(0.36, 0.78, max(luma, max_ch * 0.92));
}

void fragment() {
	vec2 uv = SCREEN_UV;
	vec2 px = SCREEN_PIXEL_SIZE;
	vec3 base = texture(screen_tex, uv).rgb;

	// 1. GameplayFootball postprocess.frag silhouette halo, player/ball micro-contrast & rim glow
	vec3 n_up = texture(screen_tex, uv + vec2(0.0, px.y * 1.25)).rgb;
	vec3 n_dn = texture(screen_tex, uv - vec2(0.0, px.y * 1.25)).rgb;
	vec3 n_rt = texture(screen_tex, uv + vec2(px.x * 1.25, 0.0)).rgb;
	vec3 n_lf = texture(screen_tex, uv - vec2(px.x * 1.25, 0.0)).rgb;
	vec3 avg4 = (n_up + n_dn + n_rt + n_lf) * 0.25;
	vec3 detail = base - avg4;
	float edge = clamp(length(detail) * 3.6, 0.0, 1.0);
	vec3 rim_glow = max(avg4 - base * 0.74, vec3(0.0)) * edge * 0.56;
	base = mix(base + detail * 0.32, avg4, edge * 0.42) + rim_glow;

	// 2. Multi-radius Bloom (player kits, ball sheen, LED adboards, stadium lights & pitch lines)
	vec3 bloom = bright_pass(base) * 0.20;
	bloom += bright_pass(texture(screen_tex, uv + vec2( 2.5,  0.0) * px).rgb) * 0.12;
	bloom += bright_pass(texture(screen_tex, uv + vec2(-2.5,  0.0) * px).rgb) * 0.12;
	bloom += bright_pass(texture(screen_tex, uv + vec2( 0.0,  2.5) * px).rgb) * 0.12;
	bloom += bright_pass(texture(screen_tex, uv + vec2( 0.0, -2.5) * px).rgb) * 0.12;
	bloom += bright_pass(texture(screen_tex, uv + vec2( 6.0,  6.0) * px).rgb) * 0.085;
	bloom += bright_pass(texture(screen_tex, uv + vec2(-6.0,  6.0) * px).rgb) * 0.085;
	bloom += bright_pass(texture(screen_tex, uv + vec2( 6.0, -6.0) * px).rgb) * 0.085;
	bloom += bright_pass(texture(screen_tex, uv + vec2(-6.0, -6.0) * px).rgb) * 0.085;
	bloom += bright_pass(texture(screen_tex, uv + vec2(12.0,  0.0) * px).rgb) * 0.05;
	bloom += bright_pass(texture(screen_tex, uv + vec2(-12.0, 0.0) * px).rgb) * 0.05;
	bloom += bright_pass(texture(screen_tex, uv + vec2( 0.0, 12.0) * px).rgb) * 0.05;
	bloom += bright_pass(texture(screen_tex, uv + vec2( 0.0,-12.0) * px).rgb) * 0.05;

	vec3 col = base + bloom * vec3(1.04, 0.99, 0.88) * 0.51;

	// 3. Atmospheric stadium depth fog + 16h00 warm afternoon sun glow in upper screen
	float top_factor = clamp(1.0 - uv.y * 1.42, 0.0, 1.0);
	float depth_fog = pow(top_factor, 1.38) * 0.29;
	float sun_haze = exp(-length((uv - vec2(0.78, 0.05)) * vec2(1.30, 2.45)) * 2.35) * 0.16;
	float spot_beam_l = exp(-abs(uv.x - 0.20) * 9.5) * pow(top_factor, 1.72) * 0.07;
	float spot_beam_c = exp(-abs(uv.x - 0.50) * 9.5) * pow(top_factor, 1.72) * 0.06;
	float spot_beam_r = exp(-abs(uv.x - 0.80) * 9.5) * pow(top_factor, 1.72) * 0.07;
	vec3 fog_color = mix(vec3(0.60, 0.65, 0.71), vec3(0.76, 0.71, 0.59), clamp(uv.x * 0.80, 0.0, 1.0));
	col = mix(col, fog_color, depth_fog);
	col += vec3(1.0, 0.89, 0.56) * sun_haze;
	col += vec3(0.97, 0.91, 0.74) * (spot_beam_l + spot_beam_c + spot_beam_r);

	// 4. GameplayFootball postprocess.frag S-curve & 16h00 afternoon broadcast grading
	col = csb(col, 0.99, 1.015, 0.92);
	col *= vec3(1.02, 1.00, 0.955);
	col = alt_contrast(col, 0.20);

	// 5. Broadcast lens vignette
	vec2 d = (uv - vec2(0.5)) * vec2(1.15, 1.0);
	float vig = 1.0 - smoothstep(0.38, 0.82, length(d)) * 0.17;
	col *= vig;

	COLOR = vec4(clamp(col, 0.0, 1.0), 1.0);
}
"""

func _setup_gf_postprocess_layer() -> void:
	var pp_layer := CanvasLayer.new()
	pp_layer.name = "GFPostProcess"
	pp_layer.layer = 1
	add_child(pp_layer)
	var rect := ColorRect.new()
	rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var sh := Shader.new()
	sh.code = GF_POSTPROCESS_SHADER
	var mat := ShaderMaterial.new()
	mat.shader = sh
	rect.material = mat
	pp_layer.add_child(rect)

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
	var overlay: Texture2D = load("res://textures/overlay.png")
	var sh := Shader.new()
	sh.code = PITCH_SHADER_CODE
	var m := ShaderMaterial.new()
	m.shader = sh
	m.set_shader_parameter("grass_tex", grass)
	if overlay:
		m.set_shader_parameter("overlay_tex", overlay)
	m.set_shader_parameter("noise_tex", _pitch_noise_tex)
	return m

func _make_pro_ball_material() -> StandardMaterial3D:
	# High-contrast FIFA / Champions League style match ball texture so 3D spin
	# animation is clearly visible and post-processing gives crisp 3D shading.
	var img := Image.create(128, 64, false, Image.FORMAT_RGBA8)
	var base_white := Color(0.97, 0.97, 0.99, 1.0)
	var dark_pent := Color(0.08, 0.10, 0.14, 1.0)
	var emerald_trim := Color(0.08, 0.68, 0.36, 1.0)
	var gold_trim := Color(0.92, 0.74, 0.16, 1.0)
	img.fill(base_white)
	var centers := [
		Vector2(16, 16), Vector2(48, 16), Vector2(80, 16), Vector2(112, 16),
		Vector2(32, 48), Vector2(64, 48), Vector2(96, 48), Vector2(0, 48), Vector2(127, 48)
	]
	for y in range(64):
		for x in range(128):
			var p := Vector2(x, y)
			var min_d := 999.0
			for c in centers:
				var d := p.distance_to(c)
				if d < min_d:
					min_d = d
			if min_d < 8.5:
				img.set_pixel(x, y, dark_pent)
			elif min_d < 10.8:
				img.set_pixel(x, y, emerald_trim)
			elif min_d < 12.2:
				img.set_pixel(x, y, gold_trim)
			elif int(x + y) % 16 == 0:
				img.set_pixel(x, y, Color(0.82, 0.84, 0.88, 1.0))
	img.generate_mipmaps()
	var tex := ImageTexture.create_from_image(img)
	var ball_mat := StandardMaterial3D.new()
	ball_mat.albedo_texture = tex
	ball_mat.uv1_triplanar = true
	ball_mat.uv1_scale = Vector3(2.4, 2.4, 2.4)
	ball_mat.roughness = 0.26
	ball_mat.specular = 0.72
	ball_mat.metallic = 0.04
	ball_mat.rim_enabled = true
	ball_mat.rim = 0.65
	ball_mat.rim_tint = 0.20
	ball_mat.emission_enabled = true
	ball_mat.emission = Color(0.85, 0.88, 0.92)
	ball_mat.emission_energy_multiplier = 0.16
	return ball_mat

func _setup_ball() -> void:
	var ball_mat := _make_pro_ball_material()
	# Try loading real ball GLB
	var ball_scene = _load_glb("res://models/ball.glb")
	if ball_scene:
		ball = _find_mesh_in_node(ball_scene)
		if ball:
			ball.name = "Ball"
			ball.scale = Vector3(1.12, 1.12, 1.12)
			ball.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
			for s in ball.mesh.get_surface_count():
				ball.set_surface_override_material(s, ball_mat)
			add_child(ball_scene)
			print("[DZFoot] Ball GLB loaded with pro patterned material & shadow")
			return
		add_child(ball_scene)
	# Fallback: procedural ball
	var ball_mesh := SphereMesh.new()
	ball_mesh.radius = 0.13
	ball_mesh.height = 0.26
	ball_mesh.material = ball_mat
	ball = MeshInstance3D.new()
	ball.mesh = ball_mesh
	ball.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
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
	var is_away := idx >= 11
	var is_gk := (idx == 0 or idx == 11)
	var colored := _apply_kit(player, is_away, is_gk)
	if colored == 0:
		push_warning("[DZFoot] %s: no meshes colored" % player.name)
	_attach_back_number(player, str((idx % 11) + 1))
	_attach_player_contact_shadow(player)
	return player

var _player_ao_mat: ShaderMaterial = null

func _attach_player_contact_shadow(player: Node3D) -> void:
	if _software_gl:
		return
	if _player_ao_mat == null:
		var sh := Shader.new()
		sh.code = """
shader_type spatial;
render_mode unshaded, depth_draw_never, cull_disabled;
void fragment() {
	vec2 d = UV * 2.0 - vec2(1.0);
	float a = pow(clamp(1.0 - dot(d, d), 0.0, 1.0), 1.85) * 0.46;
	ALBEDO = vec3(0.02, 0.05, 0.02);
	ALPHA = a;
}
"""
		_player_ao_mat = ShaderMaterial.new()
		_player_ao_mat.shader = sh
	var ao := MeshInstance3D.new()
	ao.name = "ContactAO"
	var pm := PlaneMesh.new()
	pm.size = Vector2(0.92, 0.78)
	ao.mesh = pm
	ao.material_override = _player_ao_mat
	ao.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	ao.position = Vector3(0.0, 0.018, 0.0)
	player.add_child(ao)

# Server-provided kit colors (MatchSetupPacket RGB) — fall back to defaults.
var _team_a_shirt := Color(0.95, 0.95, 0.97)
var _team_a_shorts := Color(0.08, 0.28, 0.78)
var _team_b_shirt := Color(0.86, 0.08, 0.12)
var _team_b_shorts := Color(0.95, 0.95, 0.97)

func _apply_kit(player: Node, is_away: bool, is_gk: bool) -> int:
	var shirt: Color
	var shorts: Color
	if is_gk:
		shirt = Color(0.22, 0.78, 0.30) if not is_away else Color(0.92, 0.48, 0.12)
		shorts = Color(0.10, 0.10, 0.12)
	elif is_away:
		shirt = _team_b_shirt
		shorts = _team_b_shorts
	else:
		shirt = _team_a_shirt
		shorts = _team_a_shorts
	return _apply_kit_to_meshes(player, shirt, shorts)

var _kit_tex_cache: Dictionary = {}
var _kit_normal_tex: Texture2D = null

func _get_kit_material(shirt: Color, shorts: Color) -> StandardMaterial3D:
	var key := "%d_%d" % [shirt.to_rgba32(), shorts.to_rgba32()]
	if _kit_tex_cache.has(key):
		return _kit_tex_cache[key]
	var img := Image.create(256, 256, true, Image.FORMAT_RGBA8)
	img.fill(shorts)
	var accent := shorts
	if accent.is_equal_approx(shirt) or Vector3(accent.r - shirt.r, accent.g - shirt.g, accent.b - shirt.b).length() < 0.25:
		accent = Color(0.95, 0.95, 0.97) if shirt.get_luminance() < 0.6 else Color(0.10, 0.22, 0.68)
	# 1. Shirt torso area (y 0..103): main shirt color + soft side shading
	img.fill_rect(Rect2i(0, 0, 256, 104), shirt)
	var side_shade := shirt.darkened(0.09)
	img.fill_rect(Rect2i(26, 38, 6, 66), side_shade)
	img.fill_rect(Rect2i(96, 38, 6, 66), side_shade)
	img.fill_rect(Rect2i(154, 38, 6, 66), side_shade)
	img.fill_rect(Rect2i(224, 38, 6, 66), side_shade)
	# 2. Contrasting sleeves + 3 shoulder stripes (exact style of Images 1-3!)
	# Front sleeves sit at x=0..24 and x=104..128; back sleeves at x=128..152 and x=232..256 (y=0..38)
	img.fill_rect(Rect2i(0, 0, 24, 38), accent)
	img.fill_rect(Rect2i(104, 0, 48, 38), accent)
	img.fill_rect(Rect2i(232, 0, 24, 38), accent)
	# 3 parallel shoulder stripes along top of sleeves/shoulders (y=2..4, 7..9, 12..14)
	var stripe_col := Color(0.96, 0.96, 0.98) if accent.get_luminance() < 0.65 else shirt
	for sy in [2, 7, 12]:
		img.fill_rect(Rect2i(0, sy, 44, 2), stripe_col)
		img.fill_rect(Rect2i(84, sy, 88, 2), stripe_col)
		img.fill_rect(Rect2i(212, sy, 44, 2), stripe_col)
	# 3. Bold V-neck collar (front x=50..78 y=0..15, back x=178..206 y=0..7)
	for dy in range(15):
		var hw := 14 - int(dy * 0.75)
		img.fill_rect(Rect2i(64 - hw, dy, hw * 2, 1), accent)
	img.fill_rect(Rect2i(179, 0, 26, 6), accent)
	# 4. Shorts area — measured UV layout of player_base.glb:
	#    body (kit_upper) maps to x=146..248, y=138..246 and the thigh kit_lower
	#    patches overlap it at x=225..238, y=173..252. Anything else below y=131
	#    was painting ONTO the shorts (caused the horizontal striped-shorts bug).
	img.fill_rect(Rect2i(140, 131, 116, 125), shorts)
	# Dark waistband + hem
	img.fill_rect(Rect2i(146, 138, 102, 6), shorts.darkened(0.18))
	img.fill_rect(Rect2i(146, 242, 102, 4), shorts.darkened(0.12))
	# 3 vertical side stripes near each side seam (Images 1-3 style)
	var short_stripe := Color(0.96, 0.96, 0.98) if shorts.get_luminance() < 0.65 else accent
	for sx in [150, 154, 158, 236, 240, 244]:
		img.fill_rect(Rect2i(sx, 148, 2, 92), short_stripe)
	# 5. Socks area — knee kit_lower strip lives at x=1..31, y=131..158
	#    (shoulder sleeve cuffs share x=11..26, y=137..150, so hoops show there too)
	var sock_base := Color(0.95, 0.95, 0.97) if shorts.get_luminance() < 0.75 else shorts
	var sock_hoop := accent if accent.get_luminance() < 0.75 else shirt
	img.fill_rect(Rect2i(0, 131, 33, 28), sock_base)
	img.fill_rect(Rect2i(0, 134, 33, 3), sock_hoop)
	img.fill_rect(Rect2i(0, 141, 33, 3), sock_hoop)
	img.generate_mipmaps()
	var tex := ImageTexture.create_from_image(img)
	var mat := StandardMaterial3D.new()
	mat.albedo_texture = tex
	mat.roughness = 0.50
	mat.specular = 0.40
	mat.metallic = 0.04
	mat.rim_enabled = true
	mat.rim = 0.62
	mat.rim_tint = 0.18
	mat.emission_enabled = true
	mat.emission = shirt.lerp(Color(1.0, 0.96, 0.90), 0.60)
	mat.emission_energy_multiplier = 0.16
	_kit_tex_cache[key] = mat
	return mat

func _surface_tex_kind(mi: MeshInstance3D, s: int) -> int:
	# 0 = skin (image_0), 1 = kit (image_1), 2 = shoe (image_2), 3 = face
	if _is_face_surface(mi, s):
		return 3
	if mi.mesh != null:
		var src := mi.mesh.surface_get_material(s)
		if src is BaseMaterial3D and (src as BaseMaterial3D).albedo_texture != null:
			var p: String = (src as BaseMaterial3D).albedo_texture.resource_path
			if "image_0" in p:
				return 0
			if "image_1" in p:
				return 1
			if "image_2" in p:
				return 2
	var nl := mi.name.to_lower()
	if "ankle" in nl:
		return 2
	if "middle" in nl or "body" in nl or "shoulder" in nl or "thigh" in nl or "knee" in nl:
		return 1
	return 0

var _shoe_mat_cached: BaseMaterial3D = null
var _skin_mat_cache: Dictionary = {}

func _get_skin_material(skin_idx: int, tex: Texture2D = null) -> StandardMaterial3D:
	skin_idx = clampi(skin_idx, 0, SKIN_TONES.size() - 1)
	var key := "%d_%s" % [skin_idx, tex.resource_path if tex != null else "notex"]
	if _skin_mat_cache.has(key):
		return _skin_mat_cache[key]
	var tone: Color = SKIN_TONES[skin_idx]
	var mat := StandardMaterial3D.new()
	if tex != null:
		mat.albedo_texture = tex
		mat.albedo_color = Color.WHITE
	else:
		mat.albedo_color = tone
	mat.roughness = 0.48
	mat.specular = 0.40
	if _high_quality:
		mat.rim_enabled = true
		mat.rim = 0.54
		mat.rim_tint = 0.24
		mat.emission_enabled = true
		mat.emission = tone.lerp(Color(0.95, 0.45, 0.32), 0.35)
		mat.emission_energy_multiplier = 0.06
	_skin_mat_cache[key] = mat
	return mat

func _apply_kit_to_meshes(node: Node, shirt: Color, shorts: Color) -> int:
	var n_colored := 0
	var kit_mat := _get_kit_material(shirt, shorts)
	for child in node.get_children():
		if child is MeshInstance3D:
			var mi := child as MeshInstance3D
			if not _high_quality:
				mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			for s in mi.mesh.get_surface_count():
				var kind := _surface_tex_kind(mi, s)
				if kind == 1:
					mi.set_surface_override_material(s, kit_mat)
				elif kind == 2:
					if mi.get_surface_override_material(s) == null:
						if _shoe_mat_cached == null:
							var shoe_src := mi.mesh.surface_get_material(s)
							if shoe_src is BaseMaterial3D:
								_shoe_mat_cached = (shoe_src as BaseMaterial3D).duplicate() as BaseMaterial3D
								_shoe_mat_cached.roughness = 0.45
						if _shoe_mat_cached != null:
							mi.set_surface_override_material(s, _shoe_mat_cached)
				elif kind == 3:
					if mi.get_surface_override_material(s) == null:
						mi.set_surface_override_material(s, _make_face_material(mi, s, 1, 0, 1))
				else:
					if mi.get_surface_override_material(s) == null:
						mi.set_surface_override_material(s, _get_skin_material(1))
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
				bap.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
		_cache_player_procedural_bones()
		return
	# Extract bone track paths (bone name -> NodePath) from an existing clip.
	for anim_name in default_lib.get_animation_list():
		var anim: Animation = default_lib.get_animation(anim_name)
		for i in anim.get_track_count():
			var ttype := anim.track_get_type(i)
			if ttype == Animation.TYPE_ROTATION_3D or ttype == Animation.TYPE_POSITION_3D:
				var p: NodePath = anim.track_get_path(i)
				if p.get_name_count() > 0:
					_bone_paths[p.get_name(p.get_name_count() - 1)] = p
	# Build the dynamic collection (parses templates, replays GenerateAutoAnims).
	if _bone_paths.size() >= 5:
		_anim_collection = DZAnimCollectionScript.new("res://anim_data")
		if _anim_collection.has_method("find_disk_clip_id"):
			var sc_id: int = _anim_collection.find_disk_clip_id("000_showcard")
			if sc_id >= 0:
				_showcard_clip_id = sc_id
		print("[DZFoot] Dynamic animation collection ready: %d clips, %d bone paths (showcard=%d)" % [_anim_collection.get_clip_count(), _bone_paths.size(), _showcard_clip_id])
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
				ap.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
		_cache_player_procedural_bones()
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

func _find_node_by_name_ci(node: Node, target: String) -> Node3D:
	if node == null:
		return null
	if node is Node3D and str(node.name).to_lower() == target.to_lower():
		return node as Node3D
	for child in node.get_children():
		var found := _find_node_by_name_ci(child, target)
		if found:
			return found
	return null

var _player_proc_bones: Array = []
var _active_pred_offset: Vector3 = Vector3.ZERO
var _joy_world_vec: Vector2 = Vector2.ZERO
const CLIP_CELEB_EXTREME: int = 976
const CLIP_CELEB_HAPPY: int = 978
const CLIP_CELEB_SAD: int = 980
const CLIP_SHOWCARD: int = 1332
const CLIP_RUN_STRAIGHT: int = 566

func _cache_player_procedural_bones() -> void:
	_player_proc_bones.clear()
	for i in range(players.size()):
		var p: Node3D = players[i]
		if p == null:
			_player_proc_bones.append(null)
			continue
		var ap := _find_first_anim_player(p)
		var mid := p.find_child("middle", true, false) as Node3D
		var nck := p.find_child("neck", true, false) as Node3D
		var lsh := p.find_child("left_shoulder", true, false) as Node3D
		var rsh := p.find_child("right_shoulder", true, false) as Node3D
		var lel := p.find_child("left_elbow", true, false) as Node3D
		var rel := p.find_child("right_elbow", true, false) as Node3D
		var bdy := p.find_child("body", true, false) as Node3D
		_player_proc_bones.append([ap, mid, nck, lsh, rsh, lel, rel, bdy])

func _update_player_animations_and_ik(delta: float) -> void:
	if _player_proc_bones.size() < players.size():
		_cache_player_procedural_bones()
	var have_ball: bool = (ball != null and _state_received)
	var bpos: Vector3 = ball.position if have_ball else Vector3.ZERO
	for i in range(players.size()):
		var entry = _player_proc_bones[i]
		if entry == null:
			continue
		var ap := entry[0] as AnimationPlayer
		if ap == null or not ap.is_playing():
			continue
		ap.advance(delta)
		var p: Node3D = players[i]
		if p == null or not p.visible or not have_ball:
			continue
		var ft: int = _player_func_types[i] if i < _player_func_types.size() else 0
		# Skip procedural upper-body offsets during ground slides (7), GK dives (9), or trips/falls (11)
		if ft == 7 or ft == 9 or ft == 11:
			continue
		var mid := entry[1] as Node3D
		var nck := entry[2] as Node3D
		var lsh := entry[3] as Node3D
		var rsh := entry[4] as Node3D
		var lel := entry[5] as Node3D
		var rel := entry[6] as Node3D
		var bdy := entry[7] as Node3D if entry.size() > 7 else null

		# Strip redundant horizontal yaw from `body` bone on standing clips:
		# GameServer sends instantaneous `ps.rotY` (which already rotates during turn clips),
		# so keeping the clip's progressive `body` yaw on top of `p.rotation.y` doubles the turn
		# angle and causes a snap when the next clip resets `body` yaw to 0°. Preserves full pitch/lean and roll!
		if bdy != null and ft != 14:
			var bfwd: Vector3 = bdy.transform.basis.z
			var bh2: float = bfwd.x * bfwd.x + bfwd.z * bfwd.z
			if bh2 > 0.08:
				var byaw: float = atan2(bfwd.x, bfwd.z)
				bdy.quaternion = Quaternion(Vector3.UP, -byaw) * bdy.quaternion

		# 1. Port of Humanoid::CalculateGeomOffsets adaptBodyToBallPosition (humanoid.cpp:875-914)
		# Note: player_base.glb faces +Z at rotation.y = 0, so forward yaw toward (dx, dz) is atan2(dx, dz).
		var to_ball: Vector3 = bpos - p.position
		var dist_2d: float = Vector2(to_ball.x, to_ball.z).length()
		if dist_2d > 0.35 and dist_2d < 42.0:
			var target_yaw: float = atan2(to_ball.x, to_ball.z)
			var rel_yaw: float = angle_difference(p.rotation.y, target_yaw)
			if absf(rel_yaw) < 2.35:
				var prox_fade: float = clampf((dist_2d - 0.55) / 1.15, 0.25, 1.0)
				var spd: float = _player_vels[i].length() if i < _player_vels.size() else 0.0
				var velo_fade: float = 1.0 - clampf(spd / 8.5, 0.0, 1.0) * 0.28
				var bias: float = prox_fade * velo_fade
				if mid != null:
					var mid_yaw: float = clampf(rel_yaw * 0.22 * bias, -0.32, 0.32)
					mid.quaternion = Quaternion(Vector3(0.0, 1.0, 0.0), mid_yaw) * mid.quaternion
				if nck != null:
					var nck_yaw: float = clampf(rel_yaw * 0.52 * bias, -0.78, 0.78)
					var pitch_look: float = clampf((bpos.y - 1.45) / maxf(dist_2d, 1.6) * 0.48, -0.26, 0.42) * bias
					var q_neck := Quaternion(Vector3(0.0, 1.0, 0.0), nck_yaw) * Quaternion(Vector3(1.0, 0.0, 0.0), pitch_look)
					nck.quaternion = q_neck * nck.quaternion

		# 2. Port of Humanoid::CalculateGeomOffsets adaptArmsToOpp (humanoid.cpp:919-969):
		# Hold out arm & shoulder to shield position when an opponent is within 1.45m
		if ft <= 3:
			var opp_base: int = (1 - (i / 11)) * 11
			var best_opp_d: float = 1.45
			var best_opp_vec := Vector3.ZERO
			for j in range(11):
				var oi := opp_base + j
				if oi < players.size() and players[oi] != null:
					var dv: Vector3 = players[oi].position - p.position
					var d2: float = Vector2(dv.x, dv.z).length()
					if d2 < best_opp_d and d2 > 0.15:
						best_opp_d = d2
						best_opp_vec = dv
			if best_opp_d < 1.45:
				var shield_w: float = clampf((1.45 - best_opp_d) / 0.85, 0.0, 1.0) * 0.72
				var opp_yaw: float = angle_difference(p.rotation.y, atan2(best_opp_vec.x, best_opp_vec.z))
				if absf(opp_yaw) > 0.35 and absf(opp_yaw) < 2.65:
					if opp_yaw < 0.0:
						# Opponent on right side -> raise right shoulder & bend right elbow
						if rsh != null:
							rsh.quaternion = rsh.quaternion.slerp(Quaternion(Vector3(0.0, 0.0, -1.0), 0.36 * PI) * rsh.quaternion, shield_w)
						if rel != null:
							rel.quaternion = rel.quaternion.slerp(Quaternion(Vector3(1.0, 0.0, 0.0), 0.35 * PI) * rel.quaternion, shield_w)
					else:
						# Opponent on left side -> raise left shoulder & bend left elbow
						if lsh != null:
							lsh.quaternion = lsh.quaternion.slerp(Quaternion(Vector3(0.0, 0.0, 1.0), 0.36 * PI) * lsh.quaternion, shield_w)
						if lel != null:
							lel.quaternion = lel.quaternion.slerp(Quaternion(Vector3(1.0, 0.0, 0.0), 0.35 * PI) * lel.quaternion, shield_w)
					if mid != null:
						mid.quaternion = Quaternion(Vector3(1.0, 0.0, 0.0), -0.12 * PI * shield_w) * mid.quaternion

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
		if _dyn_lib != null:
			var oap := _find_first_anim_player(official)
			if oap != null and not oap.has_animation_library("dzdyn"):
				oap.add_animation_library("dzdyn", _dyn_lib)
		officials.append(official)
		add_child(official)
	if officials.size() > 0 and officials[0] != null:
		_ref_right_elbow = _find_node_by_name_ci(officials[0], "right_elbow")
		_ref_left_elbow = _find_node_by_name_ci(officials[0], "left_elbow")
		var cmesh := BoxMesh.new()
		cmesh.size = Vector3(0.09, 0.13, 0.008)
		_ref_card_mat = StandardMaterial3D.new()
		_ref_card_mat.albedo_color = Color(1.0, 0.88, 0.04)
		_ref_card_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		cmesh.material = _ref_card_mat
		_ref_card_mesh = MeshInstance3D.new()
		_ref_card_mesh.mesh = cmesh
		_ref_card_mesh.visible = false
		add_child(_ref_card_mesh)

# === Touch UI ===

func _setup_touch_ui() -> void:
	_touch_canvas = CanvasLayer.new()
	_touch_canvas.layer = 10
	add_child(_touch_canvas)

	var root_control := Control.new()
	root_control.set_anchors_preset(Control.PRESET_FULL_RECT)
	root_control.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_touch_canvas.add_child(root_control)

	# Modern FIFA / eFootball style Virtual Joystick (bottom-left)
	_joy_base = Panel.new()
	_joy_base.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_joy_base.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	_joy_base.size = Vector2(_joy_radius * 2.0, _joy_radius * 2.0)
	_joy_base.position = Vector2(115, -95 - _joy_radius * 2.0)
	var jb_style := StyleBoxFlat.new()
	jb_style.bg_color = Color(0.05, 0.08, 0.12, 0.42 if not _software_gl else 1.0)
	jb_style.border_color = Color(0.75, 0.88, 1.0, 0.55)
	jb_style.set_border_width_all(3)
	jb_style.set_corner_radius_all(int(_joy_radius))
	_joy_base.add_theme_stylebox_override("panel", jb_style)
	root_control.add_child(_joy_base)

	# Inner guide ring inside joystick
	var joy_inner := Panel.new()
	joy_inner.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var ji_r := _joy_radius * 0.55
	joy_inner.size = Vector2(ji_r * 2.0, ji_r * 2.0)
	joy_inner.position = Vector2(_joy_radius - ji_r, _joy_radius - ji_r)
	var ji_style := StyleBoxFlat.new()
	ji_style.bg_color = Color(0, 0, 0, 0)
	ji_style.border_color = Color(1.0, 1.0, 1.0, 0.16)
	ji_style.set_border_width_all(2)
	ji_style.set_corner_radius_all(int(ji_r))
	joy_inner.add_theme_stylebox_override("panel", ji_style)
	_joy_base.add_child(joy_inner)

	# glowing directional dot on the outer rim of the joystick
	_joy_dir_dot = _create_circle_control(9.0, Color(0.25, 0.95, 0.65, 0.95))
	_joy_dir_dot.visible = false
	_joy_base.add_child(_joy_dir_dot)

	# Ergonomic thumb stick knob (double ring)
	var knob_r := _joy_radius * 0.38
	_joy_knob = Panel.new()
	_joy_knob.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_joy_knob.size = Vector2(knob_r * 2.0, knob_r * 2.0)
	_joy_knob.position = Vector2(_joy_radius - knob_r, _joy_radius - knob_r)
	var jk_style := StyleBoxFlat.new()
	jk_style.bg_color = Color(0.14, 0.20, 0.28, 0.88 if not _software_gl else 1.0)
	jk_style.border_color = Color(0.38, 0.86, 1.0, 0.92)
	jk_style.set_border_width_all(4)
	jk_style.set_corner_radius_all(int(knob_r))
	_joy_knob.add_theme_stylebox_override("panel", jk_style)
	_joy_base.add_child(_joy_knob)

	# 4 Contextual Action Buttons in an ergonomic thumb arc (bottom-right)
	for btn_name in BUTTON_DEFS:
		var def: Dictionary = BUTTON_DEFS[btn_name]
		var r: float = float(def.radius)
		var btn := _create_pro_button_control(r, def.atk_color)
		btn.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
		btn.position = def.pos
		btn.mouse_filter = Control.MOUSE_FILTER_IGNORE

		var title_lbl := Label.new()
		title_lbl.name = "TitleLabel"
		title_lbl.text = def.atk_label
		title_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		title_lbl.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		title_lbl.size = Vector2(r * 2.0, r * 1.15)
		title_lbl.position = Vector2(0, r * 0.22)
		title_lbl.add_theme_font_size_override("font_size", 28 if r >= 88.0 else 24)
		title_lbl.add_theme_color_override("font_color", Color.WHITE)
		title_lbl.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.85))
		title_lbl.add_theme_constant_override("shadow_offset_x", 1)
		title_lbl.add_theme_constant_override("shadow_offset_y", 2)
		btn.add_child(title_lbl)

		var sub_lbl := Label.new()
		sub_lbl.name = "SubLabel"
		sub_lbl.text = def.atk_sub
		sub_lbl.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		sub_lbl.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		sub_lbl.size = Vector2(r * 2.0, r * 0.70)
		sub_lbl.position = Vector2(0, r * 1.04)
		sub_lbl.add_theme_font_size_override("font_size", 15)
		sub_lbl.add_theme_color_override("font_color", def.atk_color.lightened(0.35))
		btn.add_child(sub_lbl)

		root_control.add_child(btn)
		_action_buttons[btn_name] = {
			"control": btn,
			"title_label": title_lbl,
			"sub_label": sub_lbl,
			"radius": r,
			"mask": def.atk_mask,
			"color": def.atk_color,
			"def": def,
			"pressed": false,
			"pointer": -1,
			"base_pos": btn.position,
		}

	# Camera mode toggle (top-right) — sleek pill/circle button
	_cam_btn = _create_pro_button_control(40.0, Color(0.55, 0.75, 0.95))
	_cam_btn.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	_cam_btn.position = Vector2(-104, 96)
	_cam_btn.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_cam_btn_label = Label.new()
	_cam_btn_label.text = "CAM"
	_cam_btn_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_cam_btn_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_cam_btn_label.size = Vector2(80, 80)
	_cam_btn_label.add_theme_font_size_override("font_size", 18)
	_cam_btn_label.add_theme_color_override("font_color", Color.WHITE)
	_cam_btn.add_child(_cam_btn_label)
	root_control.add_child(_cam_btn)

func _create_pro_button_control(radius: float, accent_color: Color) -> Panel:
	var panel := Panel.new()
	var style := StyleBoxFlat.new()
	var bg := Color(0.05, 0.08, 0.13, 0.78)
	if _software_gl:
		bg.a = 1.0
	style.bg_color = bg
	style.border_color = Color(accent_color.r, accent_color.g, accent_color.b, 0.92)
	style.set_border_width_all(4)
	var r := int(radius)
	style.set_corner_radius_all(r)
	style.shadow_color = Color(0.0, 0.0, 0.0, 0.45)
	style.shadow_size = 6
	panel.add_theme_stylebox_override("panel", style)
	panel.size = Vector2(radius * 2.0, radius * 2.0)
	panel.pivot_offset = Vector2(radius, radius)
	return panel

func _set_touch_Attack_defense_mode(attacking: bool) -> void:
	if _is_attacking_mode == attacking:
		return
	# Never switch button masks while a finger is actively holding an action button
	for bname in _action_buttons:
		if _action_buttons[bname].pressed:
			return
	var now_m := Time.get_ticks_msec()
	if now_m - _last_mode_switch_ms < 350:
		return
	_is_attacking_mode = attacking
	_last_mode_switch_ms = now_m
	for btn_name in _action_buttons:
		var info: Dictionary = _action_buttons[btn_name]
		var def: Dictionary = info.def
		var col: Color = def.atk_color if attacking else def.def_color
		var lbl: String = def.atk_label if attacking else def.def_label
		var sub: String = def.atk_sub if attacking else def.def_sub
		var msk: int = int(def.atk_mask if attacking else def.def_mask)
		info.mask = msk
		info.color = col
		(info.title_label as Label).text = lbl
		var sl := info.sub_label as Label
		sl.text = sub
		sl.add_theme_color_override("font_color", col.lightened(0.35))
		var panel := info.control as Panel
		var st := panel.get_theme_stylebox("panel") as StyleBoxFlat
		if st:
			st.border_color = Color(col.r, col.g, col.b, 0.92)

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

	# Apply interpolation + procedural upper-body IK offsets (head look-at-ball & duel arm shielding)
	_apply_interpolation(delta)
	_update_player_animations_and_ik(delta)
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
	var fps := Engine.get_frames_per_second()
	if n > 0:
		print("[DZ_VERIFY] fps=%.0f moving=%d avgCosSrvDirVsMotion=%.2f (neg=%d) avgCosVisualFacingVsMotion=%.2f (neg=%d) clipHit=%d miss=%d | %s" % [fps, n, sum_srv / n, neg_srv, sum_vis / n, neg_vis, _clip_hits, _clip_misses, sample])
	else:
		print("[DZ_VERIFY] fps=%.0f no player >3m/s this window; clipHit=%d miss=%d" % [fps, _clip_hits, _clip_misses])

func _update_camera_smooth(delta: float) -> void:
	if not _cam_have_target or camera == null:
		return
	# Pull raw position toward look point (broadcast framing fix), then apply
	# goal-zoom factor on top of the fixed distance scale.
	var k: float = 1.0 - exp(-CAM_SMOOTH_RATE * delta)
	if _cam_mode == 1 and ball != null:
		# Action cam: closer, lower, follows the ball from the +Z touchline side.
		var bpos := ball.position
		var apos := bpos + Vector3(0, 5.8, 14.3)
		camera.position = camera.position.lerp(apos, k)
		var aq := Basis.looking_at((bpos - apos).normalized(), Vector3.UP).get_rotation_quaternion()
		camera.quaternion = camera.quaternion.slerp(aq, k)
		camera.fov = lerpf(camera.fov, 45.0, k)
		return
	if Time.get_ticks_msec() < _goal_cam_until_ms and players.size() >= DZ_MAX_PLAYERS:
		# Goal celebration camera: GF sends the scorer sprinting toward the corner flag
		# (±55, 0, ±36) while his teammates run right behind him, then plays the corner
		# celebration animations. Frame from IN FRONT of the scorer (between corner flag
		# and scorer) looking back across the scorer toward his incoming teammates!
		var s_idx := _goal_scorer_idx
		if s_idx < 0 or s_idx >= players.size():
			# Fallback: find closest player to the goal where the ball went in
			var best_d := 999.0
			for pi in range(DZ_MAX_PLAYERS):
				var d := players[pi].position.distance_to(ball.position)
				if d < best_d:
					best_d = d
					s_idx = pi
		var sc: Vector3 = players[s_idx].position
		var team_base: int = (s_idx / 11) * 11
		# Compute centroid of the closest teammates running behind the scorer
		var mate_sum := Vector3.ZERO
		var mate_cnt := 0
		for m in range(11):
			var mi := team_base + m
			if mi != s_idx and mi < players.size():
				if players[mi].position.distance_to(sc) < 28.0:
					mate_sum += players[mi].position
					mate_cnt += 1
		var behind_pt := (mate_sum / float(mate_cnt)) if mate_cnt > 0 else Vector3.ZERO
		# Vector from teammates -> scorer (points toward the corner where scorer runs)
		var front_dir := Vector3(sc.x - behind_pt.x, 0.0, sc.z - behind_pt.z)
		if front_dir.length() < 0.5:
			var cx := 55.0 if sc.x >= 0.0 else -55.0
			var cz := 36.0 if sc.z >= 0.0 else -36.0
			front_dir = Vector3(cx - behind_pt.x, 0.0, cz - behind_pt.z)
		front_dir = front_dir.normalized()
		# Place camera 6.4m in front of the scorer (toward the corner) at 2.15m height,
		# looking back through the scorer (at chest height) toward his teammates behind him.
		var gpos := Vector3(sc.x, 0.0, sc.z) + front_dir * 6.4 + Vector3(0.0, 2.15, 0.0)
		var look_target := Vector3(sc.x, 1.15, sc.z) - front_dir * 5.5
		var k_cel: float = 1.0 - exp(-6.5 * delta)
		camera.position = camera.position.lerp(gpos, k_cel)
		var gq := Basis.looking_at((look_target - gpos).normalized(), Vector3.UP).get_rotation_quaternion()
		camera.quaternion = camera.quaternion.slerp(gq, k_cel)
		camera.fov = lerpf(camera.fov, 42.0, k_cel)
		return
	var is_card_or_setpiece: bool = (_cam_raw_pos.y < 5.0) or (_set_piece_type >= 2)
	var dist_scale: float = CAM_DIST_SCALE if is_card_or_setpiece else 0.71
	var min_h: float = CAM_MIN_HEIGHT if is_card_or_setpiece else 6.6
	var target_pos := _cam_look_pt + (_cam_raw_pos - _cam_look_pt) * dist_scale
	if target_pos.y < min_h:
		target_pos.y = min_h
	camera.position = camera.position.lerp(target_pos, k)
	camera.quaternion = camera.quaternion.slerp(_cam_target_quat, k)
	camera.fov = lerpf(camera.fov, _cam_target_fov, k)

func _store_interpolation_state(bridge: Object) -> void:
	var new_tick: int = bridge.get_tick()
	if new_tick == _last_interp_tick and _state_received:
		return
	var now_recv_ms := Time.get_ticks_msec()
	var new_ts_us: int = bridge.get_timestamp_us() if bridge.has_method("get_timestamp_us") else 0
	var dt_srv: float = 0.05
	if _last_interp_ts_us > 0 and new_ts_us > _last_interp_ts_us and (new_ts_us - _last_interp_ts_us) <= 300000:
		dt_srv = clampf(float(new_ts_us - _last_interp_ts_us) * 0.000001, 0.025, 0.15)
	elif _last_interp_tick > 0 and new_tick > _last_interp_tick and (new_tick - _last_interp_tick) <= 25:
		dt_srv = clampf(float(new_tick - _last_interp_tick) * 0.0125, 0.025, 0.15)
	if _last_packet_recv_ms > 0:
		var arrival_dt: float = clampf(float(now_recv_ms - _last_packet_recv_ms) * 0.001, 0.010, 0.250)
		var inst_jitter: float = absf(arrival_dt - dt_srv)
		_jitter_ema_sec = lerpf(_jitter_ema_sec, inst_jitter, 0.15)
		_state_interp_period = clampf(dt_srv, 0.044, 0.056)
		_adaptive_extrap_tau = clampf(0.055 + 0.85 * _jitter_ema_sec, 0.055, 0.095)
	_last_packet_recv_ms = now_recv_ms
	_last_interp_tick = new_tick
	_last_interp_ts_us = new_ts_us

	for i in range(DZ_MAX_PLAYERS):
		var new_pos: Vector3 = _gf_to_godot(bridge.get_player_position(i))
		var new_rot_y: float = PI - bridge.get_player_rotation_y(i)
		var new_clip_id: int = bridge.get_player_clip_id(i)
		if not _state_received or new_pos.distance_to(_curr_positions[i]) > 3.5:
			# Initial frame or server set-piece / kickoff teleport: snap cleanly
			_prev_positions[i] = new_pos
			_curr_positions[i] = new_pos
			_net_prev_positions[i] = new_pos
			_player_vels[i] = Vector3.ZERO
			_prev_rot_y[i] = new_rot_y
			_curr_rot_y[i] = new_rot_y
			if i == _active_player_idx:
				_active_pred_offset = Vector3.ZERO
			if i < players.size() and players[i] != null:
				players[i].position = new_pos
				players[i].rotation.y = new_rot_y
		else:
			# Continuous play: compute exact server velocity and start from current server-tracked base
			var raw_vel: Vector3 = (new_pos - _net_prev_positions[i]) / dt_srv
			raw_vel.y = 0.0
			if raw_vel.length() > 10.5:
				raw_vel = raw_vel.normalized() * 10.5
			_player_vels[i] = _player_vels[i].lerp(raw_vel, 0.82)
			var cur_vis_base: Vector3 = players[i].position if (i < players.size() and players[i] != null) else _curr_positions[i]
			if i == _active_player_idx:
				cur_vis_base -= _active_pred_offset
			_prev_positions[i] = cur_vis_base
			_net_prev_positions[i] = new_pos
			_curr_positions[i] = new_pos
			var vis_rot: float = players[i].rotation.y if (i < players.size() and players[i] != null) else _curr_rot_y[i]
			_prev_rot_y[i] = vis_rot
			_curr_rot_y[i] = new_rot_y
		_prev_clip_id[i] = new_clip_id

	var new_bpos: Vector3 = _gf_to_godot(bridge.get_ball_position())
	# Headless server fallback if throw-in ball reports (0, 0, ~1.4) before first hand-attachment frame
	var gm: int = bridge.get_game_mode() if bridge.has_method("get_game_mode") else 0
	if (gm == 5 or _set_piece_type == 5) and absf(new_bpos.x) < 1.5 and absf(new_bpos.z) < 1.5:
		var taker_idx := -1
		for i in range(DZ_MAX_PLAYERS):
			if absf(_curr_positions[i].z) > 34.0:
				taker_idx = i
				break
		if taker_idx < 0 and _active_player_idx >= 0 and _active_player_idx < DZ_MAX_PLAYERS:
			taker_idx = _active_player_idx
		if taker_idx >= 0:
			new_bpos = _curr_positions[taker_idx] + Vector3(0.0, 1.65, 0.0)

	var srv_bvel: Vector3 = _gf_to_godot(bridge.get_ball_velocity()) if bridge.has_method("get_ball_velocity") else Vector3.ZERO
	if not _state_received or new_bpos.distance_to(_curr_ball_pos) > 8.0:
		_prev_ball_pos = new_bpos
		_curr_ball_pos = new_bpos
		_net_prev_ball_pos = new_bpos
		_ball_vel = Vector3.ZERO
		if ball != null:
			ball.position = new_bpos
	else:
		_prev_ball_pos = ball.position if ball != null else _curr_ball_pos
		var diff_bvel: Vector3 = (new_bpos - _curr_ball_pos) / dt_srv
		_ball_vel = srv_bvel if srv_bvel.length() > 0.1 else diff_bvel
		if _ball_vel.length() > 36.0:
			_ball_vel = _ball_vel.normalized() * 36.0
		_net_prev_ball_pos = _curr_ball_pos
		_curr_ball_pos = new_bpos

	for i in range(3):
		var opos: Vector3 = _gf_to_godot(bridge.get_official_position(i))
		var orot: float = PI - bridge.get_official_rotation_y(i)
		if not _state_received or opos.distance_to(_curr_official_pos[i]) > 3.5:
			_prev_official_pos[i] = opos
			_curr_official_pos[i] = opos
			_prev_official_rot[i] = orot
			_curr_official_rot[i] = orot
		else:
			_prev_official_pos[i] = officials[i].position if (i < officials.size() and officials[i] != null) else _curr_official_pos[i]
			_curr_official_pos[i] = opos
			_prev_official_rot[i] = officials[i].rotation.y if (i < officials.size() and officials[i] != null) else _curr_official_rot[i]
			_curr_official_rot[i] = orot

	_state_interp_time = 0.0
	_state_received = true

var _player_elbows: Array = []

func _get_player_hands_ball_pos(p_idx: int, force_two_hands: bool, ref_ball_pos: Vector3) -> Vector3:
	if p_idx < 0 or p_idx >= players.size() or players[p_idx] == null:
		return ref_ball_pos
	if _player_elbows.size() != DZ_MAX_PLAYERS:
		_player_elbows.clear()
		for i in range(DZ_MAX_PLAYERS):
			if i < players.size() and players[i] != null:
				_player_elbows.append([
					players[i].find_child("left_elbow", true, false) as Node3D,
					players[i].find_child("right_elbow", true, false) as Node3D
				])
			else:
				_player_elbows.append([null, null])
	var elbows: Array = _player_elbows[p_idx]
	var l_elbow := elbows[0] as Node3D
	var r_elbow := elbows[1] as Node3D
	if l_elbow == null or r_elbow == null:
		return ref_ball_pos
	# In Godot Y-up skeleton converted from GF Z-up, the forearm extends -0.34m along local Y from the elbow to the palm/fingers
	var l_hand: Vector3 = l_elbow.global_position - l_elbow.global_transform.basis.y * 0.34
	var r_hand: Vector3 = r_elbow.global_position - r_elbow.global_transform.basis.y * 0.34
	if force_two_hands or l_hand.distance_to(r_hand) < 0.68:
		var mid: Vector3 = (l_hand + r_hand) * 0.5
		mid.y = maxf(mid.y, _pitch_top_y + 0.13)
		return mid
	# Single-hand hold (e.g. goalkeeper carrying ball in one hand): pick mirrored hand from clip_id or closest hand
	var clip_id: int = dz_bridge.get_player_clip_id(p_idx) if (dz_bridge and dz_bridge.has_method("get_player_clip_id")) else 0
	var hand_pos: Vector3 = l_hand if (clip_id % 2 == 1) else r_hand
	if l_hand.distance_to(ref_ball_pos) + 0.15 < r_hand.distance_to(ref_ball_pos):
		hand_pos = l_hand
	elif r_hand.distance_to(ref_ball_pos) + 0.15 < l_hand.distance_to(ref_ball_pos):
		hand_pos = r_hand
	hand_pos.y = maxf(hand_pos.y, _pitch_top_y + 0.13)
	return hand_pos

func _find_ball_in_hands_player() -> int:
	if not dz_bridge:
		return -1
	# 1. Explicit server flag 0x10 (m->GetBallRetainer() == p)
	for i in range(DZ_MAX_PLAYERS):
		var fl: int = dz_bridge.get_player_flags(i)
		if (fl & 0x10) != 0:
			return i
	# 2. Throw-in set piece (_set_piece_type == 5): attach to throw-in taker on touchline while ball is with them
	if _set_piece_type == 5:
		var best_taker: int = dz_bridge.get_set_piece_taker() if dz_bridge.has_method("get_set_piece_taker") else -1
		if best_taker >= 0 and best_taker < DZ_MAX_PLAYERS and best_taker < 11:
			var sp_team: int = dz_bridge.get_set_piece_team() if dz_bridge.has_method("get_set_piece_team") else -1
			if sp_team == 1 and absf(players[best_taker].position.z) < 30.0 and absf(players[best_taker + 11].position.z) > 33.0:
				best_taker += 11
		if best_taker < 0 or best_taker >= DZ_MAX_PLAYERS:
			for i in range(DZ_MAX_PLAYERS):
				var fl: int = dz_bridge.get_player_flags(i)
				if absf(players[i].position.z) > 33.8 and ((fl & 0x08) != 0 or (fl & 0x04) != 0):
					best_taker = i
					break
		if best_taker < 0 or best_taker >= DZ_MAX_PLAYERS:
			for i in range(DZ_MAX_PLAYERS):
				if absf(players[i].position.z) > 34.0:
					best_taker = i
					break
		if best_taker >= 0 and best_taker < DZ_MAX_PLAYERS:
			var d2d := Vector2(ball.position.x - players[best_taker].position.x, ball.position.z - players[best_taker].position.z).length()
			if d2d < 1.65 or (absf(_curr_ball_pos.x) < 1.5 and absf(_curr_ball_pos.z) < 1.5):
				return best_taker
	# 3. Goalkeeper (idx 0 or 11) holding or catching the ball in hands
	for gk_idx in [0, 11]:
		if gk_idx >= players.size():
			continue
		var fl: int = dz_bridge.get_player_flags(gk_idx)
		var ft: int = dz_bridge.get_player_function_type(gk_idx) if dz_bridge.has_method("get_player_function_type") else 0
		var d2d := Vector2(ball.position.x - players[gk_idx].position.x, ball.position.z - players[gk_idx].position.z).length()
		# ft 9 = Deflect, ft 10 = Catch, or GK has possession (0x08) with ball elevated in hands (y > 0.28m)
		if (fl & 0x08) != 0 and d2d < 1.45 and (ft == 9 or ft == 10 or _curr_ball_pos.y > _pitch_top_y + 0.28):
			return gk_idx
	return -1

func _apply_interpolation(delta: float) -> void:
	if not _state_received:
		return
	_state_interp_time += delta
	var alpha: float = clampf(_state_interp_time / _state_interp_period, 0.0, 1.0)
	var over_time: float = maxf(0.0, _state_interp_time - _state_interp_period)
	var extrap_dt: float = _adaptive_extrap_tau * (1.0 - exp(-over_time / _adaptive_extrap_tau))
	var vel_damp: float = exp(-over_time * 3.5)

	# Client-side prediction lead on active player so joystick movement has zero perceived RTT lag
	var joy_mag: float = clampf(_move_dir.length(), 0.0, 1.0) if (_joy_active and _move_dir.length() >= JOY_DEADZONE) else 0.0
	if joy_mag > 0.01 and _joy_world_vec.length() > 0.01 and _active_player_idx >= 0 and _active_player_idx < DZ_MAX_PLAYERS:
		var jdir3 := Vector3(_joy_world_vec.x, 0.0, _joy_world_vec.y).normalized()
		var srv_v: Vector3 = _player_vels[_active_player_idx]
		var srv_along: float = maxf(0.0, srv_v.dot(jdir3))
		var target_spd: float = (6.8 if (_buttons & BUTTON_SPRINT) != 0 else 5.0) * joy_mag
		var lead_deficit: float = maxf(0.0, target_spd - srv_along)
		var target_offset: Vector3 = jdir3 * clampf(lead_deficit * 0.095, 0.0, 0.52)
		_active_pred_offset = _active_pred_offset.lerp(target_offset, 1.0 - exp(-18.0 * delta))
	else:
		_active_pred_offset = _active_pred_offset.lerp(Vector3.ZERO, 1.0 - exp(-14.0 * delta))

	# Interpolate players (position + rotation + bounded elastic dead-reckoning on late packet)
	var kicker_idx: int = -1
	for i in range(DZ_MAX_PLAYERS):
		var p_pos: Vector3 = _prev_positions[i].lerp(_curr_positions[i], alpha)
		if extrap_dt > 0.0001:
			p_pos += _player_vels[i] * (extrap_dt * vel_damp)
		var ft_i: int = _player_func_types[i] if i < _player_func_types.size() else 0
		if i == _active_player_idx and ft_i <= 3:
			p_pos += _active_pred_offset
		players[i].position = p_pos
		var rot_alpha: float = clampf(alpha * 1.65, 0.0, 1.0) if (ft_i >= 4 and ft_i <= 8) else alpha
		var srv_rot_y: float = lerp_angle(_prev_rot_y[i], _curr_rot_y[i], rot_alpha)
		if i == _active_player_idx and joy_mag > 0.01 and _joy_world_vec.length() > 0.01 and ft_i <= 2:
			var joy_rot_y: float = atan2(_joy_world_vec.x, _joy_world_vec.y)
			players[i].rotation.y = lerp_angle(players[i].rotation.y, lerp_angle(srv_rot_y, joy_rot_y, 0.78), 1.0 - exp(-22.0 * delta))
		else:
			players[i].rotation.y = srv_rot_y
		if (ft_i == 4 or ft_i == 5 or ft_i == 6 or ft_i == 8) and i < _player_anim_progs.size():
			var pr_i: float = _player_anim_progs[i]
			if pr_i >= 0.10 and pr_i <= 0.46:
				kicker_idx = i

	# Interpolate officials at 60 FPS (+ orient referee toward booked player & attach 3D card to raised hand)
	var now_interp_ms := Time.get_ticks_msec()
	var srv_off0_flags: int = dz_bridge.get_official_flags(0) if (dz_bridge and dz_bridge.has_method("get_official_flags")) else 0
	var showing_card_now: bool = (now_interp_ms < _card_anim_until_ms) or ((srv_off0_flags & 0x06) != 0)
	for i in range(min(3, officials.size())):
		officials[i].position = _prev_official_pos[i].lerp(_curr_official_pos[i], alpha)
		if i == 0 and showing_card_now and _card_player_idx >= 0 and _card_player_idx < players.size():
			var to_p: Vector3 = players[_card_player_idx].position - officials[0].position
			if Vector2(to_p.x, to_p.z).length() > 0.2:
				var face_y := atan2(to_p.x, to_p.z)
				officials[0].rotation.y = lerp_angle(officials[0].rotation.y, face_y, 0.25)
			else:
				officials[0].rotation.y = lerp_angle(_prev_official_rot[i], _curr_official_rot[i], alpha)
		else:
			officials[i].rotation.y = lerp_angle(_prev_official_rot[i], _curr_official_rot[i], alpha)
		_official_positions[i] = officials[i].position
	if _ref_card_mesh != null:
		if showing_card_now and officials.size() > 0 and officials[0] != null:
			var is_red_now: bool = _card_is_red if (now_interp_ms < _card_anim_until_ms) else ((srv_off0_flags & 0x04) != 0)
			_ref_card_mat.albedo_color = Color(0.96, 0.10, 0.10) if is_red_now else Color(1.0, 0.88, 0.04)
			var use_left: bool = (srv_off0_flags & 0x08) != 0
			var hand_node: Node3D = _ref_left_elbow if (use_left and _ref_left_elbow != null) else _ref_right_elbow
			if hand_node != null:
				var ht := hand_node.global_transform
				_ref_card_mesh.global_position = ht.origin - ht.basis.y * 0.26 + Vector3(0.0, 0.05, 0.0)
				_ref_card_mesh.global_basis = ht.basis
			else:
				_ref_card_mesh.global_position = officials[0].global_position + Vector3(0.0, 2.05, 0.0)
			_ref_card_mesh.visible = true
		else:
			_ref_card_mesh.visible = false

	# Interpolate ball (+ bounded ballistic dead-reckoning when free in play)
	var b_pos: Vector3 = _prev_ball_pos.lerp(_curr_ball_pos, alpha)
	# If a player is holding the ball in their hands (throw-in or goalkeeper catch/retain), snap ball to their 3D hands
	var hand_holder_idx := _find_ball_in_hands_player()
	if hand_holder_idx < 0 and extrap_dt > 0.0001 and _ball_vel.length() > 0.25:
		b_pos += _ball_vel * extrap_dt
		if b_pos.y > _pitch_top_y + 0.16:
			b_pos.y -= 4.9 * extrap_dt * extrap_dt
	ball.position = b_pos
	if hand_holder_idx >= 0:
		var is_throw_in: bool = (hand_holder_idx != 0 and hand_holder_idx != 11)
		ball.position = _get_player_hands_ball_pos(hand_holder_idx, is_throw_in, ball.position)
	elif kicker_idx >= 0 and kicker_idx < players.size() and ball.position.y < _pitch_top_y + 0.36 and _ball_vel.length() < 7.5:
		# Foot Contact Magnet: softly pull ground ball toward the striking foot point right before kick impact
		var kp: Vector3 = players[kicker_idx].position
		var d2k: float = Vector2(ball.position.x - kp.x, ball.position.z - kp.z).length()
		if d2k < 0.72:
			var kry: float = players[kicker_idx].rotation.y
			var kfwd := Vector3(sin(kry), 0.0, cos(kry))
			var strike_pt := Vector3(kp.x + kfwd.x * 0.36, ball.position.y, kp.z + kfwd.z * 0.36)
			ball.position = ball.position.lerp(strike_pt, 0.32)
	# Ball is rendered at 2x scale (radius 0.15) — keep it resting on the turf.
	ball.position.y = maxf(ball.position.y, _pitch_top_y + 0.12)
	# Keep ball physically inside the goal net when it enters the goal mouth
	if absf(ball.position.x) > 55.05 and absf(ball.position.z) < 3.72 and ball.position.y < 2.52:
		var gside := signf(ball.position.x)
		var pen := absf(ball.position.x) - 55.05
		if pen > 2.35:
			ball.position.x = gside * (55.05 + 2.35 - (pen - 2.35) * 0.25)
		ball.position.z = clampf(ball.position.z, -3.55, 3.55)
		ball.position.y = clampf(ball.position.y, _pitch_top_y + 0.12, 2.38)
		if _goal_net_impact_strength < 0.35:
			_goal_net_impact_pos = ball.position
			_goal_net_impact_strength = 1.0
			_goal_net_impact_time = 0.0

func _apply_game_state(bridge: Object) -> void:
	var tick: int = bridge.get_tick()
	if tick == last_tick:
		return
	last_tick = tick

	# Clear stale set-piece state immediately once the ball is kicked into play
	var gs_in_play: bool = (bridge.get_game_flags() & 1) != 0 if bridge.has_method("get_game_flags") else true
	if _set_piece_type > 0 and _ball_vel.length() > 2.5:
		_set_piece_type = 0
		_auto_dir_ms = 0

	# Detect active player from flags (flag bit 2 = 0x04 = human-controlled).
	# Team is derived from index: players 0-10 = team A, 11-21 = team B
	# (GameServer writes idx = i + teamId*11). Reading the team byte from
	# state is fragile — a stale layout once produced team=15, which the
	# server drops (input.team <= 1), freezing the match at kickoff.
	# Detect active player & team possession from flags (0x04 = human-selected, 0x08 = has_possession)
	if dz_plugin and dz_plugin.has_method("get_my_team"):
		var mt: int = clampi(int(dz_plugin.get_my_team()), 0, 1)
		if mt != _active_team:
			_active_team = mt
			_active_player_idx = _active_team * 11 + 9
	var team_has_ball := false
	var opp_has_ball := false
	_opp_active_player_idx = -1
	for i in range(DZ_MAX_PLAYERS):
		var flags: int = bridge.get_player_flags(i)
		var p_team: int = i / 11
		if (flags & 4) and p_team == _active_team:
			if i != _active_player_idx:
				print("[DZ_ACTIVE] team=%d %d -> %d" % [_active_team, _active_player_idx, i])
				_active_pred_offset = Vector3.ZERO
				# If active player switches near center spot right at kickoff, trigger immediate follow-up drive & pass
				if ball != null and Vector2(ball.position.x, ball.position.z).length() < 3.8 and _game_timer < 6.0:
					var now_sw := Time.get_ticks_msec()
					_kickoff_followup_until_ms = max(_kickoff_followup_until_ms, now_sw + 1600)
					_auto_take_next_ms = min(_auto_take_next_ms, now_sw + 240)
			_active_player_idx = i
			bridge.set_active_player(i, _active_team)
		elif (flags & 4) and p_team != _active_team:
			_opp_active_player_idx = i
		if flags & 8:
			if p_team == _active_team:
				team_has_ball = true
			else:
				opp_has_ball = true
	var sp_team_ui: int = bridge.get_set_piece_team() if bridge.has_method("get_set_piece_team") else -1
	if (_set_piece_type > 0 and (sp_team_ui < 0 or sp_team_ui == _active_team)) or team_has_ball:
		_set_touch_Attack_defense_mode(true)
	elif opp_has_ball:
		_set_touch_Attack_defense_mode(false)
	elif ball != null and _active_player_idx >= 0 and _active_player_idx < players.size():
		var my_d: float = players[_active_player_idx].position.distance_to(ball.position)
		var opp_base: int = (1 - _active_team) * 11
		var min_opp_d := 999.0
		for j in range(11):
			var idx_o := opp_base + j
			if idx_o < players.size():
				min_opp_d = minf(min_opp_d, players[idx_o].position.distance_to(ball.position))
		if my_d < min_opp_d + 1.5:
			_set_touch_Attack_defense_mode(true)
		elif min_opp_d < 3.2 and my_d > min_opp_d + 2.0:
			_set_touch_Attack_defense_mode(false)

	var now_gs_ms := Time.get_ticks_msec()
	var in_goal_celeb: bool = (now_gs_ms < _goal_cam_until_ms and _goal_scorer_idx >= 0)
	var celeb_rem_ms: int = (_goal_cam_until_ms - now_gs_ms) if in_goal_celeb else 0

	# Players: animation (positions AND rotations are smoothly interpolated at 60 FPS in _apply_interpolation)
	for i in range(DZ_MAX_PLAYERS):
		var p := players[i]
		var pflags: int = bridge.get_player_flags(i)
		if pflags > 0:
			p.visible = (pflags & 1) != 0
		var anim_id: int = bridge.get_player_anim(i)
		var clip_id: int = bridge.get_player_clip_id(i)
		var progress: float = bridge.get_player_anim_progress(i) / 65535.0
		var func_type: int = bridge.get_player_function_type(i) if bridge.has_method("get_player_function_type") else 0
		if i < _player_func_types.size():
			_player_func_types[i] = func_type
			_player_anim_progs[i] = progress
		var p_spd: float = _player_vels[i].length() if i < _player_vels.size() else 0.0
		if in_goal_celeb and celeb_rem_ms < 4600:
			var sc_team: int = _goal_scorer_idx / 11
			if i == _goal_scorer_idx and (celeb_rem_ms < 3900 or p_spd < 3.2):
				_play_clip(p, CLIP_CELEB_EXTREME + (i % 2), -1.0, ANIM_CELEBRATE, 14)
			elif (i / 11) == sc_team and p_spd < 2.4:
				_play_clip(p, CLIP_CELEB_HAPPY + (i % 2), -1.0, ANIM_CELEBRATE, 14)
			elif (i / 11) != sc_team and p_spd < 2.0:
				_play_clip(p, CLIP_CELEB_SAD + (i % 2), -1.0, ANIM_IDLE, 14)
			else:
				_play_clip(p, clip_id if clip_id < 0xFF00 else 0, progress, anim_id, func_type)
		elif i == _active_player_idx and _joy_active and _move_dir.length() >= 0.20 and func_type <= 1 and p_spd < 1.35 and gs_in_play:
			# Immediate client-side locomotion anim start when joystick is tilted before server RTT catches up
			_play_clip(p, CLIP_RUN_STRAIGHT + (i % 2), -1.0, ANIM_RUN, 1)
		elif clip_id >= 0xFF00:
			var cat_id: int = clip_id & 0xFF
			if cat_id == ANIM_CELEBRATE:
				_play_clip(p, CLIP_CELEB_EXTREME + (i % 2), -1.0, ANIM_CELEBRATE, 14)
			else:
				_play_anim(p, cat_id, progress)
		else:
			# Exact desktop-selected clip from the GF AnimCollection
			_play_clip(p, clip_id, progress, anim_id, func_type)

	# Officials — exact AnimCollection clip + card-raise animation (000_showcard.anim = 1332/1333)
	for i in range(min(3, officials.size())):
		var o := officials[i]
		var o_anim: int = bridge.get_official_anim(i)
		var o_clip: int = bridge.get_official_clip_id(i) if bridge.has_method("get_official_clip_id") else 0
		var o_prog: float = (bridge.get_official_anim_progress(i) / 65535.0) if bridge.has_method("get_official_anim_progress") else -1.0
		var o_flags: int = bridge.get_official_flags(i) if bridge.has_method("get_official_flags") else 0
		if i == 0 and (now_gs_ms < _card_anim_until_ms or (o_flags & 0x06) != 0):
			var card_clip: int = 1332 + (1 if (o_flags & 0x08) != 0 else 0)
			_play_clip(o, card_clip, o_prog if (o_flags & 0x06) != 0 else -1.0, ANIM_IDLE, 14)
		elif o_clip > 0 and o_clip < 0xFF00:
			var o_ft: int = 14 if (o_flags & 0x10) != 0 else 1
			_play_clip(o, o_clip, o_prog, o_anim, o_ft)
		else:
			_play_anim(o, o_anim)

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
	anim_player.speed_scale = ANIM_SPEED_SCALE
	var anim_name: String = ANIM_NAMES[anim_id]
	if anim_player.has_animation(anim_name):
		if anim_player.current_animation != anim_name:
			anim_player.play(anim_name, 0.06)
			# Seek to the server's current frame so the anim doesn't restart from 0
			if progress >= 0.0:
				var anim_len: float = anim_player.current_animation_length
				if anim_len > 0.0:
					anim_player.seek(progress * anim_len, true)
	elif anim_player.current_animation != "idle" and anim_player.has_animation("idle"):
		anim_player.play("idle", 0.06)

func _play_clip(player: Node3D, clip_id: int, progress: float, fallback_cat: int, func_type: int = 0) -> void:
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
	var is_ball_action: bool = func_type > 1  # 0=None, 1=Movement; >=2 = BallControl/Trap/Pass/Shot/Deflect/Catch/Tackle
	if anim_player.current_animation != clip_key:
		anim_player.speed_scale = ANIM_SPEED_SCALE
		var blend_s: float = 0.025 if is_ball_action else 0.065
		anim_player.play(clip_key, blend_s)
		if progress >= 0.0:
			anim_player.seek(progress * clip_len, false)
	elif progress >= 0.0:
		var cur_norm: float = fmod(anim_player.current_animation_position, clip_len) / clip_len
		var signed_diff: float = progress - cur_norm
		if signed_diff > 0.5:
			signed_diff -= 1.0
		elif signed_diff < -0.5:
			signed_diff += 1.0
		var abs_diff: float = absf(signed_diff)
		if is_ball_action:
			# Ball-action anim (pass, shot, trap, control, tackle, GK save): strict server phase sync
			anim_player.speed_scale = ANIM_SPEED_SCALE
			if abs_diff > 0.18:
				anim_player.seek(progress * clip_len, false)
		else:
			# Locomotion anim (None=0, Movement=1): smooth phase-lock via speed_scale modulation (avoids mid-stride leg snap)
			if abs_diff > 0.42:
				anim_player.seek(progress * clip_len, false)
				anim_player.speed_scale = ANIM_SPEED_SCALE
			elif abs_diff > 0.04:
				anim_player.speed_scale = ANIM_SPEED_SCALE * clampf(1.0 + signed_diff * 1.45, 0.84, 1.16)
			else:
				anim_player.speed_scale = ANIM_SPEED_SCALE

# Official Ligue 1 Mobilis 16 clubs: signature player surname -> [short_code, shirt_color, shorts_color]
const DZ_CLUB_BY_PLAYER := {
	"MEDJADEL": ["ASO", Color8(228, 24, 30), Color8(245, 245, 248)],
	"SADAHINE": ["ASO", Color8(228, 24, 30), Color8(245, 245, 248)],
	"BENCHOUYA": ["ASO", Color8(228, 24, 30), Color8(245, 245, 248)],
	"MOKHTAR": ["CRB", Color8(245, 245, 248), Color8(210, 16, 24)],
	"KEDDAD": ["CRB", Color8(245, 245, 248), Color8(210, 16, 24)],
	"MEZIANE": ["CRB", Color8(245, 245, 248), Color8(210, 16, 24)],
	"BENGUIT": ["CRB", Color8(245, 245, 248), Color8(210, 16, 24)],
	"BOUHALFAYA": ["CSC", Color8(18, 128, 42), Color8(18, 18, 22)],
	"DIB": ["CSC", Color8(18, 128, 42), Color8(18, 18, 22)],
	"GUENDOUZ": ["MCA", Color8(18, 132, 48), Color8(205, 18, 24)],
	"RAMDANE": ["MCA", Color8(18, 132, 48), Color8(205, 18, 24)],
	"MERBAH": ["JSK", Color8(242, 208, 18), Color8(18, 128, 44)],
	"HADID": ["JSK", Color8(242, 208, 18), Color8(18, 128, 44)],
	"ABDELKADER": ["USM", Color8(210, 16, 22), Color8(18, 18, 22)],
	"MOUSSAOUI": ["USM", Color8(210, 16, 22), Color8(18, 18, 22)],
	"SIFOUR": ["ESS", Color8(245, 245, 248), Color8(18, 18, 22)],
	"SOUFI": ["ESS", Color8(245, 245, 248), Color8(18, 18, 22)],
	"MENDIL": ["MCO", Color8(220, 18, 24), Color8(245, 245, 248)],
	"FERRAHI": ["MCO", Color8(220, 18, 24), Color8(245, 245, 248)],
	"BOUAOUNE": ["PAC", Color8(242, 208, 18), Color8(20, 48, 185)],
	"SAIDI": ["PAC", Color8(242, 208, 18), Color8(20, 48, 185)],
	"SEGGARI": ["JSS", Color8(240, 205, 18), Color8(18, 128, 44)],
	"RAHAL": ["MBR", Color8(22, 68, 215), Color8(245, 245, 248)],
	"MORCELY": ["USK", Color8(245, 245, 248), Color8(18, 18, 22)],
	"CHERIF": ["MCE", Color8(22, 82, 195), Color8(245, 245, 248)],
	"AZIRIA": ["OAK", Color8(22, 92, 210), Color8(245, 245, 248)],
	"KADRI": ["ESB", Color8(245, 245, 248), Color8(22, 22, 28)],
	"ALILI": ["ESM", Color8(18, 132, 48), Color8(245, 245, 248)],
}

func _detect_dz_club(start_idx: int) -> Array:
	for i in range(start_idx, start_idx + 11):
		var nm: String = dz_bridge.get_match_player_name(i).to_upper()
		for kw in DZ_CLUB_BY_PLAYER:
			if kw in nm:
				return DZ_CLUB_BY_PLAYER[kw]
	return []

func _is_default_gf_color(c: Color) -> bool:
	# Default GF SQLite DB team 1 = yellow (0.9,0.9,0.15) or team 2 = blue (0.15,0.15,0.7) or uninitialized (0,0,0)
	if c.r < 0.02 and c.g < 0.02 and c.b < 0.02:
		return true
	var d_yel := Vector3(c.r - 0.90, c.g - 0.85, c.b - 0.15).length()
	var d_blu := Vector3(c.r - 0.15, c.g - 0.15, c.b - 0.70).length()
	return d_yel < 0.25 or d_blu < 0.25

func _apply_match_setup() -> void:
	if not dz_bridge or not dz_bridge.has_match_setup():
		return

	_team_a_name = dz_bridge.get_team_a_name()
	_team_b_name = dz_bridge.get_team_b_name()

	var club_a := _detect_dz_club(0)
	var club_b := _detect_dz_club(11)
	if not club_a.is_empty() and (_team_a_name.length() > 12 or "-" in _team_a_name):
		_team_a_name = club_a[0]
	if not club_b.is_empty() and (_team_b_name.length() > 12 or "-" in _team_b_name):
		_team_b_name = club_b[0]

	# Pull kit colors from server or from detected Ligue 1 DZ club
	if dz_bridge.has_method("get_team_a_color"):
		var ca: Color = dz_bridge.get_team_a_color(true)
		var ca2: Color = dz_bridge.get_team_a_color(false)
		var cb: Color = dz_bridge.get_team_b_color(true)
		var cb2: Color = dz_bridge.get_team_b_color(false)
		if not _is_default_gf_color(ca):
			_team_a_shirt = ca
			_team_a_shorts = ca2
		elif not club_a.is_empty():
			_team_a_shirt = club_a[1]
			_team_a_shorts = club_a[2]
		if not _is_default_gf_color(cb):
			_team_b_shirt = cb
			_team_b_shorts = cb2
		elif not club_b.is_empty():
			_team_b_shirt = club_b[1]
			_team_b_shorts = club_b[2]
		# Prevent kit clash between Team A and Team B
		if Vector3(_team_a_shirt.r - _team_b_shirt.r, _team_a_shirt.g - _team_b_shirt.g, _team_a_shirt.b - _team_b_shirt.b).length() < 0.32:
			var tmp := _team_b_shirt
			_team_b_shirt = _team_b_shorts
			_team_b_shorts = tmp

	var sig := "%s|%s|%s|%s|%s|%s" % [
		_team_a_name, _team_b_name,
		_team_a_shirt.to_html(false), _team_b_shirt.to_html(false),
		dz_bridge.get_match_player_name(0), dz_bridge.get_match_player_name(11)
	]
	if sig == _applied_setup_sig and _appearance_done:
		return
	print("[DZFoot] Match setup: %s (%s/%s) vs %s (%s/%s)" % [_team_a_name, _team_a_shirt, _team_a_shorts, _team_b_name, _team_b_shirt, _team_b_shorts])
	_applied_setup_sig = sig
	_appearance_done = true

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
		_net_prev_positions[i] = players[i].position
		_player_vels[i] = Vector3.ZERO
		_prev_rot_y[i] = players[i].rotation.y
		_curr_rot_y[i] = players[i].rotation.y
		_play_anim(players[i], ANIM_IDLE)

	# Ball at center
	ball.position = Vector3.ZERO
	_prev_ball_pos = Vector3.ZERO
	_curr_ball_pos = Vector3.ZERO
	_net_prev_ball_pos = Vector3.ZERO
	_ball_vel = Vector3.ZERO

	# Officials around the pitch
	if officials.size() >= 3:
		officials[0].position = Vector3(0, 0, -20)
		officials[1].position = Vector3(0, 0, 35)
		officials[2].position = Vector3(0, 0, -35)
		for oi in range(3):
			_prev_official_pos[oi] = officials[oi].position
			_curr_official_pos[oi] = officials[oi].position
			_prev_official_rot[oi] = officials[oi].rotation.y
			_curr_official_rot[oi] = officials[oi].rotation.y

	# Pick a human-controlled player on this device's assigned team (Team A=0 or Team B=1)
	if dz_plugin and dz_plugin.has_method("get_my_team"):
		_active_team = clampi(int(dz_plugin.get_my_team()), 0, 1)
	else:
		_active_team = 0
	_active_player_idx = _active_team * 11 + 9
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
		# Camera mode toggle (strict 40px radius so nearby taps never flip CAM -> ACT)
		if _cam_btn != null:
			var cam_pos := _cam_btn.global_position + Vector2(40, 40)
			if pos.distance_to(cam_pos) < 42:
				_cam_mode = (_cam_mode + 1) % 2
				if _cam_btn_label:
					_cam_btn_label.text = "CAM" if _cam_mode == 0 else "ACT"
				print("[DZ_CAM] mode=%d" % _cam_mode)
				return
		# Check joystick
		var joy_screen_pos := _joy_base.global_position + Vector2(_joy_radius, _joy_radius)
		if pos.distance_to(joy_screen_pos) < _joy_radius * 1.55 and _joy_pointer == -1:
			_joy_pointer = event.index
			_joy_active = true
			_joy_center = joy_screen_pos
			_last_touch_ms = Time.get_ticks_msec()
			_auto_dir_ms = 0
			_kickoff_followup_until_ms = 0
			_update_joystick(pos)
			if _display_ok:
				_update_and_send_input()
			return

		# Check action buttons
		for btn_name in _action_buttons:
			var btn_info: Dictionary = _action_buttons[btn_name]
			var btn_ctrl: Control = btn_info.control
			var r_btn: float = float(btn_info.get("radius", BTN_RADIUS))
			var btn_pos := btn_ctrl.global_position + Vector2(r_btn, r_btn)
			if pos.distance_to(btn_pos) < r_btn + 16.0 and btn_info.pointer == -1:
				btn_info.pointer = event.index
				btn_info.pressed = true
				btn_info["touch_start"] = pos
				btn_info["active_mask"] = btn_info.mask
				_buttons |= btn_info.mask
				_last_touch_ms = Time.get_ticks_msec()
				_auto_dir_ms = 0
				if _is_attacking_mode and (btn_info.mask & (BUTTON_PASS | BUTTON_KICK | BUTTON_SHOT | BUTTON_HIGH_PASS)):
					var now_t := Time.get_ticks_msec()
					_power_gauge_start_ms = now_t
					_power_gauge_charging = true
					_power_gauge_value = 0.08
				_btn_latch[btn_info.mask] = Time.get_ticks_msec() + 55
				_button_pointers[event.index] = btn_name
				_update_button_visual(btn_name, true)
				if _display_ok:
					_update_and_send_input()
				return
	else:
		# Release joystick
		if event.index == _joy_pointer:
			_joy_pointer = -1
			_joy_active = false
			_joy_dir = Vector2.ZERO
			_move_dir = Vector2.ZERO
			_joy_world_vec = Vector2.ZERO
			_reset_joystick_visual()
			if _display_ok:
				_update_and_send_input()
			return

		# Release button
		if _button_pointers.has(event.index):
			var btn_name: String = _button_pointers[event.index]
			var btn_info: Dictionary = _action_buttons[btn_name]
			btn_info.pressed = false
			btn_info.pointer = -1
			var rel_mask: int = int(btn_info.get("active_mask", btn_info.mask))
			_buttons &= ~rel_mask
			if (_buttons & (BUTTON_PASS | BUTTON_KICK | BUTTON_SHOT | BUTTON_HIGH_PASS)) == 0 and _power_gauge_charging:
				var now_rel := Time.get_ticks_msec()
				_power_gauge_value = clampf(float(now_rel - _power_gauge_start_ms) / 500.0, 0.12, 1.0)
				_power_gauge_charging = false
				_power_gauge_hold_until_ms = now_rel + 280
			_button_pointers.erase(event.index)
			_update_button_visual(btn_name, false)
			if _display_ok:
				_update_and_send_input()
			return

func _handle_screen_drag(event: InputEventScreenDrag) -> void:
	if event.index == _joy_pointer and _joy_active:
		_last_touch_ms = Time.get_ticks_msec()
		_auto_dir_ms = 0
		_kickoff_followup_until_ms = 0
		_update_joystick(event.position)
		if _display_ok:
			_update_and_send_input()
		return
	if _button_pointers.has(event.index) and _is_attacking_mode:
		var bname: String = _button_pointers[event.index]
		if bname == "btn_low" or bname == "btn_mid":
			var binfo: Dictionary = _action_buttons[bname]
			var t0: Vector2 = binfo.get("touch_start", event.position)
			if event.position.y - t0.y < -36.0 and int(binfo.get("active_mask", 0)) != BUTTON_HIGH_PASS:
				var old_m: int = int(binfo.get("active_mask", binfo.mask))
				_buttons &= ~old_m
				binfo["active_mask"] = BUTTON_HIGH_PASS
				_buttons |= BUTTON_HIGH_PASS
				_btn_latch[BUTTON_HIGH_PASS] = Time.get_ticks_msec() + 80

func _update_joystick(pos: Vector2) -> void:
	var delta := pos - _joy_center
	var len := delta.length()
	if len > _joy_radius:
		delta = delta.normalized() * _joy_radius
		len = _joy_radius
	_joy_dir = delta / _joy_radius
	if _joy_dir.length() < JOY_DEADZONE:
		_joy_dir = Vector2.ZERO
	var knob_r := _joy_radius * 0.38
	if _joy_knob:
		_joy_knob.position = Vector2(_joy_radius, _joy_radius) + delta * 0.72 - Vector2(knob_r, knob_r)
	if _joy_dir_dot:
		if _joy_dir.length() >= JOY_DEADZONE:
			var rim_pos := Vector2(_joy_radius, _joy_radius) + _joy_dir.normalized() * (_joy_radius - 6.0) - Vector2(9.0, 9.0)
			_joy_dir_dot.position = rim_pos
			_joy_dir_dot.visible = true
		else:
			_joy_dir_dot.visible = false

	# Map to game direction: screen X = world X, screen Y down = world Z forward
	_move_dir.x = _joy_dir.x
	_move_dir.y = -_joy_dir.y  # invert Y: up on stick = forward
	if _move_dir.length() >= 0.20 and _active_player_idx >= 0 and _active_player_idx < players.size():
		var ft_act: int = _player_func_types[_active_player_idx] if _active_player_idx < _player_func_types.size() else 0
		var spd_act: float = _player_vels[_active_player_idx].length() if _active_player_idx < _player_vels.size() else 0.0
		if ft_act <= 1 and spd_act < 1.35:
			_play_clip(players[_active_player_idx], CLIP_RUN_STRAIGHT + (_active_player_idx % 2), -1.0, ANIM_RUN, 1)

func _reset_joystick_visual() -> void:
	var knob_r := _joy_radius * 0.38
	if _joy_knob:
		_joy_knob.position = Vector2(_joy_radius - knob_r, _joy_radius - knob_r)
	if _joy_dir_dot:
		_joy_dir_dot.visible = false

func _update_button_visual(btn_name: String, pressed: bool) -> void:
	var btn_info: Dictionary = _action_buttons[btn_name]
	var ctrl: Panel = btn_info.control as Panel
	var col: Color = btn_info.get("color", Color.WHITE)
	var st := ctrl.get_theme_stylebox("panel") as StyleBoxFlat
	if pressed:
		ctrl.scale = Vector2(1.10, 1.10)
		if st:
			st.bg_color = Color(col.r * 0.45, col.g * 0.45, col.b * 0.45, 0.92 if not _software_gl else 1.0)
			st.border_color = Color.WHITE
	else:
		ctrl.scale = Vector2(1.0, 1.0)
		if st:
			st.bg_color = Color(0.05, 0.08, 0.13, 0.78 if not _software_gl else 1.0)
			st.border_color = Color(col.r, col.g, col.b, 0.92)
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
		if _move_dir.length() >= JOY_DEADZONE:
			_joy_world_vec = Vector2(world_x, world_z)
			_world_aim_dir = _joy_world_vec.normalized()
		else:
			_joy_world_vec = Vector2.ZERO
		# Godot (x,z) -> GF env direction (x, -y): Godot z = -GF y
		dz_bridge.set_input_direction(world_x, -world_z)
	else:
		if _move_dir.length() >= JOY_DEADZONE:
			_joy_world_vec = Vector2(_move_dir.x, _move_dir.y)
			_world_aim_dir = _joy_world_vec.normalized()
		else:
			_joy_world_vec = Vector2.ZERO
		dz_bridge.set_input_direction(_move_dir.x, -_move_dir.y)

	var eff_buttons: int = _buttons
	var now_ms: int = Time.get_ticks_msec()
	if _match_over:
		eff_buttons = 0
		dz_bridge.set_input_direction(0.0, 0.0)
	# Track how long the ball has been stationary (catches throw-ins, offside free kicks,
	# AND human-selected player standing still with the ball when user isn't touching).
	if ball != null:
		var bxz := Vector3(ball.position.x, 0.0, ball.position.z)
		if bxz.distance_to(_ball_still_ref) > 0.25:
			_ball_still_ref = bxz
			_ball_still_since_ms = now_ms
	var sp_team: int = dz_bridge.get_set_piece_team() if (dz_bridge and dz_bridge.has_method("get_set_piece_team")) else -1
	var my_set_piece: bool = _set_piece_type > 0 and (sp_team < 0 or sp_team == _active_team)
	var in_kickoff_followup: bool = (now_ms < _kickoff_followup_until_ms) and _move_dir.length() < JOY_DEADZONE and _buttons == 0 and ball != null and Vector2(ball.position.x, ball.position.z).length() < 4.2
	var ball_stalled: bool = my_set_piece and _ball_still_since_ms > 0 and (now_ms - _ball_still_since_ms) > 1500
	var auto_wait_ms: int = 350 if _set_piece_type == 1 else 1600
	var need_auto: bool = (my_set_piece and now_ms - _set_piece_since_ms > auto_wait_ms) or ball_stalled or in_kickoff_followup
	var touch_idle_ok: bool = ( not _joy_active and _move_dir.length() < JOY_DEADZONE and (now_ms - _last_touch_ms > 900)) or in_kickoff_followup
	if not _match_over and need_auto and touch_idle_ok and now_ms >= _auto_take_next_ms:
		_auto_take_next_ms = now_ms + (380 if in_kickoff_followup else 850)
		_btn_latch[BUTTON_PASS] = now_ms + 150
		_auto_dir_x = 1.0 if _active_team == 0 else -1.0
		# Aim toward the vertical center of the pitch (essential for throw-ins at z=+-36 and wide free kicks)
		var bz: float = ball.position.z if ball != null else 0.0
		_auto_dir_z = clampf(-bz / 25.0, -0.85, 0.85) if not in_kickoff_followup else 0.20
		_auto_dir_ms = now_ms + 450
		print("[DZ_AUTO] auto-taking sp=%d kickoff_fu=%s stalled=%s dir=(%.2f,%.2f)" % [_set_piece_type, str(in_kickoff_followup), str(ball_stalled), _auto_dir_x, _auto_dir_z])
	for m in _btn_latch.keys():
		if now_ms < int(_btn_latch[m]):
			eff_buttons |= int(m)
		else:
			_btn_latch.erase(m)
	# Hold the attack direction for the whole press+release window or right after kickoff pass (ONLY when user is not tilting joystick)
	if not _joy_active and _move_dir.length() < JOY_DEADZONE and (now_ms < _auto_dir_ms or now_ms < _kickoff_followup_until_ms):
		var kx: float = 1.0 if _active_team == 0 else -1.0
		dz_bridge.set_input_direction(_auto_dir_x if now_ms < _auto_dir_ms else kx, _auto_dir_z if now_ms < _auto_dir_ms else 0.15)
	elif _is_attacking_mode and (eff_buttons & BUTTON_SHOT) != 0 and _move_dir.length() < JOY_DEADZONE and _active_player_idx >= 0 and _active_player_idx < players.size():
		# Assisted Goal Auto-Aim when pressing TIR without tilting the joystick (symmetric for Team 0 and Team 1)
		var sp_p: Vector3 = players[_active_player_idx].position
		var goal_x: float = 55.0 if _active_team == 0 else -55.0
		var goal_z: float = clampf(-sp_p.z * 0.18, -2.4, 2.4)
		var g_dir := Vector2(goal_x - sp_p.x, goal_z - sp_p.z).normalized()
		_world_aim_dir = g_dir
		dz_bridge.set_input_direction(g_dir.x, -g_dir.y)
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
	var bw: float = [0.92, 1.0, 1.08, 1.17][clampi(dz_bridge.get_match_player_body_type(idx), 0, 3)]
	p.scale = Vector3(sc * bw, sc, sc * bw)
	var skin_idx: int = clampi(dz_bridge.get_match_player_skin(idx), 0, 6)
	var hair_style: int = dz_bridge.get_match_player_hair_style(idx)
	var hair_col: int = clampi(dz_bridge.get_match_player_hair_color(idx), 0, _hair_tints.size() - 1)
	var beard_style: int = clampi(dz_bridge.get_match_player_beard(idx), 0, 3)
	var skin_tex: Texture2D = _skin_textures[skin_idx] if skin_idx < _skin_textures.size() else null
	_apply_skin_texture(p, skin_tex, skin_idx, beard_style, hair_col)
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
		lb.font_size = 58
		lb.pixel_size = 0.0038
		lb.outline_size = 6
		lb.outline_modulate = Color(0.06, 0.06, 0.08, 0.90)
		lb.modulate = Color(0.98, 0.98, 0.98, 0.96)
		lb.shaded = false
		lb.double_sided = true
		lb.rotation.y = PI
		lb.position = Vector3(0, 0.28, -0.132)
		torso.add_child(lb)
	lb.text = txt

const SKIN_TONES := [
	Color8(226, 184, 158), Color8(212, 166, 134), Color8(196, 152, 118), Color8(178, 136, 102),
	Color8(152, 112, 82), Color8(122, 86, 60), Color8(92, 64, 44)]
const FACE_BASE_TONE := Color8(209, 152, 119)
var _face_mat_cache: Dictionary = {}
var _base_face_img: Image = null

func _is_face_surface(mi: MeshInstance3D, s: int) -> bool:
	if mi.mesh == null:
		return false
	var m := mi.mesh.surface_get_material(s)
	return m != null and m.resource_name == "face"

func _make_face_material(mi: MeshInstance3D, s: int, skin_idx: int, beard_style: int = 0, hair_col: int = 0) -> StandardMaterial3D:
	skin_idx = clampi(skin_idx, 0, SKIN_TONES.size() - 1)
	beard_style = clampi(beard_style, 0, 3)
	hair_col = clampi(hair_col, 0, _hair_tints.size() - 1)
	var key := "%d_%d_%d" % [skin_idx, beard_style, hair_col]
	if _face_mat_cache.has(key):
		return _face_mat_cache[key]
	if _base_face_img == null:
		if ResourceLoader.exists("res://textures/face_hd.png"):
			var hd_tex := load("res://textures/face_hd.png") as Texture2D
			if hd_tex != null:
				_base_face_img = hd_tex.get_image()
		if _base_face_img == null:
			var src := mi.mesh.surface_get_material(s)
			if src is BaseMaterial3D and (src as BaseMaterial3D).albedo_texture != null:
				_base_face_img = (src as BaseMaterial3D).albedo_texture.get_image()
		if _base_face_img != null:
			if _base_face_img.is_compressed():
				_base_face_img.decompress()
			_base_face_img.convert(Image.FORMAT_RGBA8)
	var mat := StandardMaterial3D.new()
	var tone: Color = SKIN_TONES[skin_idx]
	if _base_face_img != null:
		var img := _base_face_img.duplicate() as Image
		var w := img.get_width()
		var h := img.get_height()
		var sc_uv := float(w) / 256.0
		var bcol: Color = _hair_tints[hair_col].darkened(0.15)
		for y in range(h):
			var yf := float(y) / sc_uv
			for x in range(w):
				var xf := float(x) / sc_uv
				var c := img.get_pixel(x, y)
				var diff_skin := absf(c.r - FACE_BASE_TONE.r) + absf(c.g - FACE_BASE_TONE.g) + absf(c.b - FACE_BASE_TONE.b)
				var is_eye := (yf >= 80.0 and yf <= 130.0 and xf >= 52.0 and xf <= 204.0 and diff_skin > 0.18)
				if not is_eye:
					c.r = clampf(c.r * (tone.r / FACE_BASE_TONE.r), 0.0, 1.0)
					c.g = clampf(c.g * (tone.g / FACE_BASE_TONE.g), 0.0, 1.0)
					c.b = clampf(c.b * (tone.b / FACE_BASE_TONE.b), 0.0, 1.0)
				if beard_style > 0:
					var dx := absf(xf - 128.0)
					var mask := 0.0
					if yf >= 166.0 and yf <= 178.0 and dx <= 28.0:
						var my := 1.0 - absf(yf - 172.0) / 7.0
						var mx := clampf((28.0 - dx) / 8.0, 0.0, 1.0)
						mask = maxf(mask, my * mx * 0.85)
					if yf >= 193.0 and yf <= 203.0 and dx <= 8.0:
						mask = maxf(mask, (1.0 - dx / 8.5) * 0.80)
					if yf >= 196.0 and yf <= 228.0 and dx <= 34.0:
						var cy := clampf(1.0 - absf(yf - 210.0) / 18.0, 0.0, 1.0)
						var cx := clampf((34.0 - dx) / 10.0, 0.0, 1.0)
						mask = maxf(mask, cy * cx * 0.92)
					var jaw_w := 74.0 if beard_style != 2 else 54.0
					var jaw_top := 128.0 if beard_style == 3 else (142.0 if beard_style == 1 else 176.0)
					if yf >= jaw_top and yf <= 226.0 and dx >= 24.0 and dx <= jaw_w:
						var t_y := clampf((yf - jaw_top) / (220.0 - jaw_top), 0.0, 1.0)
						var inner_x := lerpf(52.0, 20.0, t_y)
						var outer_x := lerpf(74.0, 42.0, t_y)
						if dx >= inner_x and dx <= outer_x:
							var jm := clampf(1.0 - absf(dx - (inner_x + outer_x) * 0.5) / ((outer_x - inner_x) * 0.6), 0.0, 1.0)
							mask = maxf(mask, jm * (0.45 if beard_style == 1 else 0.88))
					if mask > 0.01:
						var grain := 0.85 + 0.30 * fposmod(sin(float(x * 127 + y * 311)) * 43758.5, 1.0)
						var strength := (0.42 if beard_style == 1 else (0.78 if beard_style == 2 else 0.92)) * clampf(mask * grain, 0.0, 1.0)
						c = c.lerp(bcol, strength)
				img.set_pixel(x, y, c)
		img.generate_mipmaps()
		mat.albedo_texture = ImageTexture.create_from_image(img)
		mat.albedo_color = Color.WHITE
	else:
		mat.albedo_color = tone
	mat.roughness = 0.46
	mat.specular = 0.40
	mat.rim_enabled = true
	mat.rim = 0.54
	mat.rim_tint = 0.24
	mat.emission_enabled = true
	mat.emission = tone.lerp(Color(0.95, 0.45, 0.32), 0.35)
	mat.emission_energy_multiplier = 0.06
	_face_mat_cache[key] = mat
	return mat

func _apply_skin_texture(node: Node, tex: Texture2D, skin_idx: int = 1, beard_style: int = 0, hair_col: int = 0) -> void:
	var shared_skin := _get_skin_material(skin_idx, tex)
	for child in node.get_children():
		if child is MeshInstance3D:
			var mi := child as MeshInstance3D
			for s in mi.mesh.get_surface_count():
				var kind := _surface_tex_kind(mi, s)
				if kind == 3:
					mi.set_surface_override_material(s, _make_face_material(mi, s, skin_idx, beard_style, hair_col))
				elif kind == 0:
					mi.set_surface_override_material(s, shared_skin)
		_apply_skin_texture(child, tex, skin_idx, beard_style, hair_col)

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

func _attach_head_accessory(player: Node3D, hair_style: int, hair_col: int, _beard_style: int) -> void:
	var existing := player.find_child("HeadDecorMarker", true, false)
	if existing != null:
		existing.free()
	hair_col = clampi(hair_col, 0, _hair_tints.size() - 1)
	var att := _find_head_bone_attachment(player)
	if att == null:
		push_warning("[DZ_APPEAR] no head attachment found for %s" % player.name)
		return
	var marker := Node3D.new()
	marker.name = "HeadDecorMarker"
	if hair_style >= 0 and hair_style <= 4 and hair_style < _hair_templates.size() and _hair_templates[hair_style] != null:
		var h: Node3D = (_hair_templates[hair_style] as Node3D).duplicate()
		var hmat := StandardMaterial3D.new()
		var htex: Texture2D = load("res://textures/hair_%d.png" % hair_col)
		if htex:
			hmat.albedo_texture = htex
			# Tint darkblonde/brown texture slightly with _hair_tints for rich variety
			hmat.albedo_color = _hair_tints[hair_col].lerp(Color.WHITE, 0.55)
		else:
			hmat.albedo_color = _hair_tints[hair_col]
		hmat.roughness = 0.88
		hmat.cull_mode = BaseMaterial3D.CULL_DISABLED
		_tint_meshes(h, hmat)
		# Native GF hairstyles (short01..long01.ase) are already in neck-space;
		# slight scale (1.08, 1.03, 1.06) matches player_base.glb head width.
		h.scale = Vector3(1.08, 1.03, 1.06)
		h.position = Vector3(0.0, -0.012, 0.006)
		marker.add_child(h)
	att.add_child(marker)

func _tint_meshes(node: Node, mat: Material) -> void:
	if node is MeshInstance3D and (node as MeshInstance3D).mesh:
		var root_mi := node as MeshInstance3D
		root_mi.material_override = mat
		for rs in root_mi.mesh.get_surface_count():
			root_mi.set_surface_override_material(rs, mat)
	for child in node.get_children():
		if child is MeshInstance3D:
			var mi := child as MeshInstance3D
			mi.material_override = mat
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
		1:
			_trigger_referee_card_anim(pidx, false)
			_show_event_text("🟨 CARTON JAUNE  " + pname, Color(1.0, 0.90, 0.12))
		2:
			_trigger_referee_card_anim(pidx, true)
			_show_event_text("🟥 CARTON ROUGE  " + pname, Color(1.0, 0.22, 0.22))
		4: _show_event_text("CORNER  -  PASSE pour tirer")
		5: _show_event_text("TOUCHE  -  PASSE pour lancer")
		6: _show_event_text("COUP FRANC  -  PASSE / TIR")
		7: _show_event_text("PENALTY !  -  TIR", Color(1.0, 0.85, 0.25))
		8: _show_event_text("COUP D'ENVOI  -  appuie sur PASSE")
		9:
			_match_over = true
			_show_match_over_banner()
		10: _show_event_text("MI-TEMPS")
		11: _show_event_text("DEGAGEMENT  -  PASSE / LONG")
		12: _show_event_text("HORS-JEU  " + pname, Color(0.98, 0.86, 0.45))
		13: _show_event_text("FAUTE  " + pname, Color(1.0, 0.82, 0.35))

func _trigger_referee_card_anim(pidx: int, is_red: bool) -> void:
	_card_player_idx = pidx
	_card_is_red = is_red
	_showcard_clip_id = CLIP_SHOWCARD
	_card_anim_until_ms = Time.get_ticks_msec() + (4200 if is_red else 3600)
	if officials.size() > 0 and officials[0] != null:
		_play_clip(officials[0], _showcard_clip_id, 0.0, ANIM_IDLE, 14)

func _show_match_over_banner() -> void:
	if not _event_label:
		return
	_event_label.add_theme_color_override("font_color", Color.WHITE)
	_event_label.text = "FIN DU MATCH  %d-%d" % [_score_a, _score_b]
	_event_label.modulate.a = 1.0
	if _event_tween and _event_tween.is_valid():
		_event_tween.kill()

func _show_event_text(txt: String, col: Color = Color.WHITE) -> void:
	if not _event_label:
		return
	_event_label.add_theme_color_override("font_color", col)
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
	_goal_cam_until_ms = Time.get_ticks_msec() + 5800
	if ball != null:
		var gside := 1.0 if ball.position.x >= 0.0 else -1.0
		_goal_net_impact_pos = Vector3(56.4 * gside, clampf(ball.position.y, 0.6, 2.1), clampf(ball.position.z, -3.2, 3.2))
		_goal_net_impact_strength = 1.35
		_goal_net_impact_time = 0.0

func _setup_active_marker() -> void:
	var ring_mesh := TorusMesh.new()
	ring_mesh.inner_radius = 0.62
	ring_mesh.outer_radius = 0.74
	ring_mesh.rings = 32
	ring_mesh.ring_segments = 4
	_active_ring_mat = StandardMaterial3D.new()
	_active_ring_mat.albedo_color = Color(0.20, 0.94, 0.55, 0.68)
	_active_ring_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_active_ring_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	ring_mesh.material = _active_ring_mat
	_active_ring = MeshInstance3D.new()
	_active_ring.mesh = ring_mesh
	_active_ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_active_ring.visible = false
	add_child(_active_ring)

	# Opponent active player ring (1v1 PvP / AI indicator)
	var opp_ring_mesh := TorusMesh.new()
	opp_ring_mesh.inner_radius = 0.58
	opp_ring_mesh.outer_radius = 0.68
	opp_ring_mesh.rings = 28
	opp_ring_mesh.ring_segments = 4
	_opp_active_ring_mat = StandardMaterial3D.new()
	_opp_active_ring_mat.albedo_color = Color(0.98, 0.34, 0.22, 0.50)
	_opp_active_ring_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_opp_active_ring_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	opp_ring_mesh.material = _opp_active_ring_mat
	_opp_active_ring = MeshInstance3D.new()
	_opp_active_ring.mesh = opp_ring_mesh
	_opp_active_ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_opp_active_ring.visible = false
	add_child(_opp_active_ring)

	# Visual Direction Pointer on the turf (FIFA / eFootball style directional arrow)
	var arrow_mesh := PrismMesh.new()
	arrow_mesh.size = Vector3(0.46, 0.58, 0.04)
	_dir_arrow_mat = StandardMaterial3D.new()
	_dir_arrow_mat.albedo_color = Color(0.25, 0.96, 0.60, 0.85)
	_dir_arrow_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_dir_arrow_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	arrow_mesh.material = _dir_arrow_mat
	_dir_arrow = MeshInstance3D.new()
	_dir_arrow.mesh = arrow_mesh
	_dir_arrow.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_dir_arrow.rotation.x = PI / 2.0
	_dir_arrow.visible = false
	add_child(_dir_arrow)

	_active_label = Label3D.new()
	_active_label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_active_label.font_size = 54
	_active_label.pixel_size = 0.0050
	_active_label.modulate = Color(0.84, 0.88, 0.96, 0.88)
	_active_label.outline_size = 4
	_active_label.outline_modulate = Color(0.08, 0.10, 0.08, 0.65)
	_active_label.no_depth_test = true
	_active_label.visible = false
	add_child(_active_label)

	if _ui_layer != null:
		_power_gauge_bg = Panel.new()
		_power_gauge_bg.name = "PowerGaugeBG"
		var pg_style := StyleBoxFlat.new()
		pg_style.bg_color = Color(0.06, 0.08, 0.10, 0.88)
		pg_style.border_color = Color(0.92, 0.95, 0.98, 0.90)
		pg_style.set_border_width_all(2)
		pg_style.set_corner_radius_all(3)
		_power_gauge_bg.add_theme_stylebox_override("panel", pg_style)
		_power_gauge_bg.size = Vector2(88, 12)
		_power_gauge_bg.visible = false
		_power_gauge_bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_ui_layer.add_child(_power_gauge_bg)

		_power_gauge_fill = ColorRect.new()
		_power_gauge_fill.name = "PowerGaugeFill"
		_power_gauge_fill.position = Vector2(2, 2)
		_power_gauge_fill.size = Vector2(0, 8)
		_power_gauge_fill.color = Color(0.20, 0.92, 0.32, 1.0)
		_power_gauge_fill.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_power_gauge_bg.add_child(_power_gauge_fill)

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
	var now_pg := Time.get_ticks_msec()
	if _power_gauge_charging:
		_power_gauge_value = clampf(float(now_pg - _power_gauge_start_ms) / 500.0, 0.08, 1.0)
	var show_pg: bool = _power_gauge_charging or (now_pg < _power_gauge_hold_until_ms)
	var gauge_col := Color(0.20, 0.94, 0.55) if _is_attacking_mode else Color(0.15, 0.82, 0.96)
	if show_pg:
		if _power_gauge_value < 0.45:
			gauge_col = Color(0.18, 0.92, 0.32).lerp(Color(0.98, 0.86, 0.14), _power_gauge_value / 0.45)
		else:
			gauge_col = Color(0.98, 0.86, 0.14).lerp(Color(0.98, 0.18, 0.14), (_power_gauge_value - 0.45) / 0.55)
	if _active_ring:
		_active_ring.visible = true
		_active_ring.position = Vector3(pos.x, _pitch_top_y + 0.02, pos.z)
		if _active_ring_mat:
			_active_ring_mat.albedo_color = Color(gauge_col.r, gauge_col.g, gauge_col.b, 0.82 if show_pg else 0.65)
	if _opp_active_ring:
		if _opp_active_player_idx >= 0 and _opp_active_player_idx < players.size() and players[_opp_active_player_idx].visible:
			var opos_p: Vector3 = players[_opp_active_player_idx].position
			_opp_active_ring.visible = true
			_opp_active_ring.position = Vector3(opos_p.x, _pitch_top_y + 0.018, opos_p.z)
		else:
			_opp_active_ring.visible = false
	if _dir_arrow:
		var aim_dir := _world_aim_dir
		if _move_dir.length() < JOY_DEADZONE:
			var ry: float = p.rotation.y
			aim_dir = Vector2(-sin(ry), -cos(ry)).normalized()
		var arrow_dist: float = 0.98 + (_power_gauge_value * 0.45 if show_pg else 0.0)
		_dir_arrow.visible = true
		_dir_arrow.position = Vector3(pos.x + aim_dir.x * arrow_dist, _pitch_top_y + 0.028, pos.z + aim_dir.y * arrow_dist)
		_dir_arrow.rotation = Vector3(PI / 2.0, atan2(aim_dir.x, aim_dir.y), 0.0)
		_dir_arrow.scale = Vector3(1.0, 1.0 + (_power_gauge_value * 0.75 if show_pg else 0.0), 1.0)
		if _dir_arrow_mat:
			_dir_arrow_mat.albedo_color = Color(gauge_col.r, gauge_col.g, gauge_col.b, 0.92 if (_move_dir.length() >= JOY_DEADZONE or show_pg) else 0.52)
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
	if _power_gauge_bg and _power_gauge_fill and camera:
		if show_pg and not camera.is_position_behind(pos):
			var hs_pg := 1.0
			if _active_player_idx < _player_heights.size() and _player_heights[_active_player_idx] > 0.0:
				hs_pg = _player_heights[_active_player_idx]
			var sp2d: Vector2 = camera.unproject_position(pos + Vector3(0.0, 2.65 * hs_pg, 0.0))
			_power_gauge_bg.position = sp2d - Vector2(44.0, 14.0)
			_power_gauge_fill.size.x = roundf(84.0 * _power_gauge_value)
			_power_gauge_fill.color = gauge_col
			_power_gauge_bg.visible = true
		else:
			_power_gauge_bg.visible = false

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
	sm.size = Vector2(0.48, 0.48)
	_ball_shadow_mat = _make_blob_material(0.68)
	sm.material = _ball_shadow_mat
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
	var frame_disp := ball.position - _prev_ball_render_pos
	_prev_ball_render_pos = ball.position
	var move_vec := frame_disp if frame_disp.length() > 0.002 else (vel_godot * delta)
	var move_dist := move_vec.length()
	if move_dist > 0.001 and move_dist < 5.0:
		var flat_dir := Vector3(move_vec.x, 0.0, move_vec.z)
		if flat_dir.length() > 0.0005:
			var axis := Vector3.UP.cross(flat_dir.normalized())
			if axis.length() > 0.01:
				ball.rotate(axis.normalized(), move_dist / 0.14)
	if _ball_shadow:
		var h_air: float = maxf(0.0, ball.position.y - (_pitch_top_y + 0.13))
		# Contact AO shadow directly underneath the ball, drifting slightly along sunlight vector when airborne
		_ball_shadow.position = Vector3(ball.position.x - h_air * 0.24, _pitch_top_y + 0.016, ball.position.z + h_air * 0.12)
		var k: float = clampf(1.0 + h_air * 0.16, 0.85, 1.55)
		_ball_shadow.scale = Vector3(k, 1.0, k)
		if _ball_shadow_mat:
			var a_sh: float = clampf(0.68 * exp(-h_air * 0.55), 0.12, 0.68)
			_ball_shadow_mat.albedo_color = Color(0.0, 0.0, 0.0, a_sh)
	# Cycle LED adboard text every 4.5s
	_ad_board_timer += delta
	if _ad_board_timer >= 4.5:
		_ad_board_timer = 0.0
		_ad_board_cycle += 1
		for bi in range(_ad_board_labels.size()):
			var lb: Label3D = _ad_board_labels[bi]
			if is_instance_valid(lb):
				lb.text = BOARD_TEXTS[(bi + _ad_board_cycle) % BOARD_TEXTS.size()]
	if speed > 16.0:
		_trail_points.push_front(ball.position)
		if _trail_points.size() > 8:
			_trail_points.pop_back()
	else:
		if not _trail_points.is_empty():
			_trail_points.pop_back()
	_draw_ball_trail()
	if _goal_net_mat != null:
		_goal_net_mat.set_shader_parameter("ball_pos", ball.position)
		if _goal_net_impact_strength > 0.005:
			_goal_net_impact_time += delta
			_goal_net_impact_strength = maxf(0.0, _goal_net_impact_strength - delta * 0.72)
			_goal_net_mat.set_shader_parameter("impact_pos", _goal_net_impact_pos)
			_goal_net_mat.set_shader_parameter("impact_strength", _goal_net_impact_strength)
			_goal_net_mat.set_shader_parameter("impact_time", _goal_net_impact_time)
		else:
			_goal_net_mat.set_shader_parameter("impact_strength", 0.0)

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
				img.set_pixel(x, y, Color(0.96, 0.97, 0.98, 1.0))
	var net_tex := ImageTexture.create_from_image(img)
	var net_mat: Material
	if _software_gl:
		var sm := StandardMaterial3D.new()
		sm.albedo_texture = net_tex
		sm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		sm.alpha_scissor_threshold = 0.5
		sm.uv1_scale = Vector3(6, 6, 6)
		sm.cull_mode = BaseMaterial3D.CULL_DISABLED
		sm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		net_mat = sm
	else:
		var nsh := Shader.new()
		nsh.code = """
shader_type spatial;
render_mode unshaded, cull_disabled;
uniform sampler2D net_tex : source_color, filter_linear_mipmap, repeat_enable;
uniform vec3 ball_pos = vec3(0.0);
uniform vec3 impact_pos = vec3(56.0, 1.2, 0.0);
uniform float impact_strength = 0.0;
uniform float impact_time = 0.0;

void vertex() {
	vec3 wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	// Pin the front goal-post edge (x near +-55.0) and ground edge (y near 0) so posts stay attached
	float post_pin = clamp((abs(wpos.x) - 55.05) * 1.4, 0.0, 1.0);
	// Direct push when ball touches or enters the net
	float d_ball = length(wpos - ball_pos);
	float ball_push = exp(-d_ball * d_ball * 1.8) * 0.65 * post_pin;
	vec3 push_dir = normalize(vec3(sign(wpos.x) * 1.2, 0.15, (wpos.z - ball_pos.z) * 0.35));
	// Elastic ripple wave across the whole net when a goal is scored or ball hits the net
	float d_imp = length(wpos - impact_pos);
	float wave = sin(d_imp * 6.5 - impact_time * 20.0) * exp(-d_imp * 0.38) * impact_strength * 0.42 * post_pin;
	float bulge = exp(-d_imp * 0.55) * impact_strength * 0.55 * post_pin;
	vec3 world_disp = push_dir * (ball_push + bulge + wave);
	VERTEX += (inverse(MODEL_MATRIX) * vec4(world_disp, 0.0)).xyz;
}

void fragment() {
	vec4 c = texture(net_tex, UV * 6.0);
	if (c.a < 0.5) discard;
	ALBEDO = c.rgb;
}
"""
		_goal_net_mat = ShaderMaterial.new()
		_goal_net_mat.shader = nsh
		_goal_net_mat.set_shader_parameter("net_tex", net_tex)
		net_mat = _goal_net_mat
	var gw := 7.4
	var gh := 2.5
	var gd := 2.55
	for side in [-1.0, 1.0]:
		var gx: float = 55.0 * side
		var back := MeshInstance3D.new()
		var bp := QuadMesh.new()
		bp.size = Vector2(gw, gh)
		bp.subdivide_width = 16
		bp.subdivide_depth = 10
		bp.material = net_mat
		back.mesh = bp
		back.position = Vector3(gx + gd * side, _pitch_top_y + gh / 2.0, 0)
		back.rotation.y = PI / 2.0
		stadium.add_child(back)
		for zs in [-1.0, 1.0]:
			var sidem := MeshInstance3D.new()
			var sp := QuadMesh.new()
			sp.size = Vector2(gd, gh)
			sp.subdivide_width = 10
			sp.subdivide_depth = 10
			sp.material = net_mat
			sidem.mesh = sp
			sidem.position = Vector3(gx + gd * side / 2.0, _pitch_top_y + gh / 2.0, zs * gw / 2.0)
			stadium.add_child(sidem)
		var roof := MeshInstance3D.new()
		var rp := QuadMesh.new()
		rp.size = Vector2(gd, gw)
		rp.subdivide_width = 10
		rp.subdivide_depth = 14
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

const BOARD_TEXTS := ["DZFOOT LIGUE 1", "ALGERIE TELECOM", "MOBILIS 5G", "DJEZZY FOOT", "AIR ALGERIE", "SONATRACH", "DZ SPORT HD", "COUPE D'ALGERIE"]

const ADBOARD_LED_SHADER := """
shader_type spatial;
uniform vec3 col_a : source_color = vec3(0.04, 0.22, 0.68);
uniform vec3 col_b : source_color = vec3(0.04, 0.52, 0.26);
uniform vec3 col_c : source_color = vec3(0.76, 0.12, 0.18);
uniform float phase_offset = 0.0;
varying vec3 wpos;
void vertex() {
	wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}
void fragment() {
	float t = TIME * 0.22 + phase_offset;
	float idx = floor(t);
	float f = smoothstep(0.78, 1.0, fract(t));
	int s0 = int(mod(idx, 3.0));
	int s1 = int(mod(idx + 1.0, 3.0));
	vec3 c0 = (s0 == 0) ? col_a : ((s0 == 1) ? col_b : col_c);
	vec3 c1 = (s1 == 0) ? col_a : ((s1 == 1) ? col_b : col_c);
	vec3 bg = mix(c0, c1, f);
	// Animated scrolling LED chevrons & light wave along the perimeter board
	float coord = (wpos.x + wpos.z) * 0.65 - TIME * 2.2;
	float stripe = smoothstep(0.35, 0.65, 0.5 + 0.5 * sin(coord * 3.14159));
	float sweep = pow(0.5 + 0.5 * sin((wpos.x - wpos.z) * 0.18 - TIME * 3.4), 4.0);
	// LED pixel matrix effect
	float led_grid = (0.88 + 0.12 * sin(UV.y * 90.0)) * (0.90 + 0.10 * sin(coord * 28.0));
	vec3 final_col = bg * (0.85 + 0.28 * stripe) + vec3(0.95, 0.92, 0.78) * sweep * 0.26;
	final_col *= led_grid;
	ALBEDO = final_col;
	ROUGHNESS = 0.35;
	EMISSION = final_col * 0.42;
}
"""

const CROWD_ANIM_SHADER := """
shader_type spatial;
render_mode cull_disabled;
uniform sampler2D crowd_tex : source_color, filter_nearest, repeat_enable;
varying vec3 wpos;
varying vec3 wnorm;
void vertex() {
	wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	wnorm = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}
void fragment() {
	vec3 blending = abs(wnorm);
	blending = normalize(max(blending, vec3(0.00001)));
	float b = blending.x + blending.y + blending.z;
	blending /= vec3(b);
	// Base world triplanar coords matching uv1_scale = 0.04 (64 spectators per 256px tile)
	vec2 uv_x = wpos.zy * 0.04;
	vec2 uv_y = wpos.xz * 0.04;
	vec2 uv_z = wpos.xy * 0.04;
	// Per-spectator cell index (each spectator is a 4x4 texel block -> 64 cells per UV unit)
	float cell_x = floor(uv_x.x * 64.0) + floor(uv_x.y * 64.0) * 17.0;
	float cell_z = floor(uv_z.x * 64.0) + floor(uv_z.y * 64.0) * 17.0;
	float cell_y = floor(uv_y.x * 64.0) + floor(uv_y.y * 64.0) * 17.0;
	// Stadium wave (ola) + individual jumping/cheering bounce per spectator
	float wave = sin(atan(wpos.z, wpos.x) * 4.0 - TIME * 2.6) * 0.5 + 0.5;
	float bounce_x = sin(TIME * 6.8 + cell_x * 2.17) * (0.0032 + 0.0022 * wave);
	float sway_x   = cos(TIME * 4.2 + cell_x * 3.71) * 0.0014;
	float bounce_z = sin(TIME * 6.8 + cell_z * 2.17) * (0.0032 + 0.0022 * wave);
	float sway_z   = cos(TIME * 4.2 + cell_z * 3.71) * 0.0014;
	float bounce_y = sin(TIME * 6.8 + cell_y * 2.17) * (0.0032 + 0.0022 * wave);
	vec3 cx = texture(crowd_tex, uv_x + vec2(sway_x, bounce_x)).rgb;
	vec3 cy = texture(crowd_tex, uv_y + vec2(sway_z, bounce_y)).rgb;
	vec3 cz = texture(crowd_tex, uv_z + vec2(sway_z, bounce_z)).rgb;
	vec3 col = cx * blending.x + cy * blending.y + cz * blending.z;
	ALBEDO = col;
	ROUGHNESS = 0.92;
}
"""

func _setup_ad_boards() -> void:
	_ad_board_labels.clear()
	var mats := [
		_make_board_mat(Color(0.05, 0.22, 0.68), 0.0),
		_make_board_mat(Color(0.05, 0.50, 0.24), 1.0),
		_make_board_mat(Color(0.76, 0.12, 0.18), 2.0),
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
	_ad_board_labels.append(lb)

func _make_board_mat(bg: Color, phase_ofs: float = 0.0) -> Material:
	if _software_gl:
		var m := StandardMaterial3D.new()
		m.albedo_color = bg
		m.roughness = 0.5
		m.emission_enabled = true
		m.emission = bg.lightened(0.2)
		m.emission_energy_multiplier = 0.15
		return m
	var sh := Shader.new()
	sh.code = ADBOARD_LED_SHADER
	var sm := ShaderMaterial.new()
	sm.shader = sh
	sm.set_shader_parameter("phase_offset", phase_ofs)
	return sm

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
	var crowd_mat: Material
	if _software_gl:
		var cmat := StandardMaterial3D.new()
		cmat.albedo_texture = tex
		cmat.roughness = 0.95
		cmat.albedo_color = Color(1, 1, 1)
		cmat.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
		cmat.cull_mode = BaseMaterial3D.CULL_DISABLED
		cmat.uv1_triplanar = true
		cmat.uv1_world_triplanar = true
		cmat.uv1_scale = Vector3(0.04, 0.04, 0.04)
		crowd_mat = cmat
	else:
		var csh := Shader.new()
		csh.code = CROWD_ANIM_SHADER
		var sm := ShaderMaterial.new()
		sm.shader = csh
		sm.set_shader_parameter("crowd_tex", tex)
		crowd_mat = sm
	var model := stadium.get_node_or_null("StadiumModel")
	if model == null:
		return
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var stack: Array = [model]
	var n_applied := 0
	while not stack.is_empty():
		var nd: Node = stack.pop_back()
		stack.append_array(nd.get_children())
		if nd is MeshInstance3D and (nd as MeshInstance3D).mesh:
			var mi := nd as MeshInstance3D
			var sz: Vector3 = mi.mesh.get_aabb().size
			# Skip the big ground slabs; batch every stand piece into a single draw call!
			if maxf(sz.x, sz.z) < 40.0 and mi.global_position.y > -1.0:
				for s in range(mi.mesh.get_surface_count()):
					st.append_from(mi.mesh, s, mi.transform)
				mi.visible = false
				n_applied += 1
	if n_applied > 0:
		var merged_mi := MeshInstance3D.new()
		merged_mi.name = "MergedCrowdStands"
		merged_mi.mesh = st.commit()
		merged_mi.material_override = crowd_mat
		merged_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		model.add_child(merged_mi)
	print("[DZFoot] Animated crowd texture applied & batched on %d stand pieces (1 draw call)" % n_applied)

func _setup_stadium_floodlights() -> void:
	# Disabled 3D additive light cones: on GLES3 gl_compatibility they saturate
	# the upper framebuffer to pure white.
	return

	# Disabled 3D additive light cones: on GLES3 gl_compatibility they saturate
	# the upper framebuffer to pure white.
	return






