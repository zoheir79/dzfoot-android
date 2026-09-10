// dzfoot_gdext — GDExtension bridge between DZFoot binary protocol and Godot 3D nodes
//
// Exposes DZFootBridge class to GDScript:
//   - set_game_state_bytes(PackedByteArray) -> void
//   - set_match_setup_bytes(PackedByteArray) -> void
//   - set_match_event_bytes(PackedByteArray) -> void
//   - get_tick() -> int
//   - get_ball_position() -> Vector3
//   - get_player_position(idx) -> Vector3
//   - get_player_rotation_y(idx) -> float
//   - get_player_anim(idx) -> int
//   - get_official_position(idx) -> Vector3
//   - get_official_rotation_y(idx) -> float
//   - get_camera_position() -> Vector3
//   - get_camera_rotation() -> Quaternion
//   - get_camera_fov() -> float
//   - has_new_state() -> bool
//   - consume_state() -> void
//   - set_input_direction(float, float) -> void
//   - set_input_buttons(int) -> void
//   - get_input_bytes() -> PackedByteArray

#include <godot_cpp/godot.hpp>
#include <godot_cpp/class/extension.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <cstring>
#include <cstdint>
#include <mutex>

// Include the shared protocol header
#include "DZFootProtocol.h"

using namespace godot;

class DZFootBridge : public Extension {
	GDCLASS(DZFootBridge, Extension)

private:
	std::mutex state_mutex_;
	dzfoot::GameStatePacket current_state_;
	dzfoot::MatchSetupPacket match_setup_;
	dzfoot::MatchEventPacket last_event_;
	bool has_new_state_ = false;
	bool has_match_setup_ = false;
	bool has_new_event_ = false;

	// Input state
	float input_dir_x_ = 0.0f;
	float input_dir_z_ = 0.0f;
	uint16_t input_buttons_ = 0;
	uint8_t input_player_idx_ = 0;
	uint8_t input_team_ = 0;

protected:
	static void _bind_methods() {
		// GameState input
		ClassDB::bind_method(D_METHOD("set_game_state_bytes", "data"), &DZFootBridge::set_game_state_bytes);
		ClassDB::bind_method(D_METHOD("set_match_setup_bytes", "data"), &DZFootBridge::set_match_setup_bytes);
		ClassDB::bind_method(D_METHOD("set_match_event_bytes", "data"), &DZFootBridge::set_match_event_bytes);

		// State accessors
		ClassDB::bind_method(D_METHOD("get_tick"), &DZFootBridge::get_tick);
		ClassDB::bind_method(D_METHOD("get_ball_position"), &DZFootBridge::get_ball_position);
		ClassDB::bind_method(D_METHOD("get_ball_velocity"), &DZFootBridge::get_ball_velocity);
		ClassDB::bind_method(D_METHOD("get_player_position", "idx"), &DZFootBridge::get_player_position);
		ClassDB::bind_method(D_METHOD("get_player_rotation_y", "idx"), &DZFootBridge::get_player_rotation_y);
		ClassDB::bind_method(D_METHOD("get_player_anim", "idx"), &DZFootBridge::get_player_anim);
		ClassDB::bind_method(D_METHOD("get_player_flags", "idx"), &DZFootBridge::get_player_flags);
		ClassDB::bind_method(D_METHOD("get_player_team", "idx"), &DZFootBridge::get_player_team);
		ClassDB::bind_method(D_METHOD("get_player_role", "idx"), &DZFootBridge::get_player_role);
		ClassDB::bind_method(D_METHOD("get_player_velocity", "idx"), &DZFootBridge::get_player_velocity);
		ClassDB::bind_method(D_METHOD("get_player_tired", "idx"), &DZFootBridge::get_player_tired);
		ClassDB::bind_method(D_METHOD("get_official_position", "idx"), &DZFootBridge::get_official_position);
		ClassDB::bind_method(D_METHOD("get_official_rotation_y", "idx"), &DZFootBridge::get_official_rotation_y);
		ClassDB::bind_method(D_METHOD("get_camera_position"), &DZFootBridge::get_camera_position);
		ClassDB::bind_method(D_METHOD("get_camera_rotation"), &DZFootBridge::get_camera_rotation);
		ClassDB::bind_method(D_METHOD("get_camera_fov"), &DZFootBridge::get_camera_fov);
		ClassDB::bind_method(D_METHOD("get_score"), &DZFootBridge::get_score);
		ClassDB::bind_method(D_METHOD("get_timer"), &DZFootBridge::get_timer);
		ClassDB::bind_method(D_METHOD("get_game_flags"), &DZFootBridge::get_game_flags);

		// State management
		ClassDB::bind_method(D_METHOD("has_new_state"), &DZFootBridge::has_new_state);
		ClassDB::bind_method(D_METHOD("consume_state"), &DZFootBridge::consume_state);
		ClassDB::bind_method(D_METHOD("has_match_setup"), &DZFootBridge::has_match_setup);
		ClassDB::bind_method(D_METHOD("has_new_event"), &DZFootBridge::has_new_event);
		ClassDB::bind_method(D_METHOD("consume_event"), &DZFootBridge::consume_event);

		// Match setup accessors
		ClassDB::bind_method(D_METHOD("get_team_a_name"), &DZFootBridge::get_team_a_name);
		ClassDB::bind_method(D_METHOD("get_team_b_name"), &DZFootBridge::get_team_b_name);
		ClassDB::bind_method(D_METHOD("get_player_count"), &DZFootBridge::get_player_count);
		ClassDB::bind_method(D_METHOD("get_match_player_team", "idx"), &DZFootBridge::get_match_player_team);
		ClassDB::bind_method(D_METHOD("get_match_player_name", "idx"), &DZFootBridge::get_match_player_name);
		ClassDB::bind_method(D_METHOD("get_match_player_number", "idx"), &DZFootBridge::get_match_player_number);

		// Input
		ClassDB::bind_method(D_METHOD("set_input_direction", "x", "z"), &DZFootBridge::set_input_direction);
		ClassDB::bind_method(D_METHOD("set_input_buttons", "buttons"), &DZFootBridge::set_input_buttons);
		ClassDB::bind_method(D_METHOD("set_active_player", "idx", "team"), &DZFootBridge::set_active_player);
		ClassDB::bind_method(D_METHOD("get_input_bytes"), &DZFootBridge::get_input_bytes);
	}

public:
	DZFootBridge() {
		memset(&current_state_, 0, sizeof(current_state_));
		memset(&match_setup_, 0, sizeof(match_setup_));
		memset(&last_event_, 0, sizeof(last_event_));
	}

