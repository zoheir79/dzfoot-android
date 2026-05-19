# DZFoot Android — Contexte pour IA

## 🎯 Vue d'ensemble
Client Android **AR Multiplayer Football** pour le projet DZFoot.
Le joueur scanne un marqueur (AR) pour afficher un terrain 3D, rejoint un match en ligne via matchmaking, et joue en temps réel avec un autre joueur.

---

## 🏗 Architecture

```
┌─────────────────────────────────────────┐
│           Android Client                │
│  ┌─────────┐  ┌──────────┐  ┌─────────┐ │
│  │  ARCore │  │ LiveKit  │  │  UI     │ │
│  │  (AR)   │  │(Realtime)│  │Compose  │ │
│  └────┬────┘  └────┬─────┘  └─────────┘ │
│       │            │                     │
│       └────────────┘                     │
│              │                           │
│         ┌────┴────┐                      │
│         │ Backend  │                      │
│         │ API      │                      │
│         └──────────┘                      │
└─────────────────────────────────────────┘
```

### Composants clés
- **ARCore** : Détection marqueur, affichage terrain 3D
- **LiveKit SDK** : Communication temps réel (WebRTC data channels)
- **Jetpack Compose** : UI moderne déclarative
- **OkHttp/Retrofit** : Appels API backend

---

## 🔄 Flux utilisateur (PoC V1)

```
1. Lancer l'app
2. Scan marqueur AR (camera/webcam)
3. Terrain 3D apparaît
4. Login/Register (compte DZFoot)
5. "Rechercher un match" → Matchmaking
6. Appariement trouvé → Session crée
7. Connexion LiveKit (room ID)
8. Jouer ! (envoi inputs, réception état jeu)
9. Match termine → Résultats
```

---

## 🔌 API Backend (localhost:8080 via nginx)

| Service | Endpoint | Description |
|---------|----------|-------------|
| Account | `POST /auth/register` | Créer compte |
| Account | `POST /auth/login` | Connexion (retourne JWT) |
| Account | `GET /profile` | Profil utilisateur |
| Matchmaking | `POST /queue/join` | Rejoindre file d'attente |
| Matchmaking | `POST /queue/leave` | Quitter file |
| Session | `POST /internal/create-match` | Créer match (interne) |
| Stats | `GET /leaderboard` | Classement |
| Stats | `GET /matches/{id}` | Détail match |
| Catalog | `GET /teams` | Liste équipes |
| Catalog | `GET /stadiums` | Liste stades |

**Healthchecks** : `/health` sur chaque service.

---

## 📡 Communication temps réel (LiveKit)

```
Room : "match-{uuid}"

Data channels :
- client → serveur : inputs joueur (touches, direction)
- serveur → client : GameState (positions, score, temps)
- serveur → client : Events (but, mi-temps, fin)
```

Format GameState (JSON) :
```json
{
  "tick": 1234,
  "players": [
    {"id": "p1", "x": 0.5, "y": 0.2, "team": "A"},
    {"id": "p2", "x": -0.5, "y": 0.2, "team": "B"}
  ],
  "ball": {"x": 0.1, "y": 0.0, "z": 0.1},
  "score_a": 1,
  "score_b": 0,
  "time_s": 45.2
}
```

---

## � Anti-Lag Réseau (Spec V2)

Pour garantir la fluidité jusqu'à **150 ms de ping** (couvre WiFi et 4G), le client implémente les 4 techniques suivantes.

### 1. Client-Side Prediction (ton joueur)
- Ton input est appliqué **immédiatement** à l'écran sans attendre le serveur.
- Le client simule en local le résultat de l'input.
- Quand l'état serveur arrive, le client recalcule son état depuis cet état serveur + les inputs non encore confirmés (**server reconciliation**).

### 2. Interpolation des entités (adversaire & IA)
- L'adversaire est affiché avec **~100 ms de retard** mais de façon parfaitement fluide.
- Le client interpole les positions entre les 2 derniers états reçus (`GameState`).
- Résultat : mouvement fluide même avec des updates à 20 Hz.

### 3. Server Reconciliation
- Le serveur est autoritaire.
- Si la prédiction locale diverge de la réalité serveur, le client recalcule son état depuis le dernier `GameState` confirmé + les inputs non encore confirmés.
- Correction appliquée en douceur (pas de téléportation, pas de "rubber-banding").

