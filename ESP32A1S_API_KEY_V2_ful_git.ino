#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <time.h>

#include "ES8388/ES8388.h"
#include "FS.h"
#include "SD_MMC.h"
#include "btn_fix.h"

// ====== CRT BUNDLE AUTO DETECT (Arduino core függő) ======
#if defined(__has_include)
  #if __has_include(<esp_crt_bundle.h>)
    #include <esp_crt_bundle.h>
    #define HAVE_CRT_BUNDLE 1
  #else
    #define HAVE_CRT_BUNDLE 0
  #endif
#else
  #define HAVE_CRT_BUNDLE 0
#endif

// =====================================================
// WIFI – FIX (ELIEH STANDARD)
// =====================================================
static const char* WIFI_SSID = "IDE A SAJÁT WI-FI KELL";
static const char* WIFI_PASS = "IDE A HOZZÁ TARTOZO JELSZÓT";

// =====================================================
// OPENAI – kulcsot CSAK helyben írj be!
// =====================================================
static const char* OPENAI_API_KEY = " IDE A SAJÁT API KULCSOD MÁSOLD BE!";
static const char* OPENAI_HOST  = "api.openai.com";
static const char* OPENAI_PATH  = "/v1/audio/transcriptions";
static const int   OPENAI_PORT  = 443;
static const char* OPENAI_MODEL = "gpt-4o-transcribe";

// =====================================================
// TLS FALLBACK módok
//  - Ha van crt bundle: azt használjuk (stabil)
//  - Ha nincs crt bundle: ideiglenesen INSECURE, hogy működjön
//    (később core frissítéssel megoldjuk rendesen)
// =====================================================
static const bool ALLOW_INSECURE_TLS_WHEN_NO_BUNDLE = true;

// =====================================================
// AI Thinker AudioKit V2.2 pinek
// =====================================================
static const int PIN_I2S_MCLK = 0;
static const int PIN_I2S_BCK  = 27;
static const int PIN_I2S_WS   = 25;
static const int PIN_I2S_DOUT = 26;
static const int PIN_I2S_DIN  = 35;

static const int PIN_HP_DET   = 39;   // INPUT_PULLUP
static const int PIN_PA_EN    = 21;   // speaker amp enable

ES8388 es8388(33, 32, 400000); // SDA=33, SCL=32
static const i2s_port_t I2S_PORT = I2S_NUM_0;

// =====================================================
// GOMBOK (KEY2=GPIO13 KIESIK: SD DAT3)
// =====================================================
#define KEY1_PIN    36
#define KEY3_PIN    19
#define KEY4_PIN    23
#define KEY5_PIN    18
#define KEY6_PIN    5

#define BTN_MENU_PIN   KEY4_PIN
#define BTN_NEXT_PIN   KEY1_PIN
#define BTN_OK_PIN     KEY6_PIN
#define BTN_TEACH_PIN  KEY5_PIN

static const uint32_t DEBOUNCE_MS  = 35;
static const uint32_t LONGPRESS_MS = 650;

Btn btnMenu  { BTN_MENU_PIN,  true, true, true, 0, 0, false, false };
Btn btnNext  { BTN_NEXT_PIN,  true, true, true, 0, 0, false, false };
Btn btnOk    { BTN_OK_PIN,    true, true, true, 0, 0, false, false };
Btn btnTeach { BTN_TEACH_PIN, true, true, true, 0, 0, false, false };

// =====================================================
// MENÜ
// =====================================================
#define MENU_RUN     0
#define MENU_LEARN   1
#define MENU_COUNT   2
static int menuSel = MENU_RUN;

static const char* menuName(int m) {
  if (m == MENU_RUN)   return "VEGREHAJTAS (RUN)";
  if (m == MENU_LEARN) return "TANITAS (LEARN)";
  return "?";
}

static void printMenu() {
  Serial.println();
  Serial.println("===== ELIEH VOICE MENU =====");
  for (int i = 0; i < MENU_COUNT; i++) {
    Serial.print((i == menuSel) ? " > " : "   ");
    Serial.println(menuName(i));
  }
  Serial.println("----------------------------");
  Serial.println("MENU : menu kiiras");
  Serial.println("NEXT : menupont lepes");
  Serial.println("OK   : menupont info");
  Serial.println("TEACH: TANITAS (LEARN modban)");
  Serial.println("============================");
  Serial.println();
}