	~DZFootBridge() {}

	void set_game_state_bytes(PackedByteArray data) {
		if (data.size() < (int)sizeof(dzfoot::GameStatePacket)) {
			return;
		}
		const uint8_t* raw = data.ptr();
		if (!dzfoot::validateGameStatePacket(raw, data.size())) {
			return;
		}
		std::lock_guard<std::mutex> lock(state_mutex_);
		memcpy(&current_state_, raw, sizeof(current_state_));
		has_new_state_ = true;
	}

	void set_match_setup_bytes(PackedByteArray data) {
		if (data.size() < (int)sizeof(dzfoot::MatchSetupPacket)) {
			return;
		}
		const uint8_t* raw = data.ptr();
		if (!dzfoot::validateMatchSetupPacket(raw, data.size())) {
			return;
		}
		std::lock_guard<std::mutex> lock(state_mutex_);
		memcpy(&match_setup_, raw, sizeof(match_setup_));
		has_match_setup_ = true;
	}

	void set_match_event_bytes(PackedByteArray data) {
		if (data.size() < (int)sizeof(dzfoot::MatchEventPacket)) {
			return;
		}
		const uint8_t* raw = data.ptr();
		if (!dzfoot::validateMatchEventPacket(raw, data.size())) {
			return;
		}
		std::lock_guard<std::mutex> lock(state_mutex_);
		memcpy(&last_event_, raw, sizeof(last_event_));
		has_new_event_ = true;
	}

	int get_tick() const {
		return current_state_.tick;
	}

	Vector3 get_ball_position() const {
		return Vector3(
			current_state_.ball.pos[0],
			current_state_.ball.pos[1],
			current_state_.ball.pos[2]
		);
	}

	Vector3 get_player_position(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return Vector3();
		return Vector3(
			current_state_.players[idx].pos[0],
			current_state_.players[idx].pos[1],
			current_state_.players[idx].pos[2]
		);
	}

	float get_player_rotation_y(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return 0.0f;
		return current_state_.players[idx].rotY;
	}

	int get_player_anim(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return 0;
		return current_state_.players[idx].anim;
	}

	Vector3 get_official_position(int idx) const {
		if (idx < 0 || idx >= 3) return Vector3();
		return Vector3(
			current_state_.officials[idx].pos[0],
			current_state_.officials[idx].pos[1],
			current_state_.officials[idx].pos[2]
		);
	}

