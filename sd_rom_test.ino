#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

#include "FS.h"
#include "SD_MMC.h"

#include "snes_engine.h"

#include <WiFi.h>
#include <WebServer.h>
#include <JPEGENC.h>

void SnesLoop();

// ================= WIFI =================

const char* ssid = "BELL459";
const char* password = "96F61D215F2A";


// ================= SERVER =================

WebServer server(80);


// ================= EMULATOR DATA =================

uint8_t* romBuffer = nullptr;
size_t romSize = 0;

float currentFPS = 0;
uint16_t currentPixel = 0;

const uint32_t SNES_B_MASK      = 1u << 15;
const uint32_t SNES_Y_MASK      = 1u << 14;
const uint32_t SNES_SELECT_MASK = 1u << 13;
const uint32_t SNES_START_MASK  = 1u << 12;
const uint32_t SNES_UP_MASK     = 1u << 11;
const uint32_t SNES_DOWN_MASK   = 1u << 10;
const uint32_t SNES_LEFT_MASK   = 1u << 9;
const uint32_t SNES_RIGHT_MASK  = 1u << 8;
const uint32_t SNES_A_MASK      = 1u << 7;
const uint32_t SNES_X_MASK      = 1u << 6;
const uint32_t SNES_TL_MASK     = 1u << 4;
const uint32_t SNES_TR_MASK     = 1u << 3;


// ================= DISPLAY / JPEG =================

// snesEngineGetFramebuffer() returns a pointer to the top-left pixel of the
// visible 256x224 region, row pitch 256*2 bytes - these must match that.
#define SNES_FRAME_WIDTH  256
#define SNES_FRAME_HEIGHT 224

JPEGENC jpg;
JPEGENCODE jpe;

uint8_t* jpegOutputBuffer = nullptr;
const size_t JPEG_BUFFER_SIZE = 65536;   // plenty for a 256x224 game frame
volatile size_t jpegOutputSize = 0;


bool encodeFramebufferToJPEG(uint16_t *fb)
{
    int rc;

    rc = jpg.open(jpegOutputBuffer, JPEG_BUFFER_SIZE);

    if(rc != JPEGE_SUCCESS)
    {
        Serial.println("JPEG open failed");
        return false;
    }

    rc = jpg.encodeBegin(
        &jpe,
        SNES_FRAME_WIDTH,
        SNES_FRAME_HEIGHT,
        JPEGE_PIXEL_RGB565,
        JPEGE_SUBSAMPLE_444,
        JPEGE_Q_HIGH
    );

    if(rc != JPEGE_SUCCESS)
    {
        Serial.println("JPEG encodeBegin failed");
        return false;
    }

    const int pitch = SNES_FRAME_WIDTH * sizeof(uint16_t);

    // 4:4:4 subsampling -> 8x8 pixel MCUs. Our framebuffer is one
    // contiguous 256x224 RGB565 buffer, so we can point straight into
    // it - no separate staging buffer needed.
    for(int y = 0; y < SNES_FRAME_HEIGHT; y += 8)
    {
        for(int x = 0; x < SNES_FRAME_WIDTH; x += 8)
        {
            uint8_t *pMCU = (uint8_t*)(fb + (y * SNES_FRAME_WIDTH) + x);

            rc = jpg.addMCU(&jpe, pMCU, pitch);

            if(rc != JPEGE_SUCCESS)
            {
                Serial.println("JPEG addMCU failed");
                return false;
            }
        }
    }

    int dataSize = jpg.close();

    if(dataSize <= 0)
    {
        Serial.println("JPEG close failed");
        return false;
    }

    jpegOutputSize = (size_t)dataSize;

    return true;
}


// ================= WEB PAGE =================

