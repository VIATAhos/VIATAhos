#ifndef OS_VERSION_H
#define OS_VERSION_H

// === OS VERSION CONFIGURATION ===
// Change these 3 values to update the version and build type.
// OS_BUILD_TYPE: 0 = Stable, 1 = AlpDev (Nightly)
#define OS_VER_MAJOR "1.0"
#define OS_BUILD_TYPE 1
#define OS_BUILD_VER "1.0.0"

#if OS_BUILD_TYPE == 0
  #define OS_VERSION_STRING "V" OS_VER_MAJOR " Stable" OS_BUILD_VER
#else
  #define OS_VERSION_STRING "V" OS_VER_MAJOR " AlpDev" OS_BUILD_VER
#endif
#define OS_VER_STRING OS_VERSION_STRING
// ================================

#endif // OS_VERSION_H
