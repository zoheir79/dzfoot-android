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

# === DZFootBridge (GDExtension or GDScript fallback) ===
var dz_bridge: Object = null

# === State ===
var last_tick: int = 0
var _active_player_idx: int = 0
var _active_team: int = 0

# Coordinate mapping: GF Y up = Godot Y up, GF Z forward = Godot -Z forward
const Z_FLIP = -1.0

# === Interpolation state ===
var _prev_positions: Array = []  # Array[Vector3] for players
var _curr_positions: Array = []
var _prev_ball_pos: Vector3 = Vector3.ZERO
var _curr_ball_pos: Vector3 = Vector3.ZERO
var _state_interp_time: float = 0.0
var _state_interp_period: float = 0.05  # 50ms between states (20Hz update)
var _state_received: bool = false

# === Touch UI ===
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

# === Input state ===
var _move_dir := Vector2.ZERO
var _buttons: int = 0

# Button definitions: name -> {mask, label, color, offset}
const BUTTON_DEFS = {
	"pass":     {"mask": BUTTON_PASS,           "label": "P", "color": Color(0.2, 0.9, 0.2, 0.7), "offset": Vector2(0, 0)},
	"shot":     {"mask": BUTTON_SHOT,           "label": "S", "color": Color(0.9, 0.2, 0.2, 0.7), "offset": Vector2(-90, -70)},
	"kick":     {"mask": BUTTON_KICK,           "label": "K", "color": Color(0.9, 0.9, 0.2, 0.7), "offset": Vector2(80, -50)},
	"sprint":   {"mask": BUTTON_SPRINT,         "label": "R", "color": Color(0.2, 0.8, 0.9, 0.7), "offset": Vector2(-130, 30)},
	"switch":   {"mask": BUTTON_SWITCH_PLAYER,  "label": "W", "color": Color(0.9, 0.9, 0.9, 0.7), "offset": Vector2(60, -120)},
	"dribble":  {"mask": BUTTON_DRIBBLE,        "label": "D", "color": Color(0.8, 0.2, 0.8, 0.7), "offset": Vector2(120, 40)},
	"sliding":  {"mask": BUTTON_SLIDING,        "label": "T", "color": Color(0.9, 0.5, 0.1, 0.7), "offset": Vector2(-60, 80)},
	"highpass": {"mask": BUTTON_HIGH_PASS,      "label": "H", "color": Color(0.5, 0.2, 0.9, 0.7), "offset": Vector2(40, 90)},
}

func _ready() -> void:
	# Get the DZFootPlugin singleton from Kotlin
	dz_plugin = Engine.get_singleton("DZFootPlugin")
	if dz_plugin:
		dz_plugin.game_state_received.connect(_on_game_state_received)
		dz_plugin.match_setup_received.connect(_on_match_setup_received)
		dz_plugin.match_event_received.connect(_on_match_event_received)
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

	# Init interpolation arrays
	for i in range(DZ_MAX_PLAYERS):
		_prev_positions.append(Vector3.ZERO)
		_curr_positions.append(Vector3.ZERO)

	_setup_camera()
	_setup_lighting()
	_setup_stadium()
	_setup_ball()
	_setup_players()
	_setup_officials()
	_setup_touch_ui()
	_setup_ui_overlay()

	# If no match setup arrives within 8s, show a demo formation so the
	# scene is visible even without a running server.
	_demo_timer = Timer.new()
	_demo_timer.wait_time = 8.0
	_demo_timer.one_shot = true
	_demo_timer.timeout.connect(_setup_demo_mode)
	add_child(_demo_timer)
	_demo_timer.start()

	print("[DZFoot] Scene initialized — waiting for GameStatePackets (demo in 8s if none)")

# === Plugin signal handlers ===
func _on_game_state_received(data: PackedByteArray) -> void:
	if dz_bridge:
		dz_bridge.set_game_state_bytes(data)

func _on_match_setup_received(data: PackedByteArray) -> void:
	if _demo_timer and is_instance_valid(_demo_timer):
		_demo_timer.stop()
		_demo_timer = null
	if dz_bridge:
		dz_bridge.set_match_setup_bytes(data)
	_apply_match_setup()

func _on_match_event_received(data: PackedByteArray) -> void:
	if dz_bridge:
		dz_bridge.set_match_event_bytes(data)

# === Scene setup ===

func _setup_camera() -> void:
	camera = Camera3D.new()
	camera.fov = 55.0
	camera.near = 0.1
	camera.far = 500.0
	camera.position = Vector3(0, 40, 50)
	camera.look_at(Vector3.ZERO)
	add_child(camera)
	camera.make_current()

