# Third-party libraries

This file lists third-party libraries used in the Android source folder,
with their provenance and, when relevant, modifications made to those files.

## com.google.android.vending.expansion.downloader

- Upstream: https://github.com/google/play-apk-expansion/tree/master/apkx_library
- Version: git (9ecf54e, 2017)
- License: Apache 2.0

Overwrite all files under:

- `lib/src/main/java/com/google/android/vending/expansion/downloader`

Local changes avoid Handler ownership leaks, use the Foundry resource package,
make numeric formatting locale-stable, and preserve modern Android lint and
runtime behavior. The target-SDK-36 host also makes the downloader retry
`PendingIntent` immutable. See
`lib/patches/com.google.android.vending.expansion.downloader.patch`.
The Apache-2.0 license text is distributed at
`lib/src/main/resources/META-INF/foundry/LICENSES/Apache-2.0.txt`.

## com.google.android.vending.licensing

- Upstream: https://github.com/google/play-licensing/tree/master/lvl_library/
- Version: git (eb57657, 2018) with modifications
- License: Apache 2.0

Overwrite all files under:

- `lib/src/main/aidl/com/android/vending/licensing`
- `lib/src/main/java/com/google/android/vending/licensing`

Local changes preserve asynchronous preference behavior and replace a disabled
Java assertion with a debug-only runtime check. See
`lib/patches/com.google.android.vending.licensing.patch`.
The Apache-2.0 license text is distributed at
`lib/src/main/resources/META-INF/foundry/LICENSES/Apache-2.0.txt`.

## com.android.apksig

- Upstream: https://android.googlesource.com/platform/tools/apksig/+/ac5cbb07d87cc342fcf07715857a812305d69888
- Version: git (ac5cbb07d87cc342fcf07715857a812305d69888, 2024)
- License: Apache 2.0

Overwrite all files under:

- `editor/src/main/java/com/android/apksig`
