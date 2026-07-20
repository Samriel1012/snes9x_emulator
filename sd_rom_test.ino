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

<script>

const img = document.getElementById("screen");

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
