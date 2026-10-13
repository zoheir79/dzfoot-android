extends Control
class_name DZRadar

# Broadcast minimap: pitch outline + player dots + ball + active-player ring.
# `scene` is the DZFootScene instance (untyped to avoid a cyclic class ref).
var scene: Object = null

# Offside trap lines (Godot world X in meters), from the TacticalStatePacket.
# NaN = not available yet.
var offside_trap_a: float = NAN
var offside_trap_b: float = NAN

const PITCH_W := 105.0   # Godot X extent
const PITCH_H := 68.0    # Godot Z extent

func _draw() -> void:
	if scene == null:
		return
	var sz := size
	if sz.x < 10 or sz.y < 10:
		return
	# Background (opaque on software GL — blend canvas variants fail to link there)
	var bg := Color(0.02, 0.22, 0.05, 0.72)
	if scene._software_gl:
		bg.a = 1.0
	draw_rect(Rect2(Vector2.ZERO, sz), bg, true)
	# Pitch markings (inset margin)
	var m := 6.0
	var pw := sz.x - m * 2.0
	var ph := sz.y - m * 2.0
	var line := Color(1, 1, 1, 0.75)
	if scene._software_gl:
		line.a = 1.0
	var lw := 1.0
	draw_rect(Rect2(m, m, pw, ph), line, false, lw)
	draw_line(Vector2(m + pw / 2.0, m), Vector2(m + pw / 2.0, m + ph), line, lw)
	draw_arc(Vector2(m + pw / 2.0, m + ph / 2.0), ph * 0.14, 0, TAU, 24, line, lw)
	# Penalty boxes
	var box_w := ph * (40.32 / PITCH_H)
	var box_d := pw * (16.5 / PITCH_W)
	draw_rect(Rect2(m, m + (ph - box_w) / 2.0, box_d, box_w), line, false, lw)
	draw_rect(Rect2(m + pw - box_d, m + (ph - box_w) / 2.0, box_d, box_w), line, false, lw)
	# Offside trap lines (dashed) — from TacticalStatePacket
	_draw_trap_line(offside_trap_a, Color(0.35, 0.85, 1.0, 1.0), m, pw, ph)
	_draw_trap_line(offside_trap_b, Color(1.0, 0.45, 0.25, 1.0), m, pw, ph)

	# Players: team-colored dots
	for i in range(scene.players.size()):
		var p: Node3D = scene.players[i]
		var uv := _world_to_radar(p.position)
		var pt := Vector2(m + uv.x * pw, m + uv.y * ph)
		var col: Color = scene._team_a_shirt if i < 11 else scene._team_b_shirt
		if i == 0 or i == 11:
			col = Color(0.15, 0.8, 0.75) if i == 0 else Color(0.95, 0.85, 0.1)
		draw_circle(pt, 3.0, col)
		if i == scene._active_player_idx:
			draw_arc(pt, 5.5, 0, TAU, 16, Color.WHITE, 1.5)
	# Officials: black dots
	for i in range(scene.officials.size()):
		var o: Node3D = scene.officials[i]
		var uv := _world_to_radar(o.position)
		draw_circle(Vector2(m + uv.x * pw, m + uv.y * ph), 2.0, Color(0, 0, 0, 1.0))
	# Ball: bright dot
	if scene.ball:
		var uv := _world_to_radar(scene.ball.position)
		draw_circle(Vector2(m + uv.x * pw, m + uv.y * ph), 3.0, Color(1, 1, 0.3))

func _world_to_radar(pos: Vector3) -> Vector2:
	return Vector2(clampf(pos.x / PITCH_W + 0.5, 0.0, 1.0), clampf(pos.z / PITCH_H + 0.5, 0.0, 1.0))

func _draw_trap_line(trap_x: float, col: Color, m: float, pw: float, ph: float) -> void:
	if is_nan(trap_x):
		return
	var u := clampf(trap_x / PITCH_W + 0.5, 0.0, 1.0)
	var x := m + u * pw
	var y := m
	var seg := 5.0
	while y < m + ph - 2.0:
		draw_line(Vector2(x, y), Vector2(x, minf(y + seg, m + ph)), col, 1.2)
		y += seg * 1.7