// =====================================================
// VAD paraméterek
// =====================================================
static const int   LEARN_MS      = 2500;
static float       TH_MULT      = 1.20f;
static int         TH_MARGIN    = 40;
static int         START_FRAMES = 2;
static int         END_FRAMES   = 10;

static const int SAMPLE_RATE   = 16000;
static const int FRAME_SAMPLES = 512;
static int16_t samples[FRAME_SAMPLES];

// preroll
static const int PREROLL_FRAMES = 6;
static int16_t preroll[PREROLL_FRAMES][FRAME_SAMPLES];
static int prerollIdx = 0;

// limitek
static const uint32_t MIN_SPEECH_BYTES = 6000;
static const uint32_t MAX_SPEECH_MS    = 8000;

static bool learned = false;
static int noiseRms = 0;
static int threshold = 0;
static uint32_t learnT0 = 0;
static int learnMaxRms = 0;

static bool speaking = false;
static int aboveCnt = 0;
static int belowCnt = 0;

static uint32_t speechStartMs = 0;

static uint32_t sttCooldownUntilMs = 0;
static uint32_t sttFailCount = 0;

// =====================================================
// SD fájlok
// =====================================================
static File wavFile;
static const char* WAV_PATH = "/rec.wav";
static uint32_t wavDataBytes = 0;

// TANÍTÁS MAP
static const char* MAP_FILE = "/voice_map.txt";
static const int MAX_MAP = 80;
static const int MAX_PHRASE = 96;
static const int MAX_LABEL  = 48;

struct MapItem {
  char phrase[MAX_PHRASE];
  char label[MAX_LABEL];
  bool used;
};
static MapItem mapItems[MAX_MAP];

static String pendingLabel = "";
static bool learnCaptureArmed = false;
static uint32_t teachArmMs = 0;

// =====================================================
// SEGÉDEK
// =====================================================
static bool headphoneConnected() { return digitalRead(PIN_HP_DET) == LOW; }
static void paPower(bool on) { digitalWrite(PIN_PA_EN, on ? HIGH : LOW); }

static String norm(const String& in) {
  String t = in;
  t.trim();
  t.toLowerCase();
  while (t.indexOf("  ") >= 0) t.replace("  ", " ");
  return t;
}

// =====================================================
// MAP kezelés
// =====================================================
static void clearMap() {
  for (int i = 0; i < MAX_MAP; i++) {
    mapItems[i].used = false;
    mapItems[i].phrase[0] = 0;
    mapItems[i].label[0] = 0;
  }
}

static int findByPhrase(const String& phrase) {
  String p = norm(phrase);
  for (int i = 0; i < MAX_MAP; i++) {
    if (!mapItems[i].used) continue;
    if (norm(String(mapItems[i].phrase)) == p) return i;
  }
  return -1;
}

static int firstFreeSlot() {
  for (int i = 0; i < MAX_MAP; i++) if (!mapItems[i].used) return i;
  return -1;
}

static bool addOrUpdateMap(const String& phraseIn, const String& labelIn) {
  String phrase = norm(phraseIn);
  String label  = norm(labelIn);
  if (phrase.length() < 2) return false;
  if (label.length()  < 2) return false;

  int idx = findByPhrase(phrase);
  if (idx < 0) {
    int slot = firstFreeSlot();
    if (slot < 0) return false;
    phrase.toCharArray(mapItems[slot].phrase, MAX_PHRASE);
    label.toCharArray(mapItems[slot].label, MAX_LABEL);
    mapItems[slot].used = true;
  } else {
    label.toCharArray(mapItems[idx].label, MAX_LABEL);
  }
  return true;
}

static void listMap() {
  Serial.println();
  Serial.println("---- TANITASOK LISTA (phrase -> label) ----");
  int c = 0;
  for (int i = 0; i < MAX_MAP; i++) {
    if (!mapItems[i].used) continue;
    Serial.print("  "); Serial.print(++c);
    Serial.print(") \""); Serial.print(mapItems[i].phrase);
    Serial.print("\"  ->  "); Serial.println(mapItems[i].label);
  }
  Serial.print("Osszesen: "); Serial.println(c);
  Serial.println("------------------------------------------");
  Serial.println();
}

