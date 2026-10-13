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
const PLAYER_SIZE = 48       # pos(12)+vel(8)+dir(12)+rotY(4)+anim(1)+team(1)+role(1)+flags(1)+tired(4)+functionType(1)+enumVelocity(1)+animProgress(2)
# NetworkPlayerState field offsets within the 48-byte record:
#   pos=0(12B) vel=12(8B) dir=20(12B) rotY=32 anim=36 team=37 role=38 flags=39
#   tiredFactor=40(4B) functionType=44 enumVelocity=45 animProgress=46(2B)
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
# PlayerStaticInfo: index(1)+team(1)+role(1)+lastName(32)+height(4)+skinColor(1)+hairStyle(1)+hairColor(1)+stats(88)+playerNumber(1)+bodyType(1)+beardStyle(1)+eyeColor(1) = 134
#   3 + 32 + 4 + 3 + 88 + 4 = 134
const STATIC_PLAYER_SIZE = 134
const PSI_HEIGHT = 35
const PSI_SKIN = 39
const PSI_HAIRSTYLE = 40
const PSI_HAIRCOLOR = 41
const PSI_STATS = 42        # 22 floats
const PSI_NUMBER = 130
const PSI_BODYTYPE = 131
const PSI_BEARD = 132
const PSI_EYE = 133

# MatchEventPacket offsets (36 bytes): header(12) eventType@12 team@13 playerIdx@14 extra@15 pos(12)@16 tick(4)@28 score[2]@32
const EV_TYPE = 12
const EV_TEAM = 13
const EV_PLAYER = 14
const EV_EXTRA = 15
const EV_SCORE = 32

# TacticalStatePacket (topic "tac", 1012 bytes):
# header(12) tick(4)@12 ts(8)@16 matchPhase@24 setPieceType@25 setPieceTeam@26
# setPieceTaker@27 selectedPlayer[2]@28 designatedPlayer[2]@30
# bestPossessionPlayer[2]@32 lastTouchTeam@34 lastTouchPlayer@35 offsideTrapX(8)@36
const TAC_OFF_TRAP = 36        # 2 floats: offside trap X per team (normalized like env.x)
const TAC_SET_PIECE_TYPE = 25
const TAC_SET_PIECE_TAKER = 27

# PlayerInputPacket size = 36
const INPUT_PACKET_SIZE = 36

# Player-record layout autodetect. The deployed server may still send the LEGACY record:
#   pos=0 vel(3 floats)=12 dir=24 rotY=36 anim=40 team=41 role=42 flags=43 tired=44
# (no clipId / animProgress / functionType). The NEW record is documented above.
var _legacy_layout: bool = false
var _layout_logged: bool = false

# Setup-packet layout autodetect. The deployed server predates the `role` field:
#   LEGACY PlayerStaticInfo = index(1)+team(1)+lastName(32)+height(4)+skin(1)
#   +hair(1)+hairCol(1)+stats(88)+number(1)+body(1)+beard(1)+eye(1)+pad(1) = 134
# Everything after `team` sits 1 byte earlier than the NEW layout above.
var _legacy_setup: bool = false
var _setup_layout_logged: bool = false

var _state_data: PackedByteArray = PackedByteArray()
var _has_new_state: bool = false
var _setup_data: PackedByteArray = PackedByteArray()
var _has_match_setup: bool = false
var _event_data: PackedByteArray = PackedByteArray()
var _has_new_event: bool = false
var _tac_data: PackedByteArray = PackedByteArray()
var _has_new_tac: bool = false

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
	_detect_layout()

func _heading_matches(dx: float, dy: float, r: float) -> bool:
	var l2 := dx * dx + dy * dy
	if is_nan(l2) or l2 < 0.9 or l2 > 1.1 or is_nan(r):
		return false
	var d := absf(wrapf(r - atan2(dx, dy), -PI, PI))
	return d < 0.02

func _detect_layout() -> void:
	var new_score := 0
	var old_score := 0
	for i in range(DZ_MAX_PLAYERS):
		var o := OFF_PLAYERS + i * PLAYER_SIZE
		if _heading_matches(_state_data.decode_float(o + 24), _state_data.decode_float(o + 28), _state_data.decode_float(o + 36)):
			old_score += 1
		if _heading_matches(_state_data.decode_float(o + 20), _state_data.decode_float(o + 24), _state_data.decode_float(o + 32)):
			new_score += 1
	var was := _legacy_layout
	if old_score > new_score + 1:
		_legacy_layout = true
	elif new_score > old_score + 1:
		_legacy_layout = false
	if not _layout_logged or was != _legacy_layout:
		if old_score + new_score > 0:
			_layout_logged = true
			print("[DZFoot] Player record layout: %s (oldScore=%d newScore=%d)" % ["LEGACY (dir@24 rotY@36 anim@40)" if _legacy_layout else "NEW (dir@20 rotY@32 anim@36 clipId)", old_score, new_score])

