#!/bin/bash
set -e

echo "Building DZFoot Android client..."
cd client/app

# Ensure ARCore is in local.properties (Android Studio does this automatically)
if [ ! -f local.properties ]; then
    echo "sdk.dir=$ANDROID_HOME" > local.properties
fi

./gradlew assembleDebug

echo "APK built: client/app/build/outputs/apk/debug/app-debug.apk"