### 4. Dead Reckoning (balle)
- Entre deux `GameState`, le client extrapole la position de la balle selon sa dernière vélocité connue : `pos + vel × deltaT`.
- Si le serveur envoie une correction, interpolation douce vers la vraie position.
- La balle ne s'arrête jamais brutalement entre deux paquets.

### Fréquences optimales
- **Inputs joueur** : envoyés à **60 Hz** (chaque frame) pour fraîcheur maximale.
- **GameState serveur** : broadcast à **20 Hz** (~50 ms). Suffisant avec interpolation. Un paquet compressé ~300 octets → ~6 KB/s par match.
- **Topic LiveKit** : `"in"` (inputs), `"gs"` (GameState), `"ev"` (events). Mode **unreliable** (pas de retransmission, fraîcheur prioritaire).

### Indicateur qualité réseau
- Ping mesuré via round-trip `GameState` ou heartbeat dédié.
- **Warning** affiché si ping > 300 ms.
- **Déconnexion automatique** si ping > 500 ms.

---

## �🎨 Assets requis

| Fichier | Chemin | Description |
|---------|--------|-------------|
| `marker.jpg` | `src/main/assets/` | Marqueur AR ArUco 4x4 |
| `stadium.obj` | `src/main/assets/` | Modèle 3D terrain (temporaire : cube) |
| `ball.obj` | `src/main/assets/` | Modèle 3D ballon (temporaire : sphère) |
| `player.obj` | `src/main/assets/` | Modèle 3D joueur (temporaire : capsule) |

---

## ⚙️ Configuration dev

### Gradle
- `compileSdk 34`
- `minSdk 24`
- `targetSdk 34`
- Java 17 / Kotlin 1.9.22

### Dépendances clés
```gradle
implementation 'androidx.appcompat:appcompat:1.6.1'
implementation 'androidx.lifecycle:lifecycle-runtime-ktx:2.7.0'
implementation 'io.livekit:livekit-android:2.4.0'
```

### Émulateur
- **Device** : Pixel 7 API 34 x86_64
- **Camera** : Webcam0 (caméra PC)
- **ARCore** : APK `Google_Play_Services_for_AR_1.42.0_x86_64.apk`
- **WHPX** activé (pas AEHD, incompatible avec Docker WSL2)

### Port forward
```bash
adb reverse tcp:8080 tcp:8080
```

---

## 🗂 Structure du projet

```
dzfoot-android/
├── build.gradle                    # Config build (plugins, deps)
├── settings.gradle                 # Repos (google, mavenCentral, jitpack)
├── src/main/
│   ├── AndroidManifest.xml         # Permissions (CAMERA, INTERNET)
│   ├── java/com/football/ar/
│   │   ├── MainActivity.kt          # Entry point, ARCore setup
│   │   ├── LiveKitManager.kt        # Connexion temps réel
│   │   ├── GameStateReceiver.kt     # Parse GameState JSON
│   │   ├── MatchmakingViewModel.kt  # Logique file d'attente
│   │   └── ui/                     # Composables Jetpack
│   ├── cpp/                        # GameplayFootball JNI (V2)
│   │   └── CMakeLists.txt
│   └── assets/
│       └── marker.jpg              # Marqueur AR
└── AR_EMULATOR.md                  # Guide setup AR complet
```

---

## 🧪 Test rapide (PoC V1 sans C++)

Le backend utilise un **mock GF Server** (Python) qui :
1. Lance un "match" virtuel (60s)
2. Envoie heartbeat Redis
3. Génère score aléatoire
4. Signale fin du match

Pas besoin de compiler le C++ GameplayFootball pour V1.

---

## 🔗 Repos connexes

| Repo | Rôle | Tech |
|------|------|------|
| `dzfoot-backend` | Microservices API | Python FastAPI, PostgreSQL, Redis |
| `dzfoot-gf-server` | Moteur de jeu C++ | C++, CMake, LiveKit C++ SDK |
| `dzfoot-android` | Ce client | Kotlin, ARCore, LiveKit Android |

---

## 📌 Notes importantes

- Le projet utilise **jitpack.io** (dépendance LiveKit)
- Ne pas activer AEHD (conflit Hyper-V/WHPX/Docker)
- Le marqueur AR doit être imprimé ou affiché à l'écran
- L'app utilise `localhost:8080` en dev (port forward ADB)
