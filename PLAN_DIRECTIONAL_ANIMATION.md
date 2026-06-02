# Plan Détaillé : Animation Directionnelle — DZFoot Android

**Objectif** : Passer de 17 clips statiques à un système de sélection d'animations
directionnelles avec blending, comme le fait le desktop GameplayFootball (296 .anim).

**Estimation** : ~2-3 semaines dev

---

## 1. CONTEXTE TECHNIQUE ACTUEL

### 1.1 Ce que le serveur GF envoie (par joueur, 60 Hz)

```c
struct NetworkPlayerState {   // 48 bytes
    float pos[3], vel[3], dir[3], rotY;
    uint8_t anim;      // AnimId 0-16 (catégorie)
    uint8_t team, role, flags;
    float tiredFactor;
};
```

`anim` = 1 parmi 17 catégories :
```
IDLE=0, WALK=1, RUN=2, SPRINT=3, SHOOT_R=4, SHOOT_L=5,
PASS_SHORT=6, PASS_LONG=7, HEADER=8, TACKLE=9, DRIBBLE=10,
FALL=11, CELEBRATE=12, GK_IDLE=13, GK_DIVE_L=14, GK_DIVE_R=15, GK_CATCH=16
```

`dir[3]` = vecteur direction normalisé. `rotY` = atan2(dir.x, dir.z).

### 1.2 Client Android actuel

Fichier principal : `ARRenderer.cpp` → `PlayerRig::draw()`

- 17 clips GLB embarqués dans `player_base.glb` (anim_00..16)
- **1 clip unique par catégorie** — pas de variante par angle
- Crossfade 200ms via slerp quaternion entre clips
- Heading global : `modelYaw = PI - rotY` tourne le modèle entier
- Modèle : 14 os rigides hiérarchiques (pas de skinning vertex)

**Limitation** : un joueur courant à 45° joue le même clip "run forward"
qu'un joueur courant tout droit. Les jambes ne reflètent pas l'angle réel.

### 1.3 Desktop GF (296 .anim, référence)

Dossier `media/animations/` — 14 catégories avec variantes :
```
movement/     idle(6) walk(12) dribble(5) sprint(8)  → par angle 0°/45°/90°/135°/180°
ballcontrol/  idle(15) walk(19) sprint(9)
pass/         idle(3) walk(6) sprint(4) dribble(1)
shot/         idle(3) walk(4) sprint(6) dribble(1)
highpass/     idle(3) walk(6) sprint(2) dribble(1)
trap/         idle(5) walk(12) sprint(1)
deflect/      idle(13) walk(4) sprint(3)
interfere/    idle(3) walk(2) sprint(2)
sliding/      idle(2) walk(1) sprint(2)
trip/         t1(9) t2(6) t3(3)
celebration/  3
special/      18
templates/    10
```

Le desktop (`humanoidbase.cpp:SelectAnim`) :
1. **CrudeSelection** — filtre par functionType + incomingVelocity + bodyDirection
2. **Scoring multi-critère** — trie par similarité direction, vitesse, pied
3. **PhysicsSmuggle** — ajuste position/rotation pour transitions fluides
4. Résultat : meilleur clip parmi ~10-30 candidats, joué frame par frame

---

## 2. PLAN DE MIGRATION EN 5 ÉTAPES

---

### ÉTAPE 1 : Convertir les .anim Beta2 → format binaire Android

**Fichiers** : `tools/convert_anims.py` → `src/main/assets/directional_anims.bin`

Les .anim sont des XML avec keyframes par os (14 body parts). ~25-80 frames à 100fps.

#### Format binaire proposé

```c
// Fichier header
struct DirAnimFileHeader {
    uint32_t magic;         // 'DZAN'
    uint16_t version;       // 1
    uint16_t animCount;     // ~296
};

// Par animation
struct DirAnimMeta {
    uint8_t  categoryId;    // AnimId (0-16)
    uint8_t  angleQuant;    // 0=0°, 1=45°, 2=90°, 3=135°, 4=180°
    uint8_t  velocityIn;    // 0=idle, 1=walk, 2=dribble, 3=sprint
    uint8_t  velocityOut;
    uint8_t  foot;          // 0=right, 1=left
    uint8_t  flags;         // bit0=baseanim, bit1=transition
    uint16_t frameCount;
    float    duration;      // secondes
    uint32_t dataOffset;    // offset vers frames dans le fichier
};

// Données animation : frameCount × 14 bones × BoneFrame
struct BoneFrame {
    float rotation[4]; // quaternion xyzw
    float position[3]; // translation
};
// → 28 bytes/bone/frame × 14 bones × ~50 frames = ~19.6 KB/anim
// Total ~296 anims ≈ 5.8 MB (acceptable pour mobile)
```

