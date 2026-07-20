// =========================================================
// snes_engine.h / snes_engine.cpp
// Simple wrapper around Snes9x so the main .ino doesn't need
// to touch any of the core's internals directly.
// =========================================================

#ifndef SNES_ENGINE_H
#define SNES_ENGINE_H

#include <stdint.h>
#include <stddef.h>

// Call once at startup, after SD card is mounted and the ROM has
// been located. romData/romSize describe the ROM exactly as read
// from SD (512-byte copier header, if any, already stripped - same
// convention as the SD/ROM test sketch). Returns true on success.
bool snesEngineInit(const uint8_t *romData, size_t romSize);

// Call once per iteration of your server loop. Runs exactly one
// emulated video frame.
void snesEngineRunFrame(void);

// Returns a pointer to the top-left pixel of the visible 256x224
// region (RGB565), row pitch is 256*2 bytes - safe to hand directly
// to the JPEG encoder.
uint16_t *snesEngineGetFramebuffer(void);

// Update controller state. mask should be built by OR-ing together
// SNES_*_MASK constants from snes9x.h (SNES_A_MASK, SNES_UP_MASK, etc).
void snesEngineSetButtons(uint32_t mask);

#endif
