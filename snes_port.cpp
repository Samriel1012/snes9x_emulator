// =========================================================
// snes_port.cpp
// ESP32 Arduino Snes9x port
// =========================================================

#include "snes_engine.h"

#include <Arduino.h>
#include "esp_heap_caps.h"
#include <stdlib.h>
#include "esp_heap_caps.h"

extern "C" {
  #include "snes9x.h"
  #include "memmap.h"
  #include "gfx.h"
  #include "apu.h"
  #include "cpuexec.h"
}
// ================= FRAMEBUFFER =================

uint16_t *snesFramebuffer = nullptr;


// ================= INPUT =================

volatile uint32_t g_currentJoypadState = 0;


void snesEngineSetButtons(uint32_t mask)
{
  g_currentJoypadState = mask;
}


uint32_t S9xReadJoypad(int32_t port)
{
  if(port != 0)
    return 0;

  return g_currentJoypadState;
}


bool S9xReadMousePosition(
  int32_t which,
  int32_t *x,
  int32_t *y,
  uint32_t *buttons)
{
  if(x) *x = 0;
  if(y) *y = 0;
  if(buttons) *buttons = 0;

  return false;
}


bool S9xReadSuperScopePosition(
  int32_t *x,
  int32_t *y,
  uint32_t *buttons)
{
  if(x) *x = 0;
  if(y) *y = 0;
  if(buttons) *buttons = 0;

  return false;
}


void JustifierButtons(uint32_t *buttons)
{
  if(buttons)
    *buttons = 0;
}


bool JustifierOffscreen()
{
  return true;
}


void S9xNextController()
{
}


void S9xToggleSoundChannel(int32_t channel)
{
}



// ================= DISPLAY =================


bool S9xInitDisplay()
{
  Serial.println("Initializing display buffers");


  size_t framebufferSize =
  SNES_WIDTH *
  SNES_HEIGHT_EXTENDED *
  sizeof(uint16_t);


  snesFramebuffer =
  (uint16_t*)heap_caps_malloc(
    framebufferSize,
    MALLOC_CAP_SPIRAM
  );


  if(!snesFramebuffer)
  {
    Serial.println("Framebuffer allocation failed");
    return false;
  }


  memset(
    snesFramebuffer,
    0,
    framebufferSize
  );



  // Main screen

  GFX.Screen =
  (uint8_t*)snesFramebuffer;



  GFX.Pitch =
  SNES_WIDTH * sizeof(uint16_t);



  GFX.Pitch2 =
  SNES_WIDTH;



  // Sub screen

  GFX.SubScreen =
  (uint8_t*)heap_caps_malloc(
    framebufferSize,
    MALLOC_CAP_SPIRAM
  );

  // Z buffers
  GFX.ZBuffer =
  (uint8_t*)heap_caps_malloc(
    SNES_WIDTH *
    SNES_HEIGHT_EXTENDED,
    MALLOC_CAP_SPIRAM
  );

  GFX.SubZBuffer =
  (uint8_t*)heap_caps_malloc(
    SNES_WIDTH *
    SNES_HEIGHT_EXTENDED,
    MALLOC_CAP_SPIRAM
  );

  if(!GFX.SubScreen ||
    !GFX.ZBuffer ||
    !GFX.SubZBuffer)
  {
    Serial.println("Extra display allocation failed");
    return false;
  }

  // Zero out the allocated PSRAM memory to prevent graphical corruption/crashes
  memset(GFX.SubScreen, 0, framebufferSize);
  memset(GFX.ZBuffer, 0, SNES_WIDTH * SNES_HEIGHT_EXTENDED);
  memset(GFX.SubZBuffer, 0, SNES_WIDTH * SNES_HEIGHT_EXTENDED);


  memset(
    GFX.SubScreen,
    0,
    framebufferSize
  );


  memset(
    GFX.ZBuffer,
    0,
    SNES_WIDTH * SNES_HEIGHT_EXTENDED
  );


  memset(
    GFX.SubZBuffer,
    0,
    SNES_WIDTH * SNES_HEIGHT_EXTENDED
  );



  Serial.printf(
    "Screen=%p Pitch=%lu Pitch2=%lu\n",
    GFX.Screen,
    (unsigned long)GFX.Pitch,
                (unsigned long)GFX.Pitch2
  );


  Serial.println("Display buffers OK");


  return true;
}



void S9xDeinitDisplay()
{

  if(snesFramebuffer)
  {
    heap_caps_free(snesFramebuffer);
    snesFramebuffer = nullptr;
  }


  if(GFX.SubScreen)
  {
    heap_caps_free(GFX.SubScreen);
    GFX.SubScreen = nullptr;
  }


  if(GFX.ZBuffer)
  {
    heap_caps_free(GFX.ZBuffer);
    GFX.ZBuffer = nullptr;
  }


  if(GFX.SubZBuffer)
  {
    heap_caps_free(GFX.SubZBuffer);
    GFX.SubZBuffer = nullptr;
  }

}



// ================= VIDEO OUTPUT =================


void S9xPutImage(
  int width,
  int height
)
{
  // Browser/web output will read snesFramebuffer

  Serial.printf(
    "Frame %dx%d\n",
    width,
    height
  );
}

// ================= CORE HOOKS / MEMORY OVERRIDES =================

extern "C" {
  // Overriding Snes9x internal allocations that happen inside S9xInitGFX()
  void *S9xAllocOBJLines() {
    // SGFX structure uses OBJLines array which requires extra heap space
    size_t size = sizeof(SOBJLines) * (SNES_HEIGHT_EXTENDED + 1);
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (ptr) {
      memset(ptr, 0, size);
    } else {
      Serial.println("CRITICAL: OBJLines PSRAM allocation failed!");
    }
    return ptr;
  }
}

// Simple fallback if your specific Snes9x core edition expects
// the port layer to handle the global initialization wrapper
bool S9xOpenSoundDevice(int, int, int) { return true; }
void S9xSetPalette() {}
void S9xMessage(int, int, const char *msg) { Serial.printf("Snes9x Msg: %s\n", msg); }
const char *S9xGetFilename(const char *ex, const char *dir) { return ""; }
const char *S9xGetFilenameInc(const char *e, const char *d) { return ""; }
const char *S9xGetDirectory(const char *d) { return ""; }
const char *S9xChooseFilename(bool) { return ""; }
const char *S9xChooseMovieFilename(bool) { return ""; }
void S9xSyncSpeed() {}

// ================= LINKER WRAPPERS FOR PSRAM ROUTING =================

extern "C" {
  void* __real_malloc(size_t size);
  void* __real_calloc(size_t nmemb, size_t size);
  void* __real_realloc(void* ptr, size_t size);
  void __real_free(void* ptr);

  void* __wrap_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  }

  void* __wrap_calloc(size_t nmemb, size_t size) {
    return heap_caps_calloc(nmemb, size, MALLOC_CAP_SPIRAM);
  }

  void* __wrap_realloc(void* ptr, size_t size) {
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
  }

  void __wrap_free(void* ptr) {
    heap_caps_free(ptr);
  }
}