# Per-layout field offsets inside the 48-byte player record
func _f_roty() -> int:
	return 36 if _legacy_layout else 32

func _f_anim() -> int:
	return 40 if _legacy_layout else 36

func _f_team() -> int:
	return 41 if _legacy_layout else 37

func _f_role() -> int:
	return 42 if _legacy_layout else 38

func _f_flags() -> int:
	return 43 if _legacy_layout else 39

func _f_tired() -> int:
	return 44 if _legacy_layout else 40

func is_legacy_layout() -> bool:
	return _legacy_layout

func set_match_setup_bytes(data: PackedByteArray) -> void:
	_setup_data = data.duplicate()
	_has_match_setup = true
	_detect_setup_layout()

# NEW layout puts a small `role` enum at record+2; LEGACY puts the first
# lastName letter there (always > 15). Majority vote decides.
func _detect_setup_layout() -> void:
	var letter_votes := 0
	var seen := 0
	var count: int = min(get_player_count(), DZ_MAX_PLAYERS)
	for i in range(count):
		var o := SETUP_OFF_PLAYERS + i * STATIC_PLAYER_SIZE
		if _setup_data.size() < o + 3:
			break
		seen += 1
		if _setup_data[o + 2] > 15:
			letter_votes += 1
	var was := _legacy_setup
	if seen > 0:
		_legacy_setup = letter_votes * 2 > seen
	if not _setup_layout_logged or was != _legacy_setup:
		_setup_layout_logged = true
		print("[DZFoot] Setup layout: %s (letterVotes=%d/%d)" % ["LEGACY (no role, name@2)" if _legacy_setup else "NEW (role@2)", letter_votes, seen])

# Field offset inside PlayerStaticInfo: -1 for every field past `team` in LEGACY.
func _psi(new_off: int) -> int:
	return new_off - 1 if _legacy_setup else new_off

func set_match_event_bytes(data: PackedByteArray) -> void:
	_event_data = data.duplicate()
	_has_new_event = true

func set_tactical_state_bytes(data: PackedByteArray) -> void:
	_tac_data = data.duplicate()
	_has_new_tac = true

func has_new_tactical() -> bool:
	return _has_new_tac

# Returns {trap_a: float, trap_b: float} in Godot world X (meters), or null.
func get_set_piece_type() -> int:
	if _tac_data.size() < TAC_SET_PIECE_TYPE + 1:
		return -1
	return _tac_data[TAC_SET_PIECE_TYPE]

func get_offside_traps():
	if _tac_data.size() < TAC_OFF_TRAP + 8:
		return null
	_has_new_tac = false
	var a := _tac_data.decode_float(TAC_OFF_TRAP)
	var b := _tac_data.decode_float(TAC_OFF_TRAP + 4)
	return {"trap_a": a * 55.0, "trap_b": b * 55.0}

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
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + _f_roty()
	if _state_data.size() < off + 4:
		return 0.0
	return _state_data.decode_float(off)