static bool saveMapToSD() {
  if (SD_MMC.exists(MAP_FILE)) SD_MMC.remove(MAP_FILE);
  File f = SD_MMC.open(MAP_FILE, FILE_WRITE);
  if (!f) return false;

  f.println("# phrase|label");
  for (int i = 0; i < MAX_MAP; i++) {
    if (!mapItems[i].used) continue;
    f.print(mapItems[i].phrase); f.print("|"); f.println(mapItems[i].label);
  }
  f.close();
  return true;
}

static bool loadMapFromSD() {
  if (!SD_MMC.exists(MAP_FILE)) return false;
  File f = SD_MMC.open(MAP_FILE, FILE_READ);
  if (!f) return false;

  clearMap();
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length() || line.startsWith("#")) continue;

    int p = line.indexOf('|');
    if (p < 0) continue;

    String phrase = line.substring(0, p); phrase.trim();
    String label  = line.substring(p + 1); label.trim();
    addOrUpdateMap(phrase, label);
  }
  f.close();
  return true;
}

// =====================================================
// RMS + PEAK
// =====================================================
static void calcFrameRmsPeak(const int16_t* buf, int &rms, int &peak) {
  int32_t p = 0;
  int64_t sumsq = 0;

  for (int i = 0; i < FRAME_SAMPLES; i++) {
    int32_t v = buf[i];
    int32_t av = (v < 0) ? -v : v;
    if (av > p) p = av;
    sumsq += (int64_t)v * (int64_t)v;
  }

  uint32_t mean = (uint32_t)(sumsq / FRAME_SAMPLES);

  // integer sqrt
  uint32_t x = mean, r = 0, bit = 1UL << 30;
  while (bit > x) bit >>= 2;
  while (bit != 0) {
    if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
    else { r >>= 1; }
    bit >>= 2;
  }

  rms = (int)r;
  peak = (int)p;
}

// =====================================================
// I2S + ES8388 init
// =====================================================
static void setupMCLK() {
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
  REG_WRITE(PIN_CTRL, 0xFFFFFFF0);
}

static void setupI2S() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;

  // Arduino core kompatibilitás:
  // régi core: I2S_COMM_FORMAT_I2S / újabb: I2S_COMM_FORMAT_STAND_I2S
  #if defined(I2S_COMM_FORMAT_STAND_I2S)
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  #else
    cfg.communication_format = I2S_COMM_FORMAT_I2S;
  #endif

  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len   = 512;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;

  esp_err_t e = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  Serial.print("[I2S] driver_install err="); Serial.println((int)e);

  i2s_pin_config_t pin_config = {};
  pin_config.bck_io_num   = PIN_I2S_BCK;
  pin_config.ws_io_num    = PIN_I2S_WS;
  pin_config.data_out_num = PIN_I2S_DOUT;
  pin_config.data_in_num  = PIN_I2S_DIN;

  e = i2s_set_pin(I2S_PORT, &pin_config);
  Serial.print("[I2S] set_pin err="); Serial.println((int)e);

  e = i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  Serial.print("[I2S] set_clk err="); Serial.println((int)e);

  i2s_zero_dma_buffer(I2S_PORT);
  i2s_start(I2S_PORT);
}

static void setupES8388() {
  Serial.println("ES8388 init...");
  if (!es8388.init()) { Serial.println("ES8388 init FAIL"); return; }
  Serial.println("ES8388 init OK");

  es8388.inputSelect(IN2);
  es8388.setInputGain(8);

  es8388.outputSelect(OUT2);
  es8388.setOutputVolume(12);

  es8388.mixerSourceSelect(MIXADC, MIXADC);
  es8388.mixerSourceControl(SRCSELOUT);
  Serial.println("ES8388 ADC enabled");
}

// =====================================================
// WIFI + NTP (TLS-hez kell)
// =====================================================
static bool wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("WiFi csatlakozas");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - t0 > 15000) {
      Serial.println("\nWiFi TIMEOUT.");
      return false;
    }
  }
  Serial.println();
  Serial.print("WiFi OK, IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

static bool syncTimeNTP() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

  Serial.print("[TIME] NTP sync");
  uint32_t t0 = millis();
  time_t now = time(nullptr);

  while (now < 1700000000) {
    delay(250);
    Serial.print(".");
    now = time(nullptr);
    if (millis() - t0 > 15000) {
      Serial.println("\n[TIME] NTP TIMEOUT!");
      return false;
    }
  }

  Serial.println("\n[TIME] OK");
  struct tm t;
  gmtime_r(&now, &t);
  Serial.printf("[TIME] %04d-%02d-%02d %02d:%02d:%02d (UTC)\n",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);
  return true;
}