#### Script Python `tools/convert_anims.py`

```
Entrée : media/animations/**/*.anim (XML)
Sortie : directional_anims.bin

1. Parser chaque .anim → extraire frames, bodypart quaternions/positions
2. Déduire metadata du chemin :
   - "movement/walk/045.anim"     → cat=WALK, angle=45, velIn=walk
   - "ballcontrol/sprint/000.anim"→ cat=DRIBBLE, angle=0, velIn=sprint
   - "shot/walk/090_left_decel.anim" → cat=SHOOT_L, angle=90, velIn=walk
3. Mapper les 14 body parts desktop vers les 14 os GLB :
   Desktop: player,body,middle,neck,left_thigh,right_thigh,left_knee,
            right_knee,left_ankle,right_ankle,left_shoulder,right_shoulder,
            left_elbow,right_elbow
   GLB:     identique (même noms, vérifié)
4. Écrire le binaire, log stats par catégorie
```

**Estimation** : 2-3 jours

---

### ÉTAPE 2 : Loader C++ pour directional_anims.bin

**Fichier** : `src/main/cpp/DirectionalAnimBank.h/.cpp`

```cpp
struct DirAnimClip {
    uint8_t  category, angleQuant, velIn, velOut, foot, flags;
    uint16_t frameCount;
    float    duration;
    // Frames stockées en mémoire contigüe
    const BoneFrame* frames; // frameCount × 14
};

class DirectionalAnimBank {
public:
    bool load(const char* assetPath); // parse header + mmap ou malloc

    // Requête : retourne les clips qui matchent
    struct Query {
        uint8_t category;       // AnimId du serveur
        float   relAngleDeg;    // angle relatif mouvement vs heading (0-180°)
        uint8_t velocityIn;     // vitesse actuelle
    };

    // Retourne le meilleur clip (ou nullptr si aucun match)
    const DirAnimClip* select(const Query& q) const;

    // Retourne les N meilleurs pour blending avancé (futur)
    int selectTop(const Query& q, const DirAnimClip** out, int maxN) const;

private:
    std::vector<DirAnimClip> clips_;
    // Index par catégorie pour accès O(1)
    std::vector<int> categoryIndex_[17]; // categoryIndex_[cat] = {clipIdx...}
};
```

**Algorithme `select()`** :
```
1. Filtrer clips par category == query.category
2. Filtrer par velocityIn compatible (exact ou ±1 step)
3. Scorer chaque clip : score = -|clip.angle - query.relAngle|
4. Retourner le clip avec le meilleur score
```

**Estimation** : 1-2 jours

---

### ÉTAPE 3 : Calcul de l'angle relatif par joueur

**Fichier** : `ARRenderer.cpp` → `renderPlayers()`

L'angle relatif = différence entre la direction de mouvement et le heading du corps.

```cpp
// Données du serveur :
float rotY;        // heading du corps (atan2(dir.x, dir.z))
float vel[3];      // vel[0]=length, vel[1]=width (env_coord/tick)

// Calcul de l'angle de mouvement
float moveAngle = std::atan2(vel[0], vel[1]); // direction de déplacement
float relAngle  = rotY - moveAngle;           // angle relatif

// Normaliser en [0, 180] degrés (symétrie gauche/droite gérée par miroir)
relAngle = std::fmod(std::fabs(relAngle), 2*PI);
if (relAngle > PI) relAngle = 2*PI - relAngle;
float relAngleDeg = relAngle * 180.0f / PI;

// Quantifier pour la query
DirectionalAnimBank::Query q;
q.category = serverAnimId;
q.relAngleDeg = relAngleDeg;
q.velocityIn  = speedToVelocityEnum(speed);
```

**Variables déjà disponibles** dans `renderPlayers` : `vx`, `vz`, `rotY`, `rawAnim`.

**Estimation** : 0.5 jour

---

### ÉTAPE 4 : Intégrer le sampling dans PlayerRig::draw()