func get_player_anim(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + _f_anim()
	if _state_data.size() < off + 1:
		return 0
	return _state_data[off]

func get_player_clip_id(idx: int) -> int:
	# 16-bit clipId = anim (low byte @36) | enumVelocity (high byte @45).
	# Values >= 0xFF00 mean "legacy AnimId category" (in the low byte).
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0xFFFF
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 36
	if _state_data.size() < off + 10:
		return 0xFFFF
	if _legacy_layout:
		# Legacy server: no AnimCollection clipId — anim byte is the 17-category AnimId.
		return 0xFF00 | _state_data[OFF_PLAYERS + idx * PLAYER_SIZE + 40]
	return _state_data[off] | (_state_data[off + 9] << 8)

func get_player_team(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + _f_team()
	if _state_data.size() < off + 1:
		return 0
	return _state_data[off]

func get_player_role(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + _f_role()
	if _state_data.size() < off + 1:
		return 0
	return _state_data[off]

func get_player_flags(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + _f_flags()
	if _state_data.size() < off + 1:
		return 0
	return _state_data[off]

func get_player_velocity(idx: int) -> Vector3:
	# vel is 2 floats (X length + Y width); Z was repurposed for anim detail
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return Vector3.ZERO
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 12
	if _state_data.size() < off + 8:
		return Vector3.ZERO
	return Vector3(
		_state_data.decode_float(off),
		_state_data.decode_float(off + 4),
		0.0
	)

func get_player_tired(idx: int) -> float:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0.0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + _f_tired()
	if _state_data.size() < off + 4:
		return 0.0
	return _state_data.decode_float(off)

func get_player_anim_progress(idx: int) -> int:
	# 0-65535 = 0.0-1.0 through the current animation (server-synced)
	if idx < 0 or idx >= DZ_MAX_PLAYERS or _legacy_layout:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 46
	if _state_data.size() < off + 2:
		return 0
	return _state_data.decode_u16(off)

func get_player_function_type(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS or _legacy_layout:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 44
	if _state_data.size() < off + 1:
		return 0
	return _state_data[off]

func get_player_enum_velocity(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS or _legacy_layout:
		return 0
	var off := OFF_PLAYERS + idx * PLAYER_SIZE + 45
	if _state_data.size() < off + 1:
		return 0
	return _state_data[off]

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

func get_official_anim(idx: int) -> int:
	# anim byte @28 in NetworkOfficialState — legacy AnimId (0-16).
	if idx < 0 or idx >= 3:
		return 0
	var off := OFF_OFFICIALS + idx * OFFICIAL_SIZE + 28
	if _state_data.size() <= off:
		return 0
	return _state_data[off]

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
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + _psi(3)
	if _setup_data.size() < off + 32:
		return ""
	return _get_string(off, 32)

func get_match_player_number(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + _psi(PSI_NUMBER)
	if _setup_data.size() < off + 1:
		return 0
	return _setup_data[off]

func get_match_player_height(idx: int) -> float:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 1.80
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + _psi(PSI_HEIGHT)
	if _setup_data.size() < off + 4:
		return 1.80
	return _setup_data.decode_float(off)

func get_match_player_skin(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 2
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + _psi(PSI_SKIN)
	if _setup_data.size() < off + 1:
		return 2
	return clampi(_setup_data[off], 0, 6)

func get_match_player_hair_style(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + _psi(PSI_HAIRSTYLE)
	if _setup_data.size() < off + 1:
		return 0
	return clampi(_setup_data[off], 0, 5)

func get_match_player_hair_color(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + _psi(PSI_HAIRCOLOR)
	if _setup_data.size() < off + 1:
		return 0
	return clampi(_setup_data[off], 0, 7)

func get_match_player_body_type(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 1
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + _psi(PSI_BODYTYPE)
	if _setup_data.size() < off + 1:
		return 1
	return clampi(_setup_data[off], 0, 3)

func get_match_player_beard(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + _psi(PSI_BEARD)
	if _setup_data.size() < off + 1:
		return 0
	return clampi(_setup_data[off], 0, 3)

func get_match_player_role(idx: int) -> int:
	if idx < 0 or idx >= DZ_MAX_PLAYERS:
		return 0
	if _legacy_setup:
		return 0 # no role byte in legacy records (byte@2 is lastName[0])
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + 2
	if _setup_data.size() < off + 1:
		return 0
	return _setup_data[off]

func get_match_player_stat(idx: int, stat: int) -> float:
	if idx < 0 or idx >= DZ_MAX_PLAYERS or stat < 0 or stat >= 22:
		return 0.0
	var off := SETUP_OFF_PLAYERS + idx * STATIC_PLAYER_SIZE + _psi(PSI_STATS) + stat * 4
	if _setup_data.size() < off + 4:
		return 0.0
	return _setup_data.decode_float(off)

# === MatchEvent accessors ===

func get_event_type() -> int:
	if _event_data.size() < EV_TYPE + 1:
		return -1
	return _event_data[EV_TYPE]

func get_event_team() -> int:
	if _event_data.size() < EV_TEAM + 1:
		return -1
	return _event_data[EV_TEAM]

func get_event_player_idx() -> int:
	if _event_data.size() < EV_PLAYER + 1:
		return -1
	return _event_data[EV_PLAYER]

func get_event_score(team: int) -> int:
	if _event_data.size() < EV_SCORE + 2:
		return -1
	return _event_data[EV_SCORE + team]

# === Ball rotation (angular state) ===

func get_ball_rotation() -> Vector3:
	if _state_data.size() < OFF_BALL + 36:
		return Vector3.ZERO
	return Vector3(
		_state_data.decode_float(OFF_BALL + 24),
		_state_data.decode_float(OFF_BALL + 28),
		_state_data.decode_float(OFF_BALL + 32)
	)

# Team kit colors — RGB triplets at A_COLOR1=77, A_COLOR2=80, B_COLOR1=83, B_COLOR2=86
func get_team_a_color(primary: bool) -> Color:
	return _read_rgb(SETUP_OFF_TEAM_A_COLOR1 + (0 if primary else 3))

func get_team_b_color(primary: bool) -> Color:
	return _read_rgb(SETUP_OFF_TEAM_B_COLOR1 + (0 if primary else 3))

func _read_rgb(off: int) -> Color:
	if _setup_data.size() < off + 3:
		return Color.WHITE
	return Color(_setup_data[off] / 255.0, _setup_data[off + 1] / 255.0, _setup_data[off + 2] / 255.0)

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