// =====================================================
// SD init
// =====================================================
static bool sdInit() {
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("[SD] SD_MMC begin FAIL");
    return false;
  }
  uint64_t cardSize = SD_MMC.cardSize() / (1024ULL * 1024ULL);
  Serial.print("[SD] OK, size(MB)=");
  Serial.println((uint32_t)cardSize);
  return true;
}

// =====================================================
// WAV header
// =====================================================
static void wavWriteHeaderPlaceholder(File &f) {
  uint8_t h[44]; memset(h, 0, sizeof(h));
  h[0]='R'; h[1]='I'; h[2]='F'; h[3]='F';
  h[8]='W'; h[9]='A'; h[10]='V'; h[11]='E';
  h[12]='f'; h[13]='m'; h[14]='t'; h[15]=' ';
  h[16]=16; h[20]=1; h[22]=1;

  uint32_t sr = SAMPLE_RATE;
  h[24]= (uint8_t)(sr & 0xFF);
  h[25]= (uint8_t)((sr >> 8) & 0xFF);
  h[26]= (uint8_t)((sr >> 16) & 0xFF);
  h[27]= (uint8_t)((sr >> 24) & 0xFF);

  uint16_t bps = 16, ch = 1;
  uint32_t byteRate = sr * ch * (bps/8);
  h[28]= (uint8_t)(byteRate & 0xFF);
  h[29]= (uint8_t)((byteRate >> 8) & 0xFF);
  h[30]= (uint8_t)((byteRate >> 16) & 0xFF);
  h[31]= (uint8_t)((byteRate >> 24) & 0xFF);

  uint16_t blockAlign = ch * (bps/8);
  h[32]= (uint8_t)(blockAlign & 0xFF);
  h[33]= (uint8_t)((blockAlign >> 8) & 0xFF);

  h[34]= (uint8_t)(bps & 0xFF);
  h[35]= (uint8_t)((bps >> 8) & 0xFF);

  h[36]='d'; h[37]='a'; h[38]='t'; h[39]='a';
  f.write(h, 44);
}

static void wavFinalizeHeader(File &f, uint32_t dataBytes) {
  uint32_t riffSize = 36 + dataBytes;
  f.seek(4);  f.write((uint8_t*)&riffSize, 4);
  f.seek(40); f.write((uint8_t*)&dataBytes, 4);
  f.seek(0, SeekEnd);
}

// =====================================================
// VAD reset
// =====================================================
static void vadResetLearn() {
  learned = false;
  noiseRms = 0;
  threshold = 0;
  learnT0 = millis();
  learnMaxRms = 0;

  speaking = false;
  aboveCnt = 0;
  belowCnt = 0;

  Serial.println("[VAD] Zajtanulas ujrainditva...");
}

// =====================================================
// TLS config helper
// =====================================================
// =====================================================
// TLS config helper (CORE-KOMPATIBILIS)
// =====================================================
static void applyTlsConfig(WiFiClientSecure &client) {
  client.setTimeout(35);
  client.setHandshakeTimeout(35);

#if HAVE_CRT_BUNDLE
  Serial.println("[TLS] CRT bundle: ON");
  client.setCACertBundle(esp_crt_bundle_attach);

#else
  Serial.println("[TLS] CRT bundle: NINCS");

  if (ALLOW_INSECURE_TLS_WHEN_NO_BUNDLE) {
    Serial.println("[TLS] FIGYELEM: ideiglenes INSECURE TLS (teszt)!");

    // ESP32 Arduino core 3.x környékén már jellemzően van setInsecure()
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
      client.setInsecure();
    #else
      client.setCACert(nullptr);
    #endif

  } else {
    Serial.println("[TLS] Nincs CA -> TLS nem fog menni.");
  }
#endif
}

