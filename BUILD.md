# Build Instructions

Fractal only builds for Android (arm64-v8a).

## Android
Prerequisites:
- git
- [Android studio](https://developer.android.com/studio), or the Android SDK with NDK 29.0.14206865 and JDK 21

Instructions:
1. Run `git clone --recursive https://github.com/dfdx047/Fractal`
2. Open the project located in `src/android` in Android Studio
3. Click Build > Make Project

From the command line: `cd src/android && ./gradlew assembleRelease`. The native libraries come from the CMake project in
the repository root; vcpkg (in `dependencies/vcpkg`) builds the third-party libraries for the `arm64-android` triplet on
the first build.

## Updating the source code
1. Run `git pull --recurse-submodules` in the repository root.
2. Then rebuild with the steps above.

## CMake configure flags
The Gradle build passes the CMake flags it needs (see `src/android/app/build.gradle.kts`). These can also be set:

| Flag           | Description                                                    | Default |
|----------------|----------------------------------------------------------------|---------|
| CEMU_CXX_FLAGS | Flags passed straight to the compiler, e.g. `-Wall`            | ""      |
| ENABLE_CUBEB   | Enable the cubeb audio backend                                 | ON      |
| ENABLE_LIBUSB  | Enable libusb (the nsyshid backend for USB toys like portals) | ON      |
| ENABLE_VCPKG   | Use the vcpkg package manager to obtain dependencies           | ON      |