func _setup_lighting() -> void:
	directional_light = DirectionalLight3D.new()
	directional_light.light_energy = 1.2
	directional_light.shadow_enabled = true
	directional_light.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS
	directional_light.position = Vector3(30, 60, 30)
	directional_light.look_at(Vector3.ZERO)
	add_child(directional_light)

	world_env = WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_color = Color(0.5, 0.6, 0.8, 1.0)
	env.ambient_light_energy = 0.6
	env.fog_enabled = true
	env.fog_light_color = Color(0.6, 0.7, 0.85, 1.0)
	env.fog_density = 0.005
	env.ssr_enabled = false
	env.ssil_enabled = false
	env.ssao_enabled = true
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
		pitch_scene.scale = Vector3(0.1, 0.1, 0.1)
		stadium.add_child(pitch_scene)
		print("[DZFoot] Pitch GLB loaded")
	else:
		# Fallback: procedural pitch
		var pitch_mesh := PlaneMesh.new()
		pitch_mesh.size = Vector2(105, 68)
		pitch_mesh.material = _create_pitch_material()
		var pitch := MeshInstance3D.new()
		pitch.mesh = pitch_mesh
		stadium.add_child(pitch)
		# Pitch lines
		var line_mat := StandardMaterial3D.new()
		line_mat.albedo_color = Color.WHITE
		line_mat.roughness = 0.9
		var line_w := 0.1
		_create_line_box(stadium, Vector3(52.5, 0.01, 0), Vector3(line_w, 0.02, 68), line_mat)
		_create_line_box(stadium, Vector3(-52.5, 0.01, 0), Vector3(line_w, 0.02, 68), line_mat)
		_create_line_box(stadium, Vector3(0, 0.01, 34), Vector3(105, 0.02, line_w), line_mat)
		_create_line_box(stadium, Vector3(0, 0.01, -34), Vector3(105, 0.02, line_w), line_mat)
		_create_line_box(stadium, Vector3(0, 0.01, 0), Vector3(line_w, 0.02, 68), line_mat)
		print("[DZFoot] Pitch fallback (procedural)")

	# Load goals GLB
	var goals_scene = _load_glb("res://models/goals.glb")
	if goals_scene:
		goals_scene.name = "Goals"
		goals_scene.scale = Vector3(0.1, 0.1, 0.1)
		stadium.add_child(goals_scene)
		print("[DZFoot] Goals GLB loaded")

	# Load stadium GLB
	var stad_scene = _load_glb("res://models/stadium_minimal.glb")
	if stad_scene:
		stad_scene.name = "StadiumModel"
		stad_scene.scale = Vector3(0.1, 0.1, 0.1)
		stadium.add_child(stad_scene)
		print("[DZFoot] Stadium GLB loaded")

func _load_glb(path: String) -> Node3D:
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
	mat.albedo_color = Color(0.15, 0.45, 0.12, 1.0)
	mat.roughness = 0.8
	mat.metallic = 0.0
	return mat

func _setup_ball() -> void:
	# Try loading real ball GLB
	var ball_scene = _load_glb("res://models/ball.glb")
	if ball_scene:
		ball = _find_mesh_in_node(ball_scene)
		if ball:
			ball.name = "Ball"
			ball.scale = Vector3(0.5, 0.5, 0.5)
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

func _setup_players() -> void:
	# Try to load player_base.glb as a scene template
	var player_scene = _load_glb("res://models/player_base.glb")
	if player_scene:
		print("[DZFoot] Player base GLB loaded, instancing %d players" % DZ_MAX_PLAYERS)
		for i in range(DZ_MAX_PLAYERS):
			var player := _clone_player(player_scene, i)
			players.append(player)
			add_child(player)
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

func _clone_player(template: Node3D, idx: int) -> Node3D:
	var player := template.duplicate() as Node3D
	player.name = "Player_%d" % idx
	player.scale = Vector3(0.1, 0.1, 0.1)  # Match pitch scale (meters → world units)
	# Apply team color based on index
	var team_color := Color(0.2, 0.4, 0.9) if idx < 11 else Color(0.9, 0.4, 0.2)
	_apply_color_to_meshes(player, team_color)
	return player

func _apply_color_to_meshes(node: Node3D, color: Color) -> void:
	for child in node.get_children():
		if child is MeshInstance3D:
			var mi := child as MeshInstance3D
			var mat := mi.material_override as StandardMaterial3D
			if not mat:
				mat = StandardMaterial3D.new()
				mat.roughness = 0.7
				mi.material_override = mat
			mat.albedo_color = color
		_apply_color_to_meshes(child, color)