void handleRoot()
{
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>ESP32 SNES</title>

<style>
body{
background:#111;
color:#00ff00;
font-family:monospace;
text-align:center;
}
img{
image-rendering:pixelated;
border:1px solid #333;
margin-top:10px;
}
</style>

</head>

<body>

<h1>ESP32 SNES Emulator</h1>

<h2>Zelda</h2>

<img id="screen" src="/frame" width="512" height="448">

<p id="status">
Loading...
</p>

<p>
Controls: Arrow keys = D-pad · Z = B · X = A · A = Y · S = X ·
Q = L · W = R · Enter = Start · Shift = Select
</p>

<script>

const img = document.getElementById("screen");
const keyMask = {
  ArrowUp: 2048,
  ArrowDown: 1024,
  ArrowLeft: 512,
  ArrowRight: 256,
  KeyZ: 32768,
  KeyX: 128,
  KeyA: 16384,
  KeyS: 64,
  KeyQ: 16,
  KeyW: 8,
  Enter: 4096,
  ShiftLeft: 8192,
  ShiftRight: 8192
};
const pressed = new Set();

function sendInput() {
  let mask = 0;
  for (const code of pressed) mask |= keyMask[code];
  fetch('/input?mask=' + mask, { method: 'POST', cache: 'no-store' })
    .catch(() => {});
}

window.addEventListener('keydown', event => {
  if (!(event.code in keyMask)) return;
  event.preventDefault();
  if (!pressed.has(event.code)) {
    pressed.add(event.code);
    sendInput();
  }
});

window.addEventListener('keyup', event => {
  if (!(event.code in keyMask)) return;
  event.preventDefault();
  pressed.delete(event.code);
  sendInput();
});

window.addEventListener('blur', () => {
  if (pressed.size) {
    pressed.clear();
    sendInput();
  }
});

setInterval(()=>{

img.src = "/frame?t=" + Date.now();

},100);

setInterval(()=>{

fetch('/status')
.then(r=>r.text())
.then(t=>{

document.getElementById("status").innerHTML=t;

});

},1000);

</script>

</body>
</html>
)rawliteral";


    server.send(200,"text/html",html);
}



void handleStatus()
{

    String msg;

    msg += "FPS: ";
    msg += String(currentFPS);

    msg += "<br>";

    msg += "Pixel: 0x";
    msg += String(currentPixel,HEX);

    msg += "<br>";

    msg += "Frame bytes: ";
    msg += String((unsigned)jpegOutputSize);


    server.send(200,"text/html",msg);

}


void handleInput()
{
    if(!server.hasArg("mask"))
    {
        server.send(400, "text/plain", "Missing mask");
        return;
    }

    const uint32_t mask = (uint32_t) server.arg("mask").toInt();
    snesEngineSetButtons(mask);

    server.send(204, "text/plain", "");
}


void handleFrame()
{
    if(jpegOutputSize == 0)
    {
        server.send(503, "text/plain", "No frame yet");
        return;
    }

    server.setContentLength(jpegOutputSize);
    server.send(200, "image/jpeg", "");
    server.sendContent((const char*)jpegOutputBuffer, jpegOutputSize);
}



// ================= SD FUNCTIONS =================


void listDir(fs::FS &fs, const char *dirname)
{

    Serial.printf("Listing directory: %s\n", dirname);


    File root = fs.open(dirname);


    if(!root)
    {
        Serial.println("Failed to open directory");
        return;
    }


    File file = root.openNextFile();


    while(file)
    {

        Serial.printf("%s  %u bytes\n",
        file.name(),
        (unsigned)file.size());


        file = root.openNextFile();

    }

}




String findFirstROM(fs::FS &fs, const char *dirname)
{

    File root = fs.open(dirname);


    if(!root)
        return "";



    File file = root.openNextFile();



    while(file)
    {

        String name = file.name();


        String lower = name;

        lower.toLowerCase();



        if(lower.endsWith(".sfc") ||
           lower.endsWith(".smc"))
        {

            return String("/") + name;

        }



        file = root.openNextFile();

    }


    return "";

}





// ================= LOAD ROM =================

bool loadROMToPSRAM(const String &path)
{
    File file = SD_MMC.open(path);

    if(!file)
    {
        Serial.println("ROM open failed");
        return false;
    }


    romSize = file.size();

    Serial.printf(
        "ROM size: %u bytes\n",
        (unsigned)romSize
    );


    romBuffer =
    (uint8_t*)heap_caps_malloc(
        romSize,
        MALLOC_CAP_SPIRAM
    );


    if(!romBuffer)
    {
        Serial.println("PSRAM allocation failed");

        file.close();

        return false;
    }


    Serial.println("Reading ROM...");


    size_t total = 0;

    const size_t chunk = 32768;


    while(total < romSize)
    {
        size_t amount = romSize - total;

        if(amount > chunk)
            amount = chunk;


        size_t read =
        file.read(
            romBuffer + total,
            amount
        );


        if(read == 0)
            break;


        total += read;

        yield();
    }


    file.close();


    Serial.printf(
        "Loaded %u bytes\n",
        (unsigned)total
    );


    return total == romSize;
}



