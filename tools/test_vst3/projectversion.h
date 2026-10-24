// What the SDK's CMake would generate for the mda-vst3 sample (tools/build_test_vst3.ps1 builds without CMake).
#pragma once

#define MAJOR_VERSION_STR "1"
#define MAJOR_VERSION_INT 1
#define SUB_VERSION_STR "0"
#define SUB_VERSION_INT 0
#define RELEASE_NUMBER_STR "0"
#define RELEASE_NUMBER_INT 0
#define BUILD_NUMBER_STR "1"
#define BUILD_NUMBER_INT 1
#define SHORT_VERSION_STR MAJOR_VERSION_STR "." SUB_VERSION_STR "." RELEASE_NUMBER_STR
#define FULL_VERSION_STR SHORT_VERSION_STR "." BUILD_NUMBER_STR
#define VERSION_STR FULL_VERSION_STR
