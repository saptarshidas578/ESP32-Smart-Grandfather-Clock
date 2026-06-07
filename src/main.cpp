#include <Arduino.h>

// --- CORE FILE SYSTEMS ---
#include <FS.h>
#include <SD.h>
#include <SD_MMC.h>
#include <SPIFFS.h>
#include <FFat.h>

// --- SENSORS, PERIPHERALS & NETWORKING ---
#include <SPI.h>       
#include <Wire.h>      
#include <WiFi.h>
#include <time.h>

// --- LIBRARIES ---
#include <FastLED.h>
#include <RTClib.h>
#include <driver/i2s.h>
#include <math.h>

// ==============================
// 1. CONFIGURATION & PINS
// ==============================
const char *ssid = "Verizon-SM-N981U-7692";
const char *password = "nzjf807(";
const char *ntp_server = "pool.ntp.org";
const char *time_zone = "IST-5:30"; 

#define NUM_LEDS 84
#define DATA_PIN 5
#define BRIGHTNESS 50 
CRGB leds[NUM_LEDS];

#define I2S_WS_PIN     25
#define I2S_BCK_PIN    26
#define I2S_DOUT_PIN   27
#define SAMPLE_RATE    44100
#define CHUNK_SIZE     64

// ==============================
// 2. GLOBALS & TIMING
// ==============================
RTC_DS1307 rtc;
int h, m, s;
int lastChimeMinute = -1;
unsigned long lastSerialPrint = 0; 

CRGB MARKER_COLOR, HOUR_COLOR, MINUTE_COLOR, SECOND_COLOR;   
CRGB DEFAULT_COLOR = CRGB::Black; 

// --- Audio Queue Variables ---
volatile int quarterChimesToPlay = 0; 
volatile int hourStrikesToPlay = 0; 
const char* quarterMelody = "Westminster:d=4,o=5,b=90:e,c,d,2g4,p,g4,d,e,2c"; 
const char* hourStrike = "Gong:d=2,o=5,b=60:c"; 

// --- CROSS-CORE SYNC VARIABLES ---
volatile uint32_t sharedUnixTime = 0; 
volatile uint32_t lastSyncSuccessTime = 0; 
volatile uint32_t lastSyncAttemptTime = 0;
volatile bool isBackgroundSyncing = false;
unsigned long bgSyncStartTime = 0;

volatile bool newNtpTimeAvailable = false;
volatile int ntpYear, ntpMonth, ntpDay, ntpHour, ntpMinute, ntpSecond;

// ==============================
// 3. WIFI & NTP SYNC SYSTEMS
// ==============================

