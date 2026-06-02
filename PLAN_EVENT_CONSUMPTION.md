# Plan Détaillé : Event Consumption — DZFoot Android

**Objectif** : Consommer les `MatchEventPacket` reçus du serveur GF (via LiveKit topic `"ev"`)
pour déclencher des feedbacks visuels (overlay UI, animations caméra) et sonores (whistle, crowd).

**Estimation** : ~3-5 jours dev

---

## 1. ARCHITECTURE ACTUELLE

### 1.1 Flux existant (déjà câblé, mais pas consommé)

```
Serveur GF → LiveKit data channel (topic "ev", reliable)
  → LiveKitManager.kt : event.topic == "ev"
    → JniBridge.nativeOnGameEvent(data)
      → jni_main.cpp : Java_..._nativeOnGameEvent
        → GameBridge::applyMatchEvent(data, len)
          → valide paquet → pendingEvents_.push_back(ev)
```

**`flushEvents()`** existe et retourne le vecteur + vide la liste.
Mais **personne ne l'appelle**.

### 1.2 EventType enum (déjà défini dans DZFootProtocol.h)

```cpp
enum EventType : uint8_t {
    EVENT_GOAL              = 0,   // ⭐ But marqué
    EVENT_YELLOW_CARD       = 1,   // 🟨 Carton jaune
    EVENT_RED_CARD          = 2,   // 🟥 Carton rouge
    EVENT_SUBSTITUTION      = 3,   // Remplacement
    EVENT_CORNER            = 4,   // Corner
    EVENT_THROW_IN          = 5,   // Touche
    EVENT_FREE_KICK         = 6,   // Coup franc
    EVENT_PENALTY           = 7,   // Penalty
    EVENT_KICK_OFF          = 8,   // Coup d'envoi
    EVENT_END_MATCH         = 9,   // Fin de match
    EVENT_HALF_TIME         = 10,  // Mi-temps
    EVENT_GOAL_KICK         = 11,  // 6m
    EVENT_OFFSIDE           = 12,  // Hors-jeu
    EVENT_FOUL              = 13,  // Faute
    EVENT_POSSESSION_CHANGE = 14,  // Changement possession
    EVENT_SHOT              = 15,  // Tir
    EVENT_PASS              = 16,  // Passe
    EVENT_TACKLE            = 17   // Tacle
};
```

### 1.3 Champs de MatchEventPacket

```cpp
struct MatchEventPacket {
    PacketHeader header;
    uint8_t  eventType;   // EventType enum
    uint8_t  team;        // 0 ou 1
    uint8_t  playerIdx;   // 0-21
    uint8_t  extra;       // contexte additionnel (sous-type)
    float    pos[3];      // position sur le terrain (env_coord)
    uint32_t tick;        // tick serveur
    uint8_t  score[2];    // score après l'événement
    uint8_t  _pad[2];
};
```

### 1.4 Audio existant (câblé mais vide)

- `AudioSystem.kt` : SoundPool initialisé, API `play(name)`, `playLoop(name)`
- `nativeAudioPlay(name)` appelable depuis C++ via JNI
- Fichiers son dans `assets/sounds/` : `ballsound.ogg`, `crowd01.ogg`, `crowd02.ogg`, `goalpost.ogg`, `whistle2.ogg`, `whistle3.ogg`
- **PROBLÈME** : `audioSystem.loadSound()` n'est jamais appelé → aucun son chargé

### 1.5 UI overlay existant

- `scoreText` (TextView, 24sp, blanc, haut-centre) — MAJ depuis `onGameStateReceived`
- `timerText` (TextView, 20sp, blanc, haut-droite) — MAJ depuis `onGameStateReceived`
- `eventText` (TextView, 18sp, jaune, centre) — défini mais **jamais mis à jour**

---

## 2. PLAN EN 4 ÉTAPES

---

### ÉTAPE 1 : Charger les sons au démarrage

**Fichier** : `MainActivity.kt`

Après `audioSystem.init()`, charger les 6 fichiers son :

```kotlin
// Dans onCreate(), après audioSystem.init()
private fun loadSounds() {
    val sounds = listOf(
        "ballsound", "crowd01", "crowd02",
        "goalpost", "whistle2", "whistle3"
    )
    for (name in sounds) {
        try {
            val data = assets.open("sounds/$name.ogg").readBytes()
            audioSystem.loadSound(name, data)
        } catch (e: Exception) {
            Log.w("MainActivity", "Sound $name not found: ${e.message}")
        }
    }
}
```

**Estimation** : 0.5 jour

---