**Fichier** : `ARRenderer.cpp` → `PlayerRig::draw()`

Actuellement `PlayerRig::draw` sample un clip GLB embarqué. Il faut ajouter
un chemin alternatif qui sample un `DirAnimClip` depuis la banque.

```cpp
void PlayerRig::draw(..., const DirAnimClip* dirClip) {
    // Si dirClip != nullptr, utiliser ses frames au lieu du clip GLB
    if (dirClip) {
        float normalizedTime = fmod(time, dirClip->duration) / dirClip->duration;
        int frame = (int)(normalizedTime * dirClip->frameCount);
        frame = clamp(frame, 0, dirClip->frameCount - 1);

        // Lire les 14 BoneFrames pour ce frame
        const BoneFrame* bf = &dirClip->frames[frame * 14];
        for (int bone = 0; bone < 14; ++bone) {
            memcpy(&curR[bone * 4], bf[bone].rotation, 4 * sizeof(float));
            memcpy(&curT[bone * 3], bf[bone].position, 3 * sizeof(float));
        }
    } else {
        // Fallback : clip GLB embarqué (code actuel)
        // ...sampleSampler()...
    }
    // Le reste (blending, globalMats, rendering) est identique
}
```

**Points d'attention** :
- L'ordre des os dans les .anim desktop est le même que dans le GLB (vérifié)
- Les quaternions desktop sont dans le même repère que les GLB (à valider — peut nécessiter une conversion de signe sur Y ou W)
- Le crossfade existant (200ms slerp) fonctionne tel quel
- Le mirroring gauche/droite : pour angles > 0, si le clip est "045" et le joueur va à -45°, flip left↔right bones

**Estimation** : 2-3 jours

---

### ÉTAPE 5 : Enrichir le protocole serveur (optionnel, V2)

Actuellement le serveur envoie 1 `anim` (catégorie). Pour une sélection
encore plus fidèle, le serveur pourrait envoyer des infos supplémentaires :

```c
// Extension possible de NetworkPlayerState (V2)
uint8_t  bodyAngleQuant;   // angle du corps vs direction mouvement (0-7, pas de 45°)
uint8_t  footState;        // 0=right, 1=left (pied d'appui actuel)
uint8_t  animSubId;        // variante d'anim spécifique (pour actions précises)
```

Cela permettrait au client de matcher exactement l'animation que le serveur
a sélectionnée côté desktop. **Sans cette extension**, le client fait sa propre
sélection basée sur `vel` + `rotY` — suffisant pour movement/dribble/sprint.

**Estimation** : 1 jour serveur + 0.5 jour client

---

## 3. FICHIERS À MODIFIER/CRÉER

### Nouveaux fichiers
| Fichier | Description |
|---------|-------------|
| `tools/convert_anims.py` | Script conversion .anim XML → .bin |
| `src/main/assets/directional_anims.bin` | Banque d'animations compilée |
| `src/main/cpp/DirectionalAnimBank.h` | Header : structures + classe |
| `src/main/cpp/DirectionalAnimBank.cpp` | Implémentation loader + sélecteur |

### Fichiers existants à modifier
| Fichier | Modification |
|---------|-------------|
| `ARRenderer.h` | Ajouter membre `DirectionalAnimBank dirAnimBank_` |
| `ARRenderer.cpp init()` | Charger `directional_anims.bin` |
| `ARRenderer.cpp renderPlayers()` | Calculer relAngle, appeler `dirAnimBank_.select()` |
| `ARRenderer.cpp PlayerRig::draw()` | Ajouter paramètre `const DirAnimClip*`, sampler alternatif |
| `CMakeLists.txt` | Ajouter `DirectionalAnimBank.cpp` aux sources |

---

## 4. DONNÉES CLÉ POUR LE DÉVELOPPEUR

### 4.1 Correspondance os desktop ↔ GLB

```
Index  Desktop Name      GLB Node Name     GLB Node Index
0      player            player            0
1      body              body              1
2      middle            middle            2
3      neck              neck              3
4      left_thigh        left_thigh        4
5      right_thigh       right_thigh       5
6      left_knee         left_knee         6
7      right_knee        right_knee        7
8      left_ankle        left_ankle        8
9      right_ankle       right_ankle       9
10     left_shoulder     left_shoulder     10
11     right_shoulder    right_shoulder    11
12     left_elbow        left_elbow        12
13     right_elbow       right_elbow       13
```

