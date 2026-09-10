extends RefCounted
class_name DZFootBridgeGD

# Pure GDScript fallback for DZFootBridge GDExtension.
# Parses DZFoot binary protocol packets (little-endian) without native code.

const DZ_MAX_PLAYERS = 22

# GameStatePacket field offsets (sizeof = 1256)
const OFF_TICK = 12
const OFF_TIMESTAMP = 16
const OFF_GAME_MODE = 24
const OFF_GAME_FLAGS = 25
const OFF_SCORE = 26
const OFF_TIMER = 28
const OFF_BALL = 32          # NetworkBallState: pos(12)+vel(12)+rot(12)+ownedTeam(1)+ownedPlayer(1)+pad(2) = 40
const OFF_PLAYERS = 72       # 12+4+8+1+1+2+4+40 = 72
const PLAYER_SIZE = 48       # pos(12)+vel(12)+dir(12)+rotY(4)+anim(1)+team(1)+role(1)+flags(1)+tired(4)
const OFF_OFFICIALS = 72 + (48 * 22)  # = 1128
const OFFICIAL_SIZE = 32     # pos(12)+dir(12)+rotY(4)+anim(1)+team(1)+role(1)+flags(1)
const OFF_CAMERA = 1128 + (32 * 3)    # = 1224
# Camera: pos(12)+rot(16)+fov(4) = 32

# MatchSetupPacket field offsets
const SETUP_OFF_PLAYER_COUNT = 12
const SETUP_OFF_TEAM_A_NAME = 13
const SETUP_OFF_TEAM_B_NAME = 45  # 13 + 32
const SETUP_OFF_TEAM_A_COLOR1 = 77  # 45 + 32
const SETUP_OFF_TEAM_B_COLOR1 = 83  # 77 + 6
const SETUP_OFF_STADIUM_ID = 89     # 83 + 6
const SETUP_OFF_DURATION = 90
const SETUP_OFF_PLAYERS = 94        # 89 + 1 + 1 + 2
# PlayerStaticInfo: index(1)+team(1)+role(1)+lastName(32)+height(4)+skinColor(1)+hairStyle(1)+hairColor(1)+stats(88)+playerNumber(1)+bodyType(1)+beardStyle(1)+eyeColor(1) = 132
const STATIC_PLAYER_SIZE = 132

# PlayerInputPacket size = 36
const INPUT_PACKET_SIZE = 36

var _state_data: PackedByteArray = PackedByteArray()
var _has_new_state: bool = false
var _setup_data: PackedByteArray = PackedByteArray()
var _has_match_setup: bool = false
var _event_data: PackedByteArray = PackedByteArray()
var _has_new_event: bool = false

var _input_dir_x: float = 0.0
var _input_dir_z: float = 0.0
var _input_buttons: int = 0
var _input_player_idx: int = 0
var _input_team: int = 0

func set_game_state_bytes(data: PackedByteArray) -> void:
	if data.size() < 1256:
		return
	_state_data = data.duplicate()
	_has_new_state = true

func set_match_setup_bytes(data: PackedByteArray) -> void:
	_setup_data = data.duplicate()
	_has_match_setup = true

func set_match_event_bytes(data: PackedByteArray) -> void:
	_event_data = data.duplicate()
	_has_new_event = true

func has_new_state() -> bool:
	return _has_new_state

func consume_state() -> void:
	_has_new_state = false

func has_match_setup() -> bool:
	return _has_match_setup

func has_new_event() -> bool:
	return _has_new_event

func consume_event() -> void:
	_has_new_event = false

# === GameState accessors ===

func get_tick() -> int:
	if _state_data.size() < OFF_TICK + 4:
		return 0
	return _state_data.decode_u32(OFF_TICK)

func get_game_flags() -> int:
	if _state_data.size() < OFF_GAME_FLAGS + 1:
		return 0
	return _state_data[OFF_GAME_FLAGS]

func get_score(team: int) -> int:
	if _state_data.size() < OFF_SCORE + 2:
		return 0
	return _state_data[OFF_SCORE + team]

func get_timer() -> float:
	if _state_data.size() < OFF_TIMER + 4:
		return 0.0
	return _state_data.decode_float(OFF_TIMER)

func get_ball_position() -> Vector3:
	if _state_data.size() < OFF_BALL + 12:
		return Vector3.ZERO
	return Vector3(
		_state_data.decode_float(OFF_BALL),
		_state_data.decode_float(OFF_BALL + 4),
		_state_data.decode_float(OFF_BALL + 8)
	)

func get_ball_velocity() -> Vector3:
	if _state_data.size() < OFF_BALL + 24:
		return Vector3.ZERO
	return Vector3(
		_state_data.decode_float(OFF_BALL + 12),
		_state_data.decode_float(OFF_BALL + 16),
		_state_data.decode_float(OFF_BALL + 20)
	)

func get_player_position(idx: int) -> Vector3:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return Vector3.ZERO
	var off := OFF_PLAYERS + idx * PLAYER_SIZE
	if _state_data.size() < off + 12:
		return Vector3.ZERO
	return Vector3(
		_state_data.decode_float(off),
		_state_data.decode_float(off + 4),
		_state_data.decode_float(off + 8)
	)

func get_player_rotation_y(idx: int) -> float:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0.0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 36
	if _state_data.size() < off + 4:
		return 0.0
	return _state_data.decode_float(off)