### ÉTAPE 2 : Flush events depuis C++ et remonter vers Kotlin

Il y a 2 approches possibles. **Recommandation : Approche A** (plus simple).

#### Approche A : Flush côté C++ dans nativeOnFrame, callback JNI vers Kotlin

**Fichier** : `jni_main.cpp`

Dans `nativeOnFrame`, après le render, appeler `flushEvents()` et pour chaque
événement, appeler une méthode Kotlin via JNI :

```cpp
// Dans nativeOnFrame, après gRenderer.renderScene(...)
auto events = gGameBridge.flushEvents();
for (const auto& ev : events) {
    // Callback vers Kotlin
    jclass cls = env->GetObjectClass(thiz);
    jmethodID mid = env->GetMethodID(cls, "onMatchEvent", "(IIIIFFF[B)V");
    // ... ou plus simple : sérialiser en byte[] et appeler une seule méthode
}
```

**Méthode recommandée** (plus simple) — passer le byte array brut :

```cpp
// jni_main.cpp, dans nativeOnFrame après render
auto events = gGameBridge.flushEvents();
for (const auto& ev : events) {
    jbyteArray arr = env->NewByteArray(sizeof(dzfoot::MatchEventPacket));
    env->SetByteArrayRegion(arr, 0, sizeof(dzfoot::MatchEventPacket),
                            (const jbyte*)&ev);
    // Appeler JniBridge.onMatchEventFromNative(byte[])
    jclass cls = env->GetObjectClass(thiz);
    jmethodID mid = env->GetMethodID(cls, "onMatchEventFromNative", "([B)V");
    if (mid) env->CallVoidMethod(thiz, mid, arr);
    env->DeleteLocalRef(arr);
}
```

**Fichier** : `JniBridge.kt` — ajouter :

```kotlin
// Appelé depuis C++ (jni_main.cpp) pour chaque événement
fun onMatchEventFromNative(data: ByteArray) {
    // Parse le MatchEventPacket
    if (data.size < 36) return
    val eventType = data[12].toInt() and 0xFF
    val team      = data[13].toInt() and 0xFF
    val playerIdx = data[14].toInt() and 0xFF
    val extra     = data[15].toInt() and 0xFF
    // pos[3] à offset 16 (3 floats, 12 bytes)
    val tick = // offset 28, uint32
    val scoreA = data[32].toInt() and 0xFF
    val scoreB = data[33].toInt() and 0xFF

    // Dispatcher vers MainActivity
    // (nécessite une référence à l'Activity — voir étape 3)
}
```

#### Approche B (alternative) : Tout faire côté Kotlin

Puisque `nativeOnGameEvent` est déjà appelé depuis LiveKitManager, on peut
**aussi** parser l'événement côté Kotlin directement dans LiveKitManager
AVANT de l'envoyer au C++. Cela évite le callback JNI retour.

```kotlin
// LiveKitManager.kt, dans le when "ev"
"ev" -> {
    activity.jni.nativeOnGameEvent(event.data) // toujours envoyer au C++
    activity.handleMatchEvent(event.data)       // + traiter côté Kotlin
}
```

**Recommandation : Approche B** — plus simple, moins de plomberie JNI.

**Estimation** : 0.5 jour

---

### ÉTAPE 3 : Dispatcher les événements (UI + Audio)

**Fichier** : `MainActivity.kt`