	float get_official_rotation_y(int idx) const {
		if (idx < 0 || idx >= 3) return 0.0f;
		return current_state_.officials[idx].rotY;
	}

	Vector3 get_camera_position() const {
		return Vector3(
			current_state_.camera.pos[0],
			current_state_.camera.pos[1],
			current_state_.camera.pos[2]
		);
	}

	Quaternion get_camera_rotation() const {
		return Quaternion(
			current_state_.camera.rot[0],
			current_state_.camera.rot[1],
			current_state_.camera.rot[2],
			current_state_.camera.rot[3]
		);
	}

	float get_camera_fov() const {
		return current_state_.camera.fov;
	}

	Vector3 get_ball_velocity() const {
		return Vector3(
			current_state_.ball.vel[0],
			current_state_.ball.vel[1],
			current_state_.ball.vel[2]
		);
	}

	int get_player_flags(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return 0;
		return current_state_.players[idx].flags;
	}

	int get_player_team(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return 0;
		return current_state_.players[idx].team;
	}

	int get_player_role(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return 0;
		return current_state_.players[idx].role;
	}

	Vector3 get_player_velocity(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return Vector3();
		return Vector3(
			current_state_.players[idx].vel[0],
			current_state_.players[idx].vel[1],
			current_state_.players[idx].vel[2]
		);
	}

	float get_player_tired(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return 0.0f;
		return current_state_.players[idx].tiredFactor;
	}

	int get_score(int team) const {
		if (team < 0 || team > 1) return 0;
		return current_state_.score[team];
	}

	float get_timer() const {
		return current_state_.timer;
	}

	int get_game_flags() const {
		return current_state_.gameFlags;
	}

	bool has_new_state() const {
		return has_new_state_;
	}

	void consume_state() {
		has_new_state_ = false;
	}

	bool has_match_setup() const {
		return has_match_setup_;
	}

	bool has_new_event() const {
		return has_new_event_;
	}

	void consume_event() {
		has_new_event_ = false;
	}

	String get_team_a_name() const {
		return String(match_setup_.teamAName);
	}

	String get_team_b_name() const {
		return String(match_setup_.teamBName);
	}

	int get_player_count() const {
		return match_setup_.playerCount;
	}

	int get_match_player_team(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return 0;
		return match_setup_.players[idx].team;
	}

	String get_match_player_name(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return String();
		return String(match_setup_.players[idx].lastName);
	}

	int get_match_player_number(int idx) const {
		if (idx < 0 || idx >= dzfoot::DZ_MAX_PLAYERS) return 0;
		return match_setup_.players[idx].playerNumber;
	}

	// === Input ===
	void set_input_direction(float x, float z) {
		input_dir_x_ = x;
		input_dir_z_ = z;
	}

	void set_input_buttons(int buttons) {
		input_buttons_ = (uint16_t)buttons;
	}

	void set_active_player(int idx, int team) {
		input_player_idx_ = (uint8_t)idx;
		input_team_ = (uint8_t)team;
	}

	PackedByteArray get_input_bytes() {
		dzfoot::PlayerInputPacket pkt;
		memset(&pkt, 0, sizeof(pkt));
		pkt.header.magic = dzfoot::DZ_MAGIC;
		pkt.header.version = dzfoot::DZ_PROTOCOL_VERSION;
		pkt.header.type = dzfoot::PACKET_PLAYER_INPUT;
		pkt.header.size = sizeof(pkt);
		pkt.header.flags = 0;
		pkt.dirX = input_dir_x_;
		pkt.dirZ = input_dir_z_;
		pkt.buttons = input_buttons_;
		pkt.playerIdx = input_player_idx_;
		pkt.team = input_team_;
		pkt.clientTick = 0;  // will be set by caller
		pkt.clientTimeUs = 0;  // will be set by caller

		PackedByteArray out;
		out.resize(sizeof(pkt));
		memcpy(out.ptrw(), &pkt, sizeof(pkt));
		return out;
	}
};

// === GDExtension entry point ===
GDExtensionBool dzfoot_gdext_entry(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                    GDExtensionClassLibraryPtr p_library,
                                    GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer([](godot::ModuleInitializationLevel p_level) {
		if (p_level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
			ClassDB::register_class<DZFootBridge>();
		}
	});

	init_obj.register_terminator([](godot::ModuleInitializationLevel p_level) {
	});

	init_obj.set_minimum_library_version(4, 2);

	return init_obj.init();
}
