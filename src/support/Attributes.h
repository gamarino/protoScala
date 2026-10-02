/*
 * Attributes — function attributes that MSVC spells differently or not at all.
 *
 * GCC and Clang read [[gnu::...]]; MSVC warns that it does not recognise them
 * (C5030) and has its own __declspec(noinline), and no "cold" hint.
 */
#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#define PROTOSCALA_NOINLINE __declspec(noinline)
#define PROTOSCALA_COLD
#else
#define PROTOSCALA_NOINLINE [[gnu::noinline]]
#define PROTOSCALA_COLD [[gnu::cold]]
#endif