// =====================================================
// OpenAI STT (WAV -> text)
// =====================================================
static String openaiTranscribeWav(const char* pathOnSd) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[STT] Nincs WiFi.");
    return "";
  }
    if (millis() < sttCooldownUntilMs) {
  Serial.print("[STT] Cooldown aktiv, varj meg ennyit (ms): ");
  Serial.println(sttCooldownUntilMs - millis());
  return "";
}
  if (strlen(OPENAI_API_KEY) < 20) {
    Serial.println("[STT] Nincs API kulcs beallitva.");
    return "";
  }

  File f = SD_MMC.open(pathOnSd, FILE_READ);
  if (!f) {
    Serial.println("[STT] WAV file megnyitas FAIL.");
    return "";
  }

  IPAddress ip;
  if (!WiFi.hostByName(OPENAI_HOST, ip)) {
    Serial.println("[STT] DNS FAIL (hostByName)");
    f.close();
    return "";
  }
  Serial.print("[STT] OPENAI IP: ");
  Serial.println(ip);

  String boundary = "----EliehBoundary7MA4YWxkTrZu0gW";
  String head =
    String("--") + boundary + "\r\n" +
    "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
    String(OPENAI_MODEL) + "\r\n" +
    String("--") + boundary + "\r\n" +
    "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n" +
    "text\r\n" +
    String("--") + boundary + "\r\n" +
    "Content-Disposition: form-data; name=\"file\"; filename=\"rec.wav\"\r\n" +
    "Content-Type: audio/wav\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";

  uint32_t fileSize = (uint32_t)f.size();
  uint32_t contentLength = head.length() + fileSize + tail.length();

  WiFiClientSecure client;
  applyTlsConfig(client);

  Serial.println("[STT] Kapcsolodas OpenAI...");
  if (!client.connect(OPENAI_HOST, OPENAI_PORT)) {
    Serial.println("[STT] connect FAIL (TLS)");
    f.close();
    return "";
  }

  client.print(String("POST ") + OPENAI_PATH + " HTTP/1.1\r\n");
  client.print(String("Host: ") + OPENAI_HOST + "\r\n");
  client.print("User-Agent: Elieh-A1S\r\n");
  client.print(String("Authorization: Bearer ") + OPENAI_API_KEY + "\r\n");
  client.print(String("Content-Type: multipart/form-data; boundary=") + boundary + "\r\n");
  client.print(String("Content-Length: ") + contentLength + "\r\n");
  client.print("Connection: close\r\n\r\n");

  client.print(head);

  uint8_t buf[1024];
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    if (n > 0) client.write(buf, n);
    delay(0);
  }
  f.close();

  client.print(tail);

  String resp;
  uint32_t t0 = millis();
  while (client.connected() && (millis() - t0 < 30000)) {
    while (client.available()) {
      resp += (char)client.read();
      t0 = millis();
    }
    delay(5);
  }
  client.stop();

  int pLineEnd = resp.indexOf("\r\n");
  if (pLineEnd > 0) {
    String statusLine = resp.substring(0, pLineEnd);
    Serial.print("[STT] HTTP: ");
    Serial.println(statusLine);
  }

  int bodyPos = resp.indexOf("\r\n\r\n");
String body = (bodyPos >= 0) ? resp.substring(bodyPos + 4) : resp;
body.trim();
Serial.print("[STT] body len = ");
Serial.println(body.length());
Serial.println("[STT] body (elso 200):");
Serial.println(body.substring(0, min(200, (int)body.length())));
    
// ---- HTTP status code kinyerése ----
int httpCode = 0;
int sp1 = resp.indexOf(' ');
if (sp1 > 0) {
  int sp2 = resp.indexOf(' ', sp1 + 1);
  if (sp2 > sp1) {
    httpCode = resp.substring(sp1 + 1, sp2).toInt();
  }
}

if (httpCode == 429) {
  // 429 lehet rate-limit vagy insufficient_quota – nálad QUOTA
  Serial.println("[STT] 429 -> tul sok / quota / billing hiba.");
  Serial.println("[STT] Valasz (JSON/hiba):");
  Serial.println(body);

  // ha quota, akkor hosszabb cooldown, hogy ne spam-eljünk
  if (body.indexOf("insufficient_quota") >= 0) {
    sttCooldownUntilMs = millis() + 5UL * 60UL * 1000UL; // 5 perc
    Serial.println("[STT] INSUFFICIENT_QUOTA: 5 perc cooldown.");
  } else {
    sttCooldownUntilMs = millis() + 30UL * 1000UL; // 30 mp
    Serial.println("[STT] RATE LIMIT: 30s cooldown.");
  }

  sttFailCount++;
  return "";
}

