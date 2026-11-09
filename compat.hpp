#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#define EXTERN_DLL_EXPORT extern "C" __declspec(dllexport)
#ifndef DECLSPEC_NOINLINE
#define DECLSPEC_NOINLINE __declspec(noinline)
#endif
#ifndef SPEC_NOINLINE
#define SPEC_NOINLINE __declspec(noinline)
#endif
#define STACK_FRAME_OPT_ENABLE
#define STACK_FRAME_OPT_RESET