```kotlin
fun handleMatchEvent(data: ByteArray) {
    if (data.size < 36) return

    val eventType = data[12].toInt() and 0xFF
    val team      = data[13].toInt() and 0xFF
    val playerIdx = data[14].toInt() and 0xFF
    val scoreA    = data[32].toInt() and 0xFF
    val scoreB    = data[33].toInt() and 0xFF

    // Table de correspondance événement → action
    when (eventType) {
        0  -> onGoal(team, playerIdx, scoreA, scoreB)
        1  -> onYellowCard(team, playerIdx)
        2  -> onRedCard(team, playerIdx)
        3  -> onSubstitution(team, playerIdx)
        4  -> onSetPiece("CORNER", team)
        5  -> onSetPiece("THROW-IN", team)
        6  -> onSetPiece("FREE KICK", team)
        7  -> onSetPiece("PENALTY", team)
        8  -> onKickOff()
        9  -> onEndMatch(scoreA, scoreB)
        10 -> onHalfTime()
        11 -> onSetPiece("GOAL KICK", team)
        12 -> onSetPiece("OFFSIDE", team)
        13 -> onFoul(team, playerIdx)
        // 14-17 = fréquents, pas de feedback UI lourd
    }
}

private fun onGoal(team: Int, playerIdx: Int, scoreA: Int, scoreB: Int) {
    // 1. Audio : sifflet + crowd
    audioSystem.play("whistle2")
    Handler(Looper.getMainLooper()).postDelayed({
        audioSystem.play("crowd01")
    }, 500)

    // 2. UI : grand overlay "GOOOAL!" pendant 3 secondes
    val teamName = if (team == 0) "Team A" else "Team B"
    showEventOverlay("⚽ GOAL! $teamName\n$scoreA - $scoreB", 3000)

    // 3. Mettre à jour le score
    runOnUiThread { scoreText.text = "$scoreA - $scoreB" }
}

private fun onYellowCard(team: Int, playerIdx: Int) {
    audioSystem.play("whistle3")
    showEventOverlay("🟨 YELLOW CARD — Player #$playerIdx", 2000)
}

private fun onRedCard(team: Int, playerIdx: Int) {
    audioSystem.play("whistle3")
    showEventOverlay("🟥 RED CARD — Player #$playerIdx", 3000)
}

private fun onSetPiece(name: String, team: Int) {
    audioSystem.play("whistle2")
    val teamName = if (team == 0) "Team A" else "Team B"
    showEventOverlay("$name — $teamName", 1500)
}

private fun onFoul(team: Int, playerIdx: Int) {
    audioSystem.play("whistle3")
    showEventOverlay("FOUL", 1000)
}

private fun onKickOff() {
    audioSystem.play("whistle2")
    showEventOverlay("KICK OFF!", 2000)
    // Démarrer l'ambiance crowd en boucle
    audioSystem.playLoop("crowd02")
}

private fun onHalfTime() {
    audioSystem.play("whistle2")
    audioSystem.stopLoop()
    showEventOverlay("HALF TIME", 3000)
}

private fun onEndMatch(scoreA: Int, scoreB: Int) {
    audioSystem.play("whistle2")
    audioSystem.stopLoop()
    showEventOverlay("FULL TIME\n$scoreA - $scoreB", 5000)
}

private fun showEventOverlay(text: String, durationMs: Long) {
    runOnUiThread {
        eventText.text = text
        eventText.visibility = View.VISIBLE
        // Fade in
        eventText.alpha = 0f
        eventText.animate().alpha(1f).setDuration(200).start()
        // Auto-hide après durationMs
        Handler(Looper.getMainLooper()).postDelayed({
            eventText.animate().alpha(0f).setDuration(500).withEndAction {
                eventText.visibility = View.GONE
            }.start()
        }, durationMs)
    }
}
```

**Estimation** : 1-2 jours

---

### ÉTAPE 4 : Effets visuels avancés (optionnel, V2)

#### 4a. Replay caméra sur but

Quand un but est marqué, le renderer peut :
1. Sauvegarder les 3 dernières secondes de GameState (circular buffer)
2. Passer en mode "replay" : rejouer le buffer avec caméra dynamique
3. Revenir au live après le replay

**Fichier** : `ARRenderer.cpp` + `GameBridge.cpp`

Nécessite un `CircularBuffer<GameStatePacket, 180>` (3s à 60Hz).
Complexité : ~2-3 jours.

#### 4b. Cercle lumineux sous le porteur du ballon

Déjà partiellement implémenté via `flags & 8` (has_possession) qui brighten le joueur.
Pour un vrai cercle au sol :

```cpp
// Dans renderPlayers, si flags & 8 :
// Dessiner un quad texturé circulaire (billboard horizontal) sous le joueur
drawGroundMarker(worldPos, 0.4f /*radius*/, teamColor);
```

#### 4c. Animation carte (carton jaune/rouge)

Sur réception de EVENT_YELLOW_CARD / EVENT_RED_CARD :
1. Forcer l'anim du joueur fautif vers un clip spécifique (idle ou fall)
2. Afficher un quad 2D texturé (carte jaune/rouge) flottant au-dessus du joueur

Textures disponibles dans Beta2 : `yellowcard.png`, `redcard.png`
(pas encore copiées — à ajouter depuis desktop `media/objects/other/`).

#### 4d. Vibration haptic

```kotlin
private fun vibrate(ms: Long) {
    val vibrator = getSystemService(VIBRATOR_SERVICE) as? Vibrator
    vibrator?.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE))
}

// Appeler dans :
// onGoal → vibrate(500)
// onRedCard → vibrate(300)
// onFoul → vibrate(100)
```

**Estimation V2** : 3-5 jours total

---

## 3. FICHIERS À MODIFIER