if (httpCode >= 400) {
  Serial.print("[STT] HTTP HIBA: "); Serial.println(httpCode);
  Serial.println("[STT] Valasz:");
  Serial.println(body);

  // rövid cooldown a többi hibára is
  sttCooldownUntilMs = millis() + 20UL * 1000UL;
  sttFailCount++;
  return "";
}

// Ha JSON, még lehet error
if (body.startsWith("{") && body.indexOf("\"error\"") >= 0) {
  Serial.println("[STT] JSON error valasz:");
  Serial.println(body);
  sttFailCount++;
  sttCooldownUntilMs = millis() + 20UL * 1000UL;
  return "";
}

sttFailCount = 0;
return body;
}
// =====================================================
// “Végrehajtás”
// =====================================================
static void executeLabel(const String& label) {
  Serial.print("[VEGREHAJTAS] ");
  Serial.println(label);
  Serial.println("[STATUS] VEGREHAJTOTTA.");
}

// =====================================================
// Audio frame feldolgozás
// =====================================================
static void processAudioFrame() {
  if (menuSel == MENU_LEARN && learnCaptureArmed) {
    if (millis() - teachArmMs > 12000) {
      learnCaptureArmed = false;
      Serial.println("[TANITAS] TEACH timeout (nem beszeltel).");
    }
  }

  size_t bytesRead = 0;
  esp_err_t e = i2s_read(I2S_PORT, (void*)samples, sizeof(samples), &bytesRead, 200 / portTICK_PERIOD_MS);
  if (e != ESP_OK || bytesRead != sizeof(samples)) {
    static uint32_t last = 0;
    if (millis() - last > 1200) {
      last = millis();
      Serial.print("[I2S] read err="); Serial.print((int)e);
      Serial.print(" bytes="); Serial.println((int)bytesRead);
    }
    return;
  }

  memcpy(preroll[prerollIdx], samples, sizeof(samples));
  prerollIdx = (prerollIdx + 1) % PREROLL_FRAMES;

  int rms = 0, peak = 0;
  calcFrameRmsPeak(samples, rms, peak);

  if (!learned) {
    if (rms > learnMaxRms) learnMaxRms = rms;

    if (millis() - learnT0 >= (uint32_t)LEARN_MS) {
      learned = true;
      noiseRms = learnMaxRms;
      threshold = (int)(noiseRms * TH_MULT) + TH_MARGIN;

      Serial.print("[VAD] Zaj RMS tanult="); Serial.println(noiseRms);
      Serial.print("[VAD] Kuszob TH="); Serial.println(threshold);
      Serial.println("[VAD] Most beszelhetsz.");
    }
    return;
  }

  bool above = (rms > threshold) || (peak > threshold);
  if (above) { aboveCnt++; belowCnt = 0; }
  else       { belowCnt++; aboveCnt = 0; }

  bool wantRecordNow =
    (menuSel == MENU_RUN) ||
    (menuSel == MENU_LEARN && learnCaptureArmed);

  if (!speaking && wantRecordNow && aboveCnt >= START_FRAMES) {
    speaking = true;
    speechStartMs = millis();
    wavDataBytes = 0;

    if (SD_MMC.exists(WAV_PATH)) SD_MMC.remove(WAV_PATH);
    wavFile = SD_MMC.open(WAV_PATH, FILE_WRITE);
    if (!wavFile) {
      Serial.println("[WAV] open FAIL");
      speaking = false;
      return;
    }
    wavWriteHeaderPlaceholder(wavFile);

    for (int k = 0; k < PREROLL_FRAMES; k++) {
      int idx = (prerollIdx + k) % PREROLL_FRAMES;
      size_t w = wavFile.write((uint8_t*)preroll[idx], sizeof(preroll[idx]));
      wavDataBytes += (uint32_t)w;
    }

    if (menuSel == MENU_LEARN) Serial.println(">>> TANITAS BESZED INDUL (WAV mentes)");
    else                       Serial.println(">>> BESZED INDUL (WAV mentes SD-re)");
  }

  if (speaking) {
    size_t w = wavFile.write((uint8_t*)samples, sizeof(samples));
    wavDataBytes += (uint32_t)w;

    if (millis() - speechStartMs > MAX_SPEECH_MS) {
      belowCnt = END_FRAMES;
    }
  }

  if (speaking && belowCnt >= END_FRAMES) {
    speaking = false;

    if (wavFile) {
      wavFinalizeHeader(wavFile, wavDataBytes);
      wavFile.close();
    }

    Serial.print("<<< BESZED VEGE, wav bytes=");
    Serial.println(wavDataBytes);

    if (wavDataBytes < MIN_SPEECH_BYTES) {
      Serial.println("[WAV] Tul rovid / ures, eldobva.");
      if (menuSel == MENU_LEARN && learnCaptureArmed) {
        learnCaptureArmed = false;
        Serial.println("[TANITAS] NINCS ELTAROLVA (rovid hang).");
      }
      return;
    }

    String text = openaiTranscribeWav(WAV_PATH);
    text = norm(text);

    if (text.length() < 2) {
  Serial.println("[STT] Nincs szoveg. (Valoszinuleg quota/rate-limit/billing vagy halo).");
  if (menuSel == MENU_LEARN && learnCaptureArmed) {
    learnCaptureArmed = false;
    Serial.println("[TANITAS] NINCS ELTAROLVA (STT hiba / quota).");
  }
  return;
}

    Serial.println("[STT TEXT]");
    Serial.println(text);

    if (menuSel == MENU_LEARN && learnCaptureArmed) {
      learnCaptureArmed = false;

      if (pendingLabel.length() < 2) {
        Serial.println("[TANITAS] NINCS ELTAROLVA (nincs beirt cimke).");
        return;
      }

      bool ok = addOrUpdateMap(text, pendingLabel);
      if (ok) {
        bool saved = saveMapToSD();
        Serial.print("[TANITAS] ELTAROLVA: \"");
        Serial.print(text);
        Serial.print("\" -> ");
        Serial.println(pendingLabel);
        Serial.println(saved ? "[SD] Mentve." : "[SD] Mentes HIBA!");
        listMap();
      } else {
        Serial.println("[TANITAS] NINCS ELTAROLVA (nincs hely / hibas adat).");
      }
      return;
    }

    if (menuSel == MENU_RUN) {
      int idx = findByPhrase(text);
      if (idx >= 0) {
        Serial.print("[FELISMERES] OK: ");
        Serial.print(text);
        Serial.print(" -> ");
        Serial.println(mapItems[idx].label);
        executeLabel(String(mapItems[idx].label));
      } else {
        Serial.print("[FELISMERES] NEM FELISMERHETO: ");
        Serial.println(text);
      }
    }
  }
}