// UNINTERRUPTED RAINBOW BOOT SEQUENCE
void initialBootSync() {
    Serial.print("\nAttempting Initial WiFi Boot Sync: ");
    Serial.println(ssid);
    
    WiFi.begin(ssid, password);
    int spinPos = 0; 
    unsigned long startWait = millis();
    
    // Phase 1: Spin while connecting to WiFi (15s timeout)
    while ((WiFi.status() != WL_CONNECTED) && (millis() - startWait < 15000)) {
        FastLED.clear();
        for (int i = 0; i < 12; i++) {
            int ledIndex = (spinPos + i) % NUM_LEDS; 
            uint8_t hue = map(i, 0, 11, 0, 224);     
            leds[ledIndex] = CHSV(hue, 255, 255);
        }
        FastLED.show();
        spinPos = (spinPos + 1) % NUM_LEDS; 
        delay(25); 
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ Boot Sync Connected! Grabbing atomic time...");
        configTzTime(time_zone, ntp_server);
        
        struct tm timeinfo;
        bool timeSynced = false;
        startWait = millis();
        
        // Phase 2: Spin while getting NTP time (10s timeout)
        while (!timeSynced && (millis() - startWait < 10000)) {
            FastLED.clear();
            for (int i = 0; i < 12; i++) {
                int ledIndex = (spinPos + i) % NUM_LEDS; 
                uint8_t hue = map(i, 0, 11, 0, 224);     
                leds[ledIndex] = CHSV(hue, 255, 255);
            }
            FastLED.show();
            spinPos = (spinPos + 1) % NUM_LEDS; 
            
            // Check for time with a tiny 10ms timeout so animation doesn't freeze
            if (getLocalTime(&timeinfo, 10)) {
                timeSynced = true;
            } else {
                delay(15); 
            }
        }
        
        if (timeSynced) {
            DateTime ntpTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            rtc.adjust(ntpTime);
            lastSyncSuccessTime = ntpTime.unixtime(); 
            Serial.println("✅ Hardware RTC perfectly updated!");
        } else {
            Serial.println("❌ NTP Sync Timeout. Falling back to RTC battery.");
        }
    } else {
        Serial.println("\n❌ Boot Sync Failed. Will retry in background on Core 0 later.");
    }

    FastLED.clear();
    FastLED.show();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

// INVISIBLE BACKGROUND SYNC (Core 0)
void handleBackgroundSyncCore0() {
    if (sharedUnixTime == 0) return; 

    if (sharedUnixTime - lastSyncSuccessTime >= 86400) {
        if (!isBackgroundSyncing && (sharedUnixTime - lastSyncAttemptTime >= 3600)) {
            Serial.println("\n🔄 [CORE 0] Audio is silent. Initiating atomic sync...");
            WiFi.begin(ssid, password);
            isBackgroundSyncing = true;
            bgSyncStartTime = millis();
            lastSyncAttemptTime = sharedUnixTime; 
        }
    }

    if (isBackgroundSyncing) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("✅ [CORE 0] WiFi Connected! Grabbing atomic time...");
            configTzTime(time_zone, ntp_server);
            
            struct tm timeinfo;
            if (getLocalTime(&timeinfo, 5000)) { 
                ntpYear = timeinfo.tm_year + 1900;
                ntpMonth = timeinfo.tm_mon + 1;
                ntpDay = timeinfo.tm_mday;
                ntpHour = timeinfo.tm_hour;
                ntpMinute = timeinfo.tm_min;
                ntpSecond = timeinfo.tm_sec;
                
                newNtpTimeAvailable = true; 
                lastSyncSuccessTime = sharedUnixTime; 
                Serial.println("✅ [CORE 0] Time sent to Core 1 for RTC hardware update.");
            } else {
                Serial.println("❌ [CORE 0] NTP Server unresponsive.");
            }
            
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            isBackgroundSyncing = false;
        } 
        else if (millis() - bgSyncStartTime > 15000) {
            Serial.println("❌ [CORE 0] WiFi timeout. Will try again invisibly in 1 hour.");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            isBackgroundSyncing = false;
        }
    }
}

// ==============================
// 4. AUDIO SYSTEM (Core 0)
// ==============================
void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 128,
        .use_apll = false,
        .tx_desc_auto_clear = true
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_DOUT_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