### Fichiers existants
| Fichier | Modification |
|---------|-------------|
| `MainActivity.kt` | `loadSounds()`, `handleMatchEvent()`, `onGoal/Card/SetPiece/...()`, `showEventOverlay()` |
| `LiveKitManager.kt` | Ajouter `activity.handleMatchEvent(event.data)` dans le `when "ev"` |
| `AudioSystem.kt` | Aucune modif (API déjà complète) |

### Aucun nouveau fichier nécessaire pour V1

### Nouveaux fichiers V2 (optionnel)
| Fichier | Description |
|---------|-------------|
| `ReplayBuffer.h/.cpp` | Buffer circulaire pour replay caméra |
| `GroundMarker.cpp` | Cercle/halo au sol sous le porteur |

---

## 4. MAPPING SON → ÉVÉNEMENT

| Événement | Son principal | Son secondaire | Durée overlay |
|-----------|--------------|----------------|---------------|
| GOAL | `whistle2` | `crowd01` (500ms delay) | 3s |
| YELLOW_CARD | `whistle3` | — | 2s |
| RED_CARD | `whistle3` | — | 3s |
| CORNER/THROW_IN/FREE_KICK/PENALTY/GOAL_KICK | `whistle2` | — | 1.5s |
| KICK_OFF | `whistle2` | `crowd02` (loop start) | 2s |
| HALF_TIME | `whistle2` | crowd loop stop | 3s |
| END_MATCH | `whistle2` | crowd loop stop | 5s |
| FOUL | `whistle3` | — | 1s |
| OFFSIDE | `whistle2` | — | 1.5s |
| SHOT | `ballsound` | — | — |
| PASS | — | — | — |
| TACKLE | — | — | — |
| POSSESSION_CHANGE | — | — | — |

---

## 5. PARSING BINAIRE KOTLIN — OFFSETS

Le `MatchEventPacket` est de 36 bytes en C++ `#pragma pack(push, 1)` :

```
Offset  Taille  Champ
0       4       magic (uint32)
4       2       version (uint16)
6       2       type (uint16) = PACKET_MATCH_EVENT
8       4       size (uint32)
------- Header (12 bytes) -------
12      1       eventType (uint8)
13      1       team (uint8)
14      1       playerIdx (uint8)
15      1       extra (uint8)
16      12      pos[3] (3 × float LE)
28      4       tick (uint32 LE)
32      1       score[0] (uint8)
33      1       score[1] (uint8)
34      2       _pad
```

Kotlin parsing :
```kotlin
val buf = java.nio.ByteBuffer.wrap(data).order(java.nio.ByteOrder.LITTLE_ENDIAN)
val eventType = buf.get(12).toInt() and 0xFF
val team      = buf.get(13).toInt() and 0xFF
val playerIdx = buf.get(14).toInt() and 0xFF
val extra     = buf.get(15).toInt() and 0xFF
val posX      = buf.getFloat(16)
val posY      = buf.getFloat(20)
val posZ      = buf.getFloat(24)
val tick      = buf.getInt(28)
val scoreA    = buf.get(32).toInt() and 0xFF
val scoreB    = buf.get(33).toInt() and 0xFF
```

---

## 6. TESTS

### 6.1 Test unitaire parsing

```kotlin
// Construire un faux MatchEventPacket GOAL en byte array
// Vérifier que handleMatchEvent parse correctement
// Vérifier que scoreText est mis à jour
```

### 6.2 Test intégration offline

Sans serveur, injecter manuellement un événement :
```kotlin
// Dans onCreate, après un delay de 5 secondes :
Handler(Looper.getMainLooper()).postDelayed({
    val fakeGoal = buildFakeMatchEvent(EVENT_GOAL, team=0, scoreA=1, scoreB=0)
    handleMatchEvent(fakeGoal)
}, 5000)
```

### 6.3 Test avec serveur

1. Lancer un match GF
2. Attendre un but/faute/corner
3. Vérifier dans logcat : `DataReceived topic=ev size=36`
4. Vérifier visuellement : overlay apparaît, son joué, score mis à jour

---

## 7. PRIORITÉ

| Phase | Contenu | Effort |
|-------|---------|--------|
| **V1** | loadSounds + Approche B (parse Kotlin) + dispatcher 6 events majeurs (goal, cards, kickoff, halftime, endmatch, foul) | **2 jours** |
| **V1.5** | Set pieces (corner, throw-in, free kick, penalty, goal kick, offside) + crowd loop | **1 jour** |
| **V2** | Replay caméra, cercle au sol, animation carte, haptic | **3-5 jours** |