// =====================================================
// SOROS parancsok
// =====================================================
static void printHelp() {
  Serial.println();
  Serial.println("=== SOROS PARANCSOK ===");
  Serial.println("Beirt sor (pl: konyha be) -> cimke (pendingLabel)");
  Serial.println("LIST  -> tanitasok listazasa");
  Serial.println("CLEAR -> osszes tanitas torlese (memoria+SD)");
  Serial.println("HELP  -> ez");
  Serial.println("=======================");
  Serial.println();
}

static String serialLine;

static void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLine.length() > 0) {
        String line = serialLine;
        serialLine = "";
        line.trim();
        if (line.length() == 0) return;

        if (line.equalsIgnoreCase("HELP")) { printHelp(); return; }
        if (line.equalsIgnoreCase("LIST")) { listMap(); return; }

        if (line.equalsIgnoreCase("CLEAR")) {
          clearMap();
          if (SD_MMC.exists(MAP_FILE)) SD_MMC.remove(MAP_FILE);
          Serial.println("[CLEAR] Tanitasok torolve (memoria+SD).");
          return;
        }

        pendingLabel = norm(line);
        Serial.print("[CIMKE] Beallitva: ");
        Serial.println(pendingLabel);
        Serial.println("[TANITAS] LEARN modban nyomd meg a TEACH gombot, majd mondd ki a parancsot.");
      }
    } else {
      serialLine += c;
      if (serialLine.length() > 200) serialLine.remove(0, 80);
    }
  }
}