void playTone(float frequency, uint32_t durationMs) {
    if (frequency == 0.0f) {
        vTaskDelay(durationMs / portTICK_PERIOD_MS); 
        return;
    }
    int totalSamples = (SAMPLE_RATE * durationMs) / 1000;
    float phase = 0.0f;
    float phaseInc = (2.0f * PI * frequency) / SAMPLE_RATE;
    int16_t sampleBuffer[CHUNK_SIZE * 2]; 
    int samplesWritten = 0;
    
    float targetVolume = 0.40f; 
    int attackSamples = SAMPLE_RATE * 0.02;  
    int releaseSamples = SAMPLE_RATE * 0.60; 
    
    while (samplesWritten < totalSamples) {
        int samplesToGenerate = min(CHUNK_SIZE, totalSamples - samplesWritten);
        for (int i = 0; i < samplesToGenerate; i++) {
            int currentSampleIndex = samplesWritten + i;
            float currentVolume = targetVolume; 
            
            if (currentSampleIndex < attackSamples) {
                currentVolume = targetVolume * ((float)currentSampleIndex / attackSamples);
            } else if (currentSampleIndex > totalSamples - releaseSamples) {
                float ratio = (float)(totalSamples - currentSampleIndex) / releaseSamples;
                currentVolume = targetVolume * (ratio * ratio); 
            }

            float fundamental = sin(phase);
            float harmonic1 = 0.20f * sin(phase * 2.0f); 
            float harmonic2 = 0.02f * sin(phase * 3.0f); 
            float s = (fundamental + harmonic1 + harmonic2) / 1.22f;
            
            phase += phaseInc;
            if (phase >= (2.0f * PI)) phase -= (2.0f * PI);
            
            int16_t pcmData = (int16_t)(s * currentVolume * 32767.0f);
            sampleBuffer[i * 2] = pcmData;       
            sampleBuffer[(i * 2) + 1] = pcmData; 
        }
        size_t bytesWritten;
        i2s_write(I2S_NUM_0, sampleBuffer, samplesToGenerate * 4, &bytesWritten, portMAX_DELAY);
        samplesWritten += samplesToGenerate;
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
}

void playRTTTL(const char *p) {
    while(*p && *p != ':') p++; if(!*p) return; p++;
    int default_dur = 4, default_oct = 6, bpm = 63; int num;
    while(*p && *p != ':') {
        if(*p == 'd') { p+=2; num = 0; while(isdigit(*p)) { num = (num * 10) + (*p++ - '0'); } if(num > 0) default_dur = num; }
        if(*p == 'o') { p+=2; num = 0; while(isdigit(*p)) { num = (num * 10) + (*p++ - '0'); } if(num >= 3 && num <=7) default_oct = num; }
        if(*p == 'b') { p+=2; num = 0; while(isdigit(*p)) { num = (num * 10) + (*p++ - '0'); } bpm = num; }
        while(*p == ',') p++;
    }
    p++; 
    long wholenote = (60 * 1000L / bpm) * 4;  
    while(*p) {
        num = 0; while(isdigit(*p)) { num = (num * 10) + (*p++ - '0'); }
        int duration = num ? num : default_dur;
        int note = 0;
        switch(*p) {
            case 'c': note = 1; break; case 'd': note = 3; break; case 'e': note = 5; break; case 'f': note = 6; break;
            case 'g': note = 8; break; case 'a': note = 10; break; case 'b': note = 12; break; case 'p': default: note = 0; break;
        }
        p++;
        if(*p == '#') { note++; p++; } if(*p == '.') { duration += duration/2; p++; } 
        int octave = isdigit(*p) ? (*p++ - '0') : default_oct;
        if(*p == ',') p++;
        long durationMs = wholenote / duration;
        if(note == 0) playTone(0, durationMs);
        else {
            float frequency = 440.0 * pow(2.0, ((note - 10) + (octave - 4) * 12) / 12.0);
            playTone(frequency, durationMs);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); 
    }
}

void audioTask(void *pvParameters) {
    setupI2S(); 
    for (;;) {
        if (quarterChimesToPlay > 0 || hourStrikesToPlay > 0) {
            
            if (isBackgroundSyncing) {
                Serial.println("⚠️ [CORE 0] Audio Priority Override! Killing WiFi connection attempt.");
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                isBackgroundSyncing = false;
            }

            if (quarterChimesToPlay > 0) {
                playRTTTL(quarterMelody);
                quarterChimesToPlay--; 
                if (quarterChimesToPlay > 0) vTaskDelay(1000 / portTICK_PERIOD_MS); 
                else if (hourStrikesToPlay > 0) vTaskDelay(1500 / portTICK_PERIOD_MS); 
            } 
            else if (hourStrikesToPlay > 0) {
                playRTTTL(hourStrike);
                hourStrikesToPlay--;
                if (hourStrikesToPlay > 0) vTaskDelay(500 / portTICK_PERIOD_MS); 
            }
        } 
        else {
            handleBackgroundSyncCore0();
            vTaskDelay(50 / portTICK_PERIOD_MS); 
        }
    }
}

// ==============================
// 5. LED & CLOCK SYSTEM (Core 1)
// ==============================

// --- NIGHT MODE LOGIC ---
bool isAudioMuted(int currentHour, int currentMinute) {
    if (currentHour == 23 && currentMinute > 0) return true;
    if (currentHour >= 0 && currentHour < 6) return true;
    if (currentHour == 6 && currentMinute < 30) return true;
    return false;
}

void updateColorPalette(int hour24) {
    if (hour24 >= 0 && hour24 < 6) { 
        MARKER_COLOR = CRGB(0x88BBFF); HOUR_COLOR = CRGB(0x8800FF); MINUTE_COLOR = CRGB(0x00FFCC); SECOND_COLOR = CRGB(0xFF00AA);
    } else if (hour24 >= 6 && hour24 < 12) { 
        MARKER_COLOR = CRGB(0xFFF8DC); HOUR_COLOR = CRGB(0xFF4444); MINUTE_COLOR = CRGB(0x33FF33); SECOND_COLOR = CRGB(0xFFFF00);
    } else if (hour24 >= 12 && hour24 < 18) { 
        MARKER_COLOR = CRGB(0xFF88FF); HOUR_COLOR = CRGB(0xFF6600); MINUTE_COLOR = CRGB(0x00AAFF); SECOND_COLOR = CRGB(0xCCFF00);
    } else { 
        MARKER_COLOR = CRGB(0xFFB800); HOUR_COLOR = CRGB(0xFF3388); MINUTE_COLOR = CRGB(0x00FF88); SECOND_COLOR = CRGB(0x4400FF);
    }
}

int getPhysicalIndex(int block, int logicalIndex) {
    if (block % 2 == 0) return (block * 7) + logicalIndex;
    else return (block * 7) + (6 - logicalIndex);
}

void setup() {
    Serial.begin(115200);

    FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear(); FastLED.show();

    Wire.begin(21, 22); 
    if (!rtc.begin()) {
        Serial.println("❌ ERROR: RTC not found!");
    }
    
    initialBootSync();

    xTaskCreatePinnedToCore(audioTask, "AudioTask", 10000, NULL, 1, NULL, 0);
    
    quarterChimesToPlay = 1; 
    Serial.println("\n⌚ Smart Clock is Live! Ticking started...\n");
}

void loop() {
    DateTime now = rtc.now();
    sharedUnixTime = now.unixtime(); 
    h = now.hour();
    m = now.minute();
    s = now.second();

    if (newNtpTimeAvailable) {
        rtc.adjust(DateTime(ntpYear, ntpMonth, ntpDay, ntpHour, ntpMinute, ntpSecond));
        newNtpTimeAvailable = false; 
        Serial.println("✅ [CORE 1] Hardware RTC successfully updated with atomic time from Core 0.");
    }

    if (millis() - lastSerialPrint >= 1000) {
        lastSerialPrint = millis();
    }

    if (m != lastChimeMinute) { 
        if (!isAudioMuted(h, m)) {
            if (m == 15) quarterChimesToPlay = 1; 
            else if (m == 30) quarterChimesToPlay = 2; 
            else if (m == 45) quarterChimesToPlay = 3; 
            else if (m == 0) { 
                quarterChimesToPlay = 4; 
                hourStrikesToPlay = (h % 12 == 0) ? 12 : (h % 12);
            }
        } else {
            if (m == 0 || m == 15 || m == 30 || m == 45) {
                Serial.println("🤫 Night Mode Active: Audio bypassed for sleep window.");
            }
        }
        lastChimeMinute = m; 
    }

    updateColorPalette(h);
    
    int currentHourBlock = h % 12; 
    if (m >= 45) {
        currentHourBlock = (currentHourBlock + 1) % 12;
    }
    
    int currentMinuteBlock = m / 5;
    int currentSecondBlock = s / 5;

    for (int b = 0; b < 12; b++) {
        leds[getPhysicalIndex(b, 6)] = MARKER_COLOR; 
        
        if (b == currentSecondBlock) {
            for (int i = 0; i < 6; i++) leds[getPhysicalIndex(b, i)] = SECOND_COLOR;
        } else {
            if (b == currentHourBlock && b == currentMinuteBlock) {
                leds[getPhysicalIndex(b, 0)] = HOUR_COLOR; leds[getPhysicalIndex(b, 1)] = HOUR_COLOR; 
                leds[getPhysicalIndex(b, 2)] = HOUR_COLOR; leds[getPhysicalIndex(b, 3)] = HOUR_COLOR;
                leds[getPhysicalIndex(b, 4)] = MINUTE_COLOR; leds[getPhysicalIndex(b, 5)] = MINUTE_COLOR;
            } else if (b == currentHourBlock) {
                for (int i = 0; i < 4; i++) leds[getPhysicalIndex(b, i)] = HOUR_COLOR;
                for (int i = 4; i < 6; i++) leds[getPhysicalIndex(b, i)] = DEFAULT_COLOR;
            } else if (b == currentMinuteBlock) {
                for (int i = 0; i < 6; i++) leds[getPhysicalIndex(b, i)] = MINUTE_COLOR;
            } else {
                for (int i = 0; i < 6; i++) leds[getPhysicalIndex(b, i)] = DEFAULT_COLOR;
            }
        }
    }

    // --- BACKGROUND WIFI SYNC INDICATOR ---
    // Visually overlay a spinning cyan dot if Core 0 is currently syncing in the background
    if (isBackgroundSyncing) {
        int spinPos = (millis() / 150) % 12; 
        leds[getPhysicalIndex(spinPos, 6)] = CRGB(0x00AAFF); 
    }
    
    FastLED.show();
    delay(50); 
}