func _setup_officials() -> void:
	for i in range(3):
		var official := Node3D.new()
		official.name = "Official_%d" % i
		var body_mesh := CapsuleMesh.new()
		body_mesh.radius = 0.22
		body_mesh.height = 1.6
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color.BLACK
		mat.roughness = 0.7
		body_mesh.material = mat
		var body := MeshInstance3D.new()
		body.mesh = body_mesh
		body.position.y = 0.8
		official.add_child(body)
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

	# Virtual joystick (bottom-left)
	_joy_base = _create_circle_control(_joy_radius, Color(1, 1, 1, 0.15))
	_joy_base.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_joy_base.position = Vector2(160, get_viewport().size.y - 160 - _joy_radius)
	root_control.add_child(_joy_base)

	_joy_knob = _create_circle_control(_joy_radius * 0.4, Color(1, 1, 1, 0.4))
	_joy_knob.position = Vector2(_joy_radius - _joy_radius * 0.4, _joy_radius - _joy_radius * 0.4)
	_joy_base.add_child(_joy_knob)

	# Action buttons (bottom-right)
	var btn_root_x = get_viewport().size.x - 200
	var btn_root_y = get_viewport().size.y - 200
	for btn_name in BUTTON_DEFS:
		var def: Dictionary = BUTTON_DEFS[btn_name]
		var btn := _create_circle_control(45.0, def.color)
		btn.position = Vector2(btn_root_x + def.offset.x, btn_root_y + def.offset.y)
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

func _create_circle_control(radius: float, color: Color) -> Control:
	var ctrl := Control.new()
	ctrl.size = Vector2(radius * 2, radius * 2)
	var _color := color
	ctrl.set("_draw_color", _color)
	ctrl.draw.connect(func():
		ctrl.draw_circle(ctrl.size * 0.5, radius, _color)
	)
	ctrl.queue_redraw()
	return ctrl

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
	_score_label.text = "%d - %d" % [_score_a, _score_b]
	var minutes: int = int(_game_timer) / 60
	var seconds: int = int(_game_timer) % 60
	_timer_label.text = "%02d:%02d" % [minutes, seconds]
	_team_a_label.text = _team_a_name
	_team_b_label.text = _team_b_name

# === Process loop ===

func _process(delta: float) -> void:
	# Process incoming game state
	if dz_bridge and dz_bridge.has_new_state():
		_store_interpolation_state(dz_bridge)
		_apply_game_state(dz_bridge)
		dz_bridge.consume_state()

	# Apply interpolation
	_apply_interpolation(delta)

	# Build and send input packet
	_update_and_send_input()

	# Redraw touch UI if needed
	if _touch_canvas:
		_touch_canvas.visible = _ui_visible

func _store_interpolation_state(bridge: Object) -> void:
	# Store current positions as previous, capture new as current
	for i in range(DZ_MAX_PLAYERS):
		_prev_positions[i] = _curr_positions[i]
		var pos = bridge.get_player_position(i)
		_curr_positions[i] = Vector3(pos.x, pos.y, pos.z * Z_FLIP)

	_prev_ball_pos = _curr_ball_pos
	var bpos = bridge.get_ball_position()
	_curr_ball_pos = Vector3(bpos.x, bpos.y, bpos.z * Z_FLIP)

	_state_interp_time = 0.0
	_state_received = true

func _apply_interpolation(delta: float) -> void:
	if not _state_received:
		return
	_state_interp_time += delta
	var alpha: float = clampf(_state_interp_time / _state_interp_period, 0.0, 1.0)

	# Interpolate ball
	ball.position = _prev_ball_pos.lerp(_curr_ball_pos, alpha)

	# Interpolate players
	for i in range(DZ_MAX_PLAYERS):
		players[i].position = _prev_positions[i].lerp(_curr_positions[i], alpha)

