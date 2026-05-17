# DZFoot — ARCore Emulator Setup (Windows PC)

## Prérequis

| Outil | État |
|---|---|
| Docker Desktop | ✅ Lancé |
| Android Studio | À installer si pas déjà fait |
| Émulateur Android API 28+ | À créer |
| ARCore SDK émulateur | À installer |
| Webcam PC | Utilisée comme caméra AR |

---

## 1. Installer Android Studio

```powershell
# Télécharger depuis https://developer.android.com/studio
# Ou via winget
winget install Google.AndroidStudio
```

Relancer le PC après installation.

---

## 2. Créer un émulateur avec ARCore

Dans Android Studio :

```
Tools → Device Manager → Create Device
→ Phone → Pixel 7
→ System Image → API 34 (Android 14) x86_64
   ⚠️ Sélectionner "Google APIs" (pas Google Play si tu veux root)
→ Finish
```

**Activer la caméra PC dans l'émulateur** :
```
Device Manager → ▼ (dropdown sur l'émulateur) → Show Advanced Settings
→ Camera:
   - Front: Webcam0
   - Back: Webcam0  ← ta webcam PC
```

---

## 3. Installer ARCore dans l'émulateur

Télécharger l'APK ARCore pour émulateur :

```powershell
# Dans un terminal PowerShell
mkdir C:\temp\arcore
cd C:\temp\arcore

# Télécharger ARCore APK émulateur (x86_64)
# URL officielle Google (vérifier la dernière version)
Invoke-WebRequest -Uri "https://github.com/google-ar/arcore-android-sdk/releases/download/1.42.0/Google_Play_Services_for_AR_1.42.0_x86_for_emulator.apk" -OutFile "arcore.apk" 2>$null

# Ou manuellement depuis: https://github.com/google-ar/arcore-android-sdk/releases
```

**Installer sur l'émulateur** :
```powershell
# Lancer l'émulateur d'abord
# Puis:
adb install C:\temp\arcore\arcore.apk
```

Vérifier :
```powershell
adb shell pm list packages | findstr com.google.ar.core
# → com.google.ar.core
```

---

## 4. Créer le marqueur AR

DZFoot utilise **Augmented Images** (marqueur image, pas surface plane).

### Option A : Générer un marqueur ArUco

```python
# marker_generator.py
import cv2
import cv2.aruco as aruco

# ArUco 5x5, ID 42
dictionary = aruco.getPredefinedDictionary(aruco.DICT_5X5_50)
marker_size = 700  # pixels

marker_image = aruco.generateImageMarker(dictionary, 42, marker_size)
cv2.imwrite("marker.jpg", marker_image)
print("marker.jpg créé (700x700 pixels)")
```

Exécuter :
```powershell
pip install opencv-python opencv-contrib-python
python marker_generator.py
```

### Option B : Utiliser une image existante

N'importe quelle image avec suffisamment de détails fonctionne. Formats supportés : PNG, JPG.

**Copier dans le projet** :
```powershell
copy marker.jpg d:\DZFoot\repos\dzfoot-android\src\main\assets\marker.jpg
```

**IMPORTANT** : L'image doit être référencée dans `assets/imgdb/imgdb.imgdb` (ARCore Augmented Image Database). Le format est un fichier binaire spécifique à ARCore.

Générer la database :
```kotlin
// Dans MainActivity.kt ou un script utilitaire
val bitmap = BitmapFactory.decodeStream(assets.open("marker.jpg"))
val augmentedImageDatabase = AugmentedImageDatabase(session)
augmentedImageDatabase.addImage("marker", bitmap, 0.2f) // 20cm width
```

Ou utiliser l'outil en ligne de commande `arcoreimg` (inclus dans ARCore SDK).

---

## 5. Configurer l'émulateur pour AR

**Lancer l'émulateur avec GPU** :
```
Device Manager → Play (▶) sur le Pixel 7
→ Settings → Advanced → GPU: Hardware
```

**Autoriser la caméra** :
Dans l'émulateur Android :
```
Settings → Apps → Permissions → Camera → ALLOW
```

---

## 6. Lancer le backend + port forward

Dans un terminal PowerShell (en parallèle) :

```powershell
# 1. Backend
cd d:\DZFoot\repos\dzfoot-backend
docker-compose -f docker-compose.dev.yml up -d

# 2. Port forward ADB
adb reverse tcp:8080 tcp:8080
adb reverse tcp:8002 tcp:8002  # Session direct (optionnel)

# 3. Vérifier
adb devices
# → emulator-5554   device
```

---

## 7. Builder et installer l'APK

```powershell
cd d:\DZFoot\repos\dzfoot-android

# Build debug
.\gradlew assembleDebug

# Install sur émulateur
adb install .\app\build\outputs\apk\debug\app-debug.apk
```

Ou directement depuis Android Studio :
```
Run → Run 'app'  (Shift+F10)
→ Sélectionner l'émulateur Pixel 7
```

---

## 8. Tester l'AR

**Dans l'émulateur** :
1. Ouvrir l'app DZFoot
2. Autoriser la caméra quand demandé
3. **Afficher le marqueur** sur ton écran PC :
   - Ouvrir `marker.jpg` dans un viewer
   - Ou imprimer sur papier A4
4. **Pointer la webcam** vers le marqueur :
   - L'émulateur utilise ta webcam PC comme "caméra arrière"
   - Tu dois physiquement montrer le marqueur à ta webcam

**Astuce** : Si tu n'as pas de webcam physique, utiliser la **Virtual Scene** d'Android Emulator :
```
Émulateur → ⋮ (Extended Controls) → Camera → Virtual Scene
→ Load une image contenant le marqueur
```

---

## 9. Workflow dev quotidien

```powershell
# Terminal 1: Backend
cd d:\DZFoot\repos\dzfoot-backend
docker-compose -f docker-compose.dev.yml up -d

# Terminal 2: Android
cd d:\DZFoot\repos\dzfoot-android
adb reverse tcp:8080 tcp:8080
# Modifier code → Android Studio Run (Shift+F10)

# Terminal 3: Logs
cd d:\DZFoot\repos\dzfoot-backend
docker-compose -f docker-compose.dev.yml logs -f session
```

---

## Dépannage

### "ARCore not supported on this device"
```powershell
# Réinstaller ARCore
adb uninstall com.google.ar.core
adb install C:\temp\arcore\arcore.apk
```

### "Camera permission denied"
Dans l'émulateur : `Settings → Apps → DZFoot → Permissions → Camera → Allow`

### Marqueur non détecté
- Vérifier que `marker.jpg` est dans `assets/`
- Vérifier la taille physique déclarée (20cm = 0.2f)
- Image trop petite ou trop floue → en faire une plus grande

### Émulateur très lent
- Activer `Hardware - GLES 2.0` dans les settings de l'AVD
- Augmenter RAM de l'émulateur à 4GB
- Fermer Docker Desktop temporairement si RAM insuffisante

### "Connection refused" vers backend
```powershell
adb reverse --list
# Si vide:
adb reverse tcp:8080 tcp:8080
```

---

## Limitations émulateur vs vrai device

| Feature | Émulateur | Vrai device |
|---|---|---|
| Tracking marqueur | ✅ OK | ✅ OK |
| Surface detection | ❌ Limité | ✅ OK |
| Depth API | ❌ Non | ✅ OK (certains devices) |
| Performance | ⚠️ Lent | ✅ 30fps |
| Caméra AR | Webcam PC | Caméra device |

**Recommandation** : Développer la logique sur émulateur, tester la performance sur vrai device.