// ================= SETUP =================

void setup()
{
    Serial.begin(115200);

    delay(5000);

    Serial.println("Starting ESP32 SNES");

    // The vendored Snes9x core (gfx.cpp etc.) calls plain calloc()/malloc()
    // in several places (e.g. S9xInitGFX's ~22KB LocalState and ~128KB
    // GFX.ZERO lookup table). By default the ESP32 Arduino/IDF allocator
    // keeps plain malloc() on internal SRAM only, even with PSRAM enabled -
    // you only get PSRAM automatically for calls that explicitly pass
    // MALLOC_CAP_SPIRAM. heap_caps_malloc_extmem_enable(0) changes that
    // default so ALL malloc()/calloc() calls prefer PSRAM first. This must
    // be called before any of those core allocations happen.
    if (!psramFound())
    {
        Serial.println("PSRAM was not detected; this emulator cannot initialize graphics.");
        while (true) delay(1000);
    }

    Serial.printf(
        "PSRAM free at startup: %u bytes\n",
        (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM)
    );

    heap_caps_malloc_extmem_enable(0);

    Serial.println("About to mount SD");


    // ================= JPEG OUTPUT BUFFER =================

    jpegOutputBuffer =
    (uint8_t*)heap_caps_malloc(
        JPEG_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM
    );

    if(!jpegOutputBuffer)
    {
        Serial.println("JPEG buffer allocation failed");

        while(true)
        {
            yield();
        }
    }


    // ================= SD =================

SD_MMC.setPins(
    39,  // CLK
    38,  // CMD
    40,  // D0
    41,  // D1
    42,  // D2
    2    // D3
);


if(!SD_MMC.begin("/sdcard", true))
{
    Serial.println("SD FAILED");
    while(true);
}
    // ================= LOAD ROM =================

    String rom = findFirstROM(SD_MMC, "/");

Serial.print("ROM FOUND: ");
Serial.println(rom);


if(rom == "")
{
    Serial.println("NO ROM FOUND");

    while(true)
    {
        yield();
    }
}
    
    if(!loadROMToPSRAM(rom))
    {
        Serial.println("ROM LOAD FAILED");

        while(true)
        {
            yield();
        }
    }


    // ================= FIX ROM HEADER =================

    Serial.println("Checking ROM header...");


    uint8_t* header = romBuffer + 0x7FC0;


    Serial.printf(
        "ROM NAME: %.21s\n",
        header
    );


    uint8_t mapMode = header[0x15];


    Serial.printf(
        "MAP MODE: %02X\n",
        mapMode
    );


    // ================= SNES =================

    Serial.println("Starting Snes9x...");


    if(!snesEngineInit(
        romBuffer,
        romSize
    ))
    {
        Serial.println("SNES INIT FAILED");

        while(true)
        {
            yield();
        }
    }


    Serial.println("READY");

    WiFi.begin(ssid, password);

Serial.print("Connecting");

while(WiFi.status() != WL_CONNECTED)
{
    delay(500);
    Serial.print(".");
}

Serial.println();
Serial.println(WiFi.localIP());


server.on("/", handleRoot);
server.on("/status", handleStatus);
server.on("/frame", handleFrame);
server.on("/input", HTTP_POST, handleInput);

server.begin();

Serial.println("Web server started");

}



// ================= LOOP =================

void loop()
{
    unsigned long frameStart = millis();

    snesEngineRunFrame();

    uint16_t *fb = snesEngineGetFramebuffer();

    if(fb)
    {
        if(encodeFramebufferToJPEG(fb))
        {
            currentPixel =
            fb[(SNES_FRAME_HEIGHT / 2) * SNES_FRAME_WIDTH +
               (SNES_FRAME_WIDTH / 2)];
        }
    }

    unsigned long frameTime = millis() - frameStart;

    if(frameTime > 0)
    {
        currentFPS = 1000.0f / (float)frameTime;
    }

    server.handleClient();
}