func _apply_game_state(bridge: Object) -> void:
	var tick: int = bridge.get_tick()
	if tick == last_tick:
		return
	last_tick = tick

	# Detect active player from flags (flag bit 2 = 0x04 = human-controlled)
	for i in range(DZ_MAX_PLAYERS):
		var flags: int = bridge.get_player_flags(i)
		if flags & 4:
			_active_player_idx = i
			_active_team = bridge.get_player_team(i)
			bridge.set_active_player(i, _active_team)
			break

	# Players: rotation, animation (positions handled by interpolation)
	for i in range(DZ_MAX_PLAYERS):
		var p := players[i]
		var rot_y: float = bridge.get_player_rotation_y(i)
		p.rotation.y = -rot_y

		var anim_id: int = bridge.get_player_anim(i)
		_play_anim(p, anim_id)

	# Officials
	for i in range(3):
		var o := officials[i]
		var pos = bridge.get_official_position(i)
		o.position = Vector3(pos.x, pos.y, pos.z * Z_FLIP)
		o.rotation.y = -bridge.get_official_rotation_y(i)

	# Camera
	var cam_pos = bridge.get_camera_position()
	var cam_rot = bridge.get_camera_rotation()
	var cam_fov: float = bridge.get_camera_fov()
	camera.position = Vector3(cam_pos.x, cam_pos.y, cam_pos.z * Z_FLIP)
	camera.fov = cam_fov
	var quat := Quaternion(cam_rot.x, cam_rot.y, cam_rot.z, cam_rot.w)
	camera.quaternion = _convert_gf_quaternion(quat)

	# Update score and timer from game state
	_score_a = bridge.get_score(0)
	_score_b = bridge.get_score(1)
	_game_timer = bridge.get_timer()

func _convert_gf_quaternion(q: Quaternion) -> Quaternion:
	return Quaternion(q.x, q.y, -q.z, q.w)

func _play_anim(player: Node3D, anim_id: int) -> void:
	if anim_id < 0 or anim_id >= ANIM_NAMES.size():
		return
	var anim_player := player.get_node("AnimationPlayer") as AnimationPlayer
	if not anim_player:
		return
	var anim_name: String = ANIM_NAMES[anim_id]
	if anim_player.has_animation(anim_name):
		if anim_player.current_animation != anim_name:
			anim_player.play(anim_name)
	elif anim_player.current_animation != "idle" and anim_player.has_animation("idle"):
		anim_player.play("idle")

func _apply_match_setup() -> void:
	if not dz_bridge or not dz_bridge.has_match_setup():
		return

	_team_a_name = dz_bridge.get_team_a_name()
	_team_b_name = dz_bridge.get_team_b_name()
	print("[DZFoot] Match setup: %s vs %s" % [_team_a_name, _team_b_name])

	# Apply team colors to player meshes
	for i in range(DZ_MAX_PLAYERS):
		var team: int = dz_bridge.get_match_player_team(i)
		var team_color := Color(0.2, 0.4, 0.9) if team == 0 else Color(0.9, 0.4, 0.2)
		_apply_color_to_meshes(players[i], team_color)

func _setup_demo_mode() -> void:
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
			_apply_color_to_meshes(players[i], Color(0.2, 0.4, 0.9))
			players[i].position = team_a_positions[i]
			players[i].rotation.y = 0.0
		else:
			_apply_color_to_meshes(players[i], Color(0.9, 0.4, 0.2))
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
	var pos := event.position
	if event.pressed:
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
			if pos.distance_to(btn_pos) < 55 and btn_info.pointer == -1:
				btn_info.pointer = event.index
				btn_info.pressed = true
				_buttons |= btn_info.mask
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
	_joy_knob.position = Vector2(_joy_radius, _joy_radius) + delta - Vector2(_joy_radius * 0.4, _joy_radius * 0.4)

	# Map to game direction: screen X = world X, screen Y down = world Z forward
	_move_dir.x = _joy_dir.x
	_move_dir.y = -_joy_dir.y  # invert Y: up on stick = forward

func _reset_joystick_visual() -> void:
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

	# Apply camera-relative rotation to input direction
	var cam_forward := camera.global_transform.basis.z
	var cam_fwd_xz := Vector2(cam_forward.x, cam_forward.z).normalized()
	if cam_fwd_xz.length() > 0.001:
		var cam_right := Vector2(-cam_fwd_xz.y, cam_fwd_xz.x)
		var world_x := _move_dir.x * cam_right.x + _move_dir.y * cam_fwd_xz.x
		var world_z := _move_dir.x * cam_right.y + _move_dir.y * cam_fwd_xz.y
		dz_bridge.set_input_direction(world_x, world_z)
	else:
		dz_bridge.set_input_direction(_move_dir.x, _move_dir.y)

	dz_bridge.set_input_buttons(_buttons)
	dz_bridge.set_active_player(_active_player_idx, _active_team)

	# Serialize and send to Kotlin via plugin
	var input_bytes = dz_bridge.get_input_bytes()
	if input_bytes.size() > 0:
		dz_plugin.set_input_packet(input_bytes)

	# Update UI overlay
	_update_ui_overlay()
