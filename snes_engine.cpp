// =========================================================
// snes_engine.cpp
// ESP32 S3 Snes9x engine wrapper
// =========================================================

#include "snes_engine.h"

#include <Arduino.h>
#include <string.h>

#include "display.h"
#include "snes9x.h"
#include "memmap.h"
#include "gfx.h"
#include "apu.h"
#include "cpuexec.h"


extern uint16_t *snesFramebuffer;


#define SNES_FB_WIDTH  SNES_WIDTH
#define SNES_FB_HEIGHT SNES_HEIGHT_EXTENDED



bool snesEngineInit(const uint8_t *romData, size_t romSize)
{
  Serial.println("snesEngineInit starting");


  // -------------------------------------------------
  // Snes9x settings
  // -------------------------------------------------

  Settings.CyclesPercentage = 100;

  Settings.H_Max = SNES_CYCLES_PER_SCANLINE;

  Settings.FrameTimePAL = 20000;

  Settings.FrameTimeNTSC = 16667;

  Settings.ControllerOption = SNES_JOYPAD;

  Settings.HBlankStart =
  (256 * Settings.H_Max) /
  SNES_HCOUNTER_MAX;


  Settings.SoundPlaybackRate = 32000;

  Settings.SoundInputRate = 32000;

  Settings.DisableSoundEcho = false;

  Settings.InterpolatedSound = false;



  // -------------------------------------------------
  // Initialize display
  // -------------------------------------------------

  Serial.println("Initializing display");

  if(!S9xInitDisplay())
  {
    Serial.println("S9xInitDisplay failed");
    return false;
  }


  // -------------------------------------------------
  // Memory
  // -------------------------------------------------

  Serial.println("Initializing memory");

  if(!S9xInitMemory())
  {
    Serial.println("S9xInitMemory failed");
    return false;
  }



  // -------------------------------------------------
  // APU
  // -------------------------------------------------

  Serial.println("Initializing APU");

  if(!S9xInitAPU())
  {
    Serial.println("S9xInitAPU failed");
    return false;
  }



  Serial.println("Initializing sound");

  if(!S9xInitSound(0,0))
  {
    Serial.println("S9xInitSound failed");
    return false;
  }



  // -------------------------------------------------
  // Graphics
  // -------------------------------------------------

  Serial.println("Initializing GFX");

  if(!S9xInitGFX())
  {
    Serial.println("S9xInitGFX failed");
    return false;
  }



  // -------------------------------------------------
  // Load ROM
  // -------------------------------------------------

  Serial.printf(
    "ROM size: %u bytes\n",
    (unsigned)romSize
  );


  if(romSize > Memory.ROM_AllocSize)
  {
    Serial.printf(
      "ROM too large %u > %u\n",
      (unsigned)romSize,
                  (unsigned)Memory.ROM_AllocSize
    );

    return false;
  }



  Serial.println("Copying ROM");


  memcpy(
    Memory.ROM,
    romData,
    romSize
  );


  // IMPORTANT:
  // Do NOT change Memory.ROM_AllocSize here.
  // It is the allocated buffer size, not ROM size.



  Serial.println("Calling LoadROM");


  if(!LoadROM(NULL))
  {
    Serial.println("LoadROM failed");
    return false;
  }


  Serial.println("LoadROM complete");


  Serial.printf(
    "ROM loaded: %s\n",
    Memory.ROMName
  );



  // -------------------------------------------------
  // Reset SNES
  // -------------------------------------------------

  Serial.println("Before S9xReset");


  S9xReset();


  Serial.println("After S9xReset");


  Serial.println("SNES engine ready");


  return true;
}





void snesEngineRunFrame(void)
{
  IPPU.RenderThisFrame = true;
  //IPPU.SkippedFrames = 0;

  //Settings.SkipFrames = 0;

  Serial.println("Running frame");

  S9xMainLoop();

  Serial.println("Frame done");

  delay(1);
}





uint16_t *snesEngineGetFramebuffer(void)
{
  return snesFramebuffer;
}
