#pragma once

// Read-only rigid presentation interpolation for KF2's exact January 2024 Win64 image.
// KF2's writable particle/normal buffers always remain canonical.
void p4_install_presentation();
void p4_release_presentation(void* solver);

using P4Synchronize = void(*)(void*, float);
using P4RigidData = void(*)(void*, float**, float**);

#ifdef P4_CPU_MODEL
void p4_test_bind_presentation(P4Synchronize synchronize, P4RigidData rigid);
#define P4_PRESENT_API extern "C"
#else
#define P4_PRESENT_API extern "C" __declspec(dllexport)
#endif

P4_PRESENT_API void p4_present_sync(void* instance, float delta_seconds);
P4_PRESENT_API void p4_present_rigid(void* container, float** rotations, float** translations);


