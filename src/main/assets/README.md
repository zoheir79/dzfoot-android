# DZFoot Android Assets

## marker.jpg
- **Format**: JPEG, 24-bit color
- **Size**: A4 (210mm x 297mm) printed at 300 DPI = 2480x3508 pixels
- **Physical width in AR**: 0.21m (short edge)
- **Content**: High-contrast geometric pattern (checkerboard or QR-like)
- **Purpose**: ARCore Augmented Image anchor

Generate with: any ArUco marker generator or custom high-contrast image.

## sounds/
| File | Usage | Trigger |
|---|---|---|
| crowd_loop.ogg | Background stadium ambience | Match start |
| crowd_goal.ogg | Crowd cheer on goal | Event GOAL |
| crowd_boo.ogg | Crowd reaction negative | Event FOUL |
| whistle_kickoff.ogg | Start of play | Event KICKOFF |
| whistle_halftime.ogg | Half time | Event HALFTIME |
| whistle_fulltime.ogg | Match end | Event FULLTIME |
| whistle_foul.ogg | Foul whistle | Event FREEKICK/PENALTY |
| kick.ogg | Ball kick | anim_id SHOOT_R/L |
| pass.ogg | Ball pass | anim_id PASS_SHORT |

Format: Ogg Vorbis, 44.1kHz, mono or stereo.

## models/
| File | Description | Polygons target |
|---|---|---|
| player.glb | Base player mesh (Mixamo rig) | ~3000 (Standard) / ~6000 (High) |
| goalkeeper.glb | Goalkeeper variant | ~3000 |
| ball.glb | Football sphere | ~500 |
| pitch.glb | Ground plane with markings | ~10000 |
| goal.glb | Goal posts + net | ~4000 |

Format: glTF 2.0 binary (.glb) with embedded buffers.
Rig: Mixamo-compatible skeleton for animation blending.