### 4.2 Mapping catégorie → dossier .anim

```
IDLE(0)       → movement/idle/
WALK(1)       → movement/walk/
RUN(2)        → movement/dribble/  (dribble=jog dans GF)
SPRINT(3)     → movement/sprint/
SHOOT_R(4)    → shot/
SHOOT_L(5)    → shot/ (variante left foot)
PASS_SHORT(6) → pass/
PASS_LONG(7)  → highpass/
HEADER(8)     → interfere/ ou deflect/ (selon contexte)
TACKLE(9)     → sliding/
DRIBBLE(10)   → ballcontrol/
FALL(11)      → trip/
CELEBRATE(12) → celebration/
GK_IDLE(13)   → special/ (GK specific)
GK_DIVE_L(14) → special/
GK_DIVE_R(15) → special/
GK_CATCH(16)  → special/
```

### 4.3 Convention angle dans les .anim

Le nom du fichier encode l'angle de mouvement relatif au heading :
- `000` = droit devant (0°)
- `045` = 45° à droite
- `090` = latéral (90°)
- `135` = 135° (quasi arrière)
- `180` = marche arrière

Suffixes : `_accel` (accélération), `_decel` (décélération), `_left` (pied gauche)

### 4.4 Coordinate system

```
GF interne : X=longueur, Y=largeur (Y_FIELD_SCALE=-83.6, INVERSÉ)
Android scene : X=longueur, Y=hauteur, Z=largeur (Z = -Y_interne)
rotY serveur = atan2(dir.X, dir.Y_interne)
Scene heading = PI - rotY  (correction Y inversion)
```

### 4.5 Code existant à réutiliser

- `sampleSampler()` — interpolation linéaire/step sur keyframes
- `quatSlerpLocal()` — slerp quaternion pour blending
- `composeTRS()` — compose T*R*S en matrice 4×4
- `PlayerRig::nodes[i].parentIndex` — hiérarchie os pour globalMat
- `PlayerAnimState::play()/update()` — machine à état crossfade

---

## 5. TESTS ET VALIDATION

### 5.1 Validation conversion

```python
# Dans convert_anims.py, ajouter un mode --validate qui :
# 1. Charge le .bin généré
# 2. Pour chaque clip, vérifie que les quaternions sont unitaires (|q| ≈ 1.0)
# 3. Vérifie que chaque catégorie a au moins 1 clip
# 4. Affiche stats : min/max frames, durées, angles couverts
```

### 5.2 Validation runtime

Ajouter un log diagnostic dans `renderPlayers` (déjà conditionné par `shouldLog`) :
```
[dirAnim] P0 cat=WALK relAngle=43.2° velIn=walk → clip "walk_045" frames=50
```

### 5.3 Validation visuelle

- Un joueur courant à 45° doit avoir les jambes orientées en diagonale
- La transition idle→walk doit être smooth (pas de pop)
- Le joueur qui change de direction doit transitionner vers le clip d'angle le plus proche
- En mode offline, faire tourner le joueur en cercle et vérifier que les clips changent

---

## 6. PRIORITÉ DE MIGRATION

### Phase 1 (impact maximal, effort minimal) — MOVEMENT uniquement
Convertir seulement `movement/` (31 anims : idle+walk+dribble+sprint × angles).
Cela couvre ~80% du temps de jeu visible.

### Phase 2 — BALLCONTROL + PASS + SHOT
Ajouter les catégories les plus visibles lors des actions (43 + 14 + 14 anims).

### Phase 3 — Toutes les catégories
Trap, deflect, interfere, sliding, trip, celebration, special (reste ~194 anims).

---

## 7. RISQUES ET MITIGATIONS

| Risque | Impact | Mitigation |
|--------|--------|-----------|
| Format .anim incompatible | Conversion échoue | Parser 2-3 fichiers manuellement d'abord, valider structure |
| Quaternions dans repère différent | Joueurs déformés | Comparer visually 1 frame desktop vs 1 frame GLB pour le même os |
| 5.8 MB sur mobile | APK trop gros | Compresser avec zstd (→ ~2 MB), ou charger à la demande |
| Overhead CPU sélection | Frame drop | Index par catégorie → O(~5) comparaisons, négligeable |
| Fallback si clip manquant | Pop visuel | Toujours garder les 17 clips GLB en fallback |