func get_player_anim(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 40
	if _state_data.size() < off + 1:
		return 0
	return _state_data[off]

func get_player_team(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 41
	if _state_data.size() < off + 1:
		return 0
	return _state_data[off]

func get_player_role(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 42
	if _state_data.size() < off + 1:
		return 0
	return _state_data[off]

func get_player_flags(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 43
	if _state_data.size() < off + 1:
		return 0
	return _state_data[off]

func get_player_velocity(idx: int) -> Vector3:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return Vector3.ZERO
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 12
	if _state_data.size() < off + 12:
		return Vector3.ZERO
	return Vector3(
		_state_data.decode_float(off),
		_state_data.decode_float(off + 4),
		_state_data.decode_float(off + 8)
	)

func get_player_tired(idx: int) -> float:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0.0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 44
	if _state_data.size() < off + 4:
		return 0.0
	return _state_data.decode_float(off)

func get_official_position(idx: int) -> Vector3:
	if idx < 0 or idx >= 3:
		return Vector3.ZERO
	var off := OFF_OFFICIALS + idx * OFFICIAL_SIZE
	if _state_data.size() < off + 12:
		return Vector3.ZERO
	return Vector3(
		_state_data.decode_float(off),
		_state_data.decode_float(off + 4),
		_state_data.decode_float(off + 8)
	)

func get_official_rotation_y(idx: int) -> float:
	if idx < 0 or idx >= 3:
		return 0.0
	var off := OFF_OFFICIALS + idx * OFFICIAL_SIZE + 24
	if _state_data.size() < off + 4:
		return 0.0
	return _state_data.decode_float(off)

func get_camera_position() -> Vector3:
	if _state_data.size() < OFF_CAMERA + 12:
		return Vector3.ZERO
	return Vector3(
		_state_data.decode_float(OFF_CAMERA),
		_state_data.decode_float(OFF_CAMERA + 4),
		_state_data.decode_float(OFF_CAMERA + 8)
	)

func get_camera_rotation() -> Quaternion:
	if _state_data.size() < OFF_CAMERA + 28:
		return Quaternion.IDENTITY
	return Quaternion(
		_state_data.decode_float(OFF_CAMERA + 12),
		_state_data.decode_float(OFF_CAMERA + 16),
		_state_data.decode_float(OFF_CAMERA + 20),
		_state_data.decode_float(OFF_CAMERA + 24)
	)

func get_camera_fov() -> float:
	if _state_data.size() < OFF_CAMERA + 32:
		return 55.0
	return _state_data.decode_float(OFF_CAMERA + 28)

# === MatchSetup accessors ===

func get_team_a_name() -> String:
	if _setup_data.size() < SETUP_OFF_TEAM_A_NAME + 32:
		return ""
	return _get_string(SETUP_OFF_TEAM_A_NAME, 32)

func get_team_b_name() -> String:
	if _setup_data.size() < SETUP_OFF_TEAM_B_NAME + 32:
		return ""
	return _get_string(SETUP_OFF_TEAM_B_NAME, 32)

func get_player_count() -> int:
	if _setup_data.size() < SETUP_OFF_PLAYER_COUNT + 1:
		return 0
	return _setup_data[SETUP_OFF_PLAYER_COUNT]

func get_match_player_team(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + 1
	if _setup_data.size() < off + 1:
		return 0
	return _setup_data[off]

func get_match_player_name(idx: int) -> String:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return ""
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + 3
	if _setup_data.size() < off + 32:
		return ""
	return _get_string(off, 32)

func get_match_player_number(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + 3 + 32 + 4 + 3 + 88
	if _setup_data.size() < off + 1:
		return 0
	return _setup_data[off]

# === Input ===

func set_input_direction(x: float, z: float) -> void:
	_input_dir_x = x
	_input_dir_z = z

func set_input_buttons(buttons: int) -> void:
	_input_buttons = buttons

func set_active_player(idx: int, team: int) -> void:
	_input_player_idx = idx
	_input_team = team

func get_input_bytes() -> PackedByteArray:
	var pkt := PackedByteArray()
	pkt.resize(INPUT_PACKET_SIZE)
	# Header: magic(4) + version(2) + type(2) + size(2) + flags(2) = 12
	pkt.encode_u32(0, 0x54465A44)  # DZ_MAGIC 'DZFT' little-endian
	pkt.encode_u16(4, 1)           # DZ_PROTOCOL_VERSION
	pkt.encode_u16(6, 3)           # PACKET_PLAYER_INPUT
	pkt.encode_u16(8, INPUT_PACKET_SIZE)  # size
	pkt.encode_u16(10, 0)          # flags
	# dirX(4) + dirZ(4) + buttons(2) + playerIdx(1) + team(1) + clientTick(4) + clientTimeUs(8)
	pkt.encode_float(12, _input_dir_x)
	pkt.encode_float(16, _input_dir_z)
	pkt.encode_u16(20, _input_buttons)
	pkt[22] = _input_player_idx
	pkt[23] = _input_team
	pkt.encode_u32(24, 0)  # clientTick
	pkt.encode_u64(28, 0)  # clientTimeUs
	return pkt

# === Helpers ===

func _get_string(offset: int, max_len: int) -> String:
	var end := offset
	while end < offset + max_len and end < _setup_data.size() and _setup_data[end] != 0:
		end += 1
	if end == offset:
		return ""
	return _setup_data.slice(offset, end).get_string_from_ascii()