// =====================================================
// SETUP / LOOP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(800);

  pinMode(PIN_PA_EN, OUTPUT);
  digitalWrite(PIN_PA_EN, LOW);
  pinMode(PIN_HP_DET, INPUT_PULLUP);

  btnInit(btnMenu);
  btnInit(btnNext);
  btnInit(btnOk);
  btnInit(btnTeach);

  Serial.println();
  Serial.println("=== ELIEH A1S VOICE v2.4 (COMPAT + TLS FIX) ===");

  bool wifiOk = wifiConnect();
  if (wifiOk) {
    syncTimeNTP();
  } else {
    Serial.println("[WIFI] Nincs WiFi -> STT nem fog menni.");
  }

  if (!sdInit()) {
    Serial.println("[SD] Nincs SD, tanitas/mentes nem fog menni rendesen.");
  } else {
    bool ok = loadMapFromSD();
    Serial.println(ok ? "[SD] Tanitasok betoltve." : "[SD] Nincs mentett tanitas (vagy ures).");
  }

  setupMCLK();
  setupI2S();
  setupES8388();

  printHelp();
  printMenu();

  vadResetLearn();

  Serial.println("[I2S] teszt olvasas...");
  size_t br=0;
  int16_t tmp[256];
  esp_err_t e = i2s_read(I2S_PORT, tmp, sizeof(tmp), &br, 200 / portTICK_PERIOD_MS);
  Serial.print("[I2S] test err="); Serial.print((int)e);
  Serial.print(" bytes="); Serial.println((int)br);

  if (menuSel == MENU_RUN) {
    Serial.println("[RUN] Eddigi tanitasok:");
    listMap();
  }
}

void loop() {
  pollSerial();

  static bool lastPA = false;
  bool wantPA = !headphoneConnected();
  if (wantPA != lastPA) {
    lastPA = wantPA;
    paPower(wantPA);
    Serial.print("[AUDIO] PA=");
    Serial.println(wantPA ? "ON" : "OFF");
  }

  btnUpdate(btnMenu,  DEBOUNCE_MS, LONGPRESS_MS);
  btnUpdate(btnNext,  DEBOUNCE_MS, LONGPRESS_MS);
  btnUpdate(btnOk,    DEBOUNCE_MS, LONGPRESS_MS);
  btnUpdate(btnTeach, DEBOUNCE_MS, LONGPRESS_MS);

  if (btnPressed(btnMenu)) {
    printMenu();
    if (menuSel == MENU_RUN) listMap();
    if (menuSel == MENU_LEARN) {
      Serial.println("[LEARN] Irj be soroson egy cimket (pl: konyha be), majd TEACH, es mondd ki.");
    }
  }

  if (btnPressed(btnNext)) {
    menuSel++;
    if (menuSel >= MENU_COUNT) menuSel = 0;
    printMenu();
    if (menuSel == MENU_RUN) {
      Serial.println("[RUN] Eddigi tanitasok:");
      listMap();
    }
    if (menuSel == MENU_LEARN) {
      Serial.println("[LEARN] Irj be soroson egy cimket, majd TEACH.");
    }
  }

  if (btnPressed(btnOk)) {
    Serial.print("[MENU] Kivalasztva: ");
    Serial.println(menuName(menuSel));
    if (menuSel == MENU_RUN) {
      Serial.println("[RUN] Beszelj, felismeres indul.");
      listMap();
    } else if (menuSel == MENU_LEARN) {
      Serial.println("[LEARN] Cimke most: " + pendingLabel);
      Serial.println("[LEARN] Nyomd meg TEACH-et a tanitashoz.");
    }
  }

  if (btnPressed(btnTeach)) {
    if (menuSel != MENU_LEARN) {
      Serial.println("[TANITAS] Csak LEARN modban mukodik.");
    } else if (pendingLabel.length() < 2) {
      Serial.println("[TANITAS] Elobb irj be soroson egy cimket (pl: konyha be)!");
    } else {
      Serial.print("[TANITAS] TEACH ARM. Cimke: ");
      Serial.println(pendingLabel);
      Serial.println("[TANITAS] Most mondd ki a parancsot (VAD indul) ...");

      learnCaptureArmed = true;
      teachArmMs = millis();

      speaking = false;
      aboveCnt = 0;
      belowCnt = 0;
    }
  }

  processAudioFrame();
}
