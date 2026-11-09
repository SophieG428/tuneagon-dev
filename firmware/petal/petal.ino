// ============================================================================
//  Tuneagon PETAL firmware v3 "light touch"  (ESP32-C3 SuperMini, one per petal)
// ----------------------------------------------------------------------------
//  v2 -> v3: with keycaps resting on the FSRs, a press only moves the sensor a
//  little, so v2's fixed threshold missed light presses. v3:
//    * oversamples + low-pass filters each key, so the noise floor is lower
//    * threshold = max(floor, k x noise), set by a SENSITIVITY level 1..9
//      (default 6; v2 behaved like about level 3)
//    * a press must stay above threshold for 3 ms (rejects single-sample spikes)
//    * slow baseline tracking at rest (keycap settling / FSR creep can't
//      creep into a phantom note), frozen while a key is pressed or half-pressed
//    * lighter presses reach louder velocities (FULL_MIN 0.20 -> 0.10)
//  Hub packet, I2C address and pins are unchanged, so the hub and app need no
//  changes to use it.
//
//  USB serial (115200), optional, for tuning by feel:
//    prints "raw:" lines 10x/s: each key's signal above rest and its threshold
//    send "s7" + Enter to set sensitivity 7 (1 = least, 9 = most). It is saved
//    in the board's flash and survives power-off. "s?" prints the current level.
//
//  Wiring is unchanged (see Tunagon_Assembly_final_wiring_v2.pdf).
//  Message to the hub (14 bytes):
//    [0] 0xA5  [1] key-down bits  [2..7] expression per key  [8..13] velocity
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>

#ifndef PETAL_ID
#define PETAL_ID 1          // set per petal at build time with -DPETAL_ID=N
#endif

const uint8_t I2C_ADDR = 0x10 + PETAL_ID;      // petal 1 = 0x11 ... petal 30 = 0x2E
const int NK = 6;
const int KEY_PIN[NK] = {0, 1, 2, 3, 4, 5};
const int SDA_PIN = 6, SCL_PIN = 7;
const int LED_PIN = 8;                          // on-board blue LED (on when LOW)
const int DIGITAL_KEY = 5;                      // key A: on/off only
const uint8_t DIGITAL_VEL = 100;

// feel
const float FULL_MIN   = 0.10f;     // v2: 0.20. Smaller = light presses sound louder
const float FULL_MAX   = 0.85f;
const float FULL_RELAX = 0.00005f;  // per ms
const float CURVE      = 2.4f;

// detection
const int   OVERSAMPLE  = 8;        // ADC reads averaged per key per ms (v2: 4)
const float FILTER_A    = 0.5f;     // 1-pole low-pass on each key, per ms
const int   DEBOUNCE_MS = 3;        // must stay above threshold this long
const float BASE_ADAPT  = 0.0005f;  // per ms (~2 s) baseline tracking at rest
const float BASE_DOWN   = 0.02f;    // per ms: follow quickly when the rest level drops
const int   SENS_DEFAULT = 6;

struct Key {
  float base = 0, nz = 0, filt = 0;
  float on_th = 0.01f, off_th = 0.005f, full = FULL_MIN;
  float smooth = 0, sustain = 0, peak = 0;
  bool on = false, judged = false;
  uint8_t above = 0;
  uint32_t t0 = 0;
  uint8_t expr = 0, vel = 0;
};
Key key[NK];
int sens = SENS_DEFAULT;
Preferences prefs;

volatile uint8_t pkt[14] = {0xA5};
portMUX_TYPE pktLock = portMUX_INITIALIZER_UNLOCKED;

static inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

// 0 = untouched, 1 = sensor fully conducting
static float readKey(int i) {
  if (i == DIGITAL_KEY) return digitalRead(KEY_PIN[i]) == LOW ? 1.0f : 0.0f;
  uint32_t s = 0;
  for (int n = 0; n < OVERSAMPLE; n++) s += analogRead(KEY_PIN[i]);
  return 1.0f - ((float)s / OVERSAMPLE) / 4095.0f;
}

// sensitivity 1..9 -> threshold. Level 1: max(0.016, 5.0 x noise);
// level 9: max(0.004, 2.2 x noise). Default 6: max(0.0085, 3.25 x noise).
static void applySensitivity() {
  float mult   = 5.0f - 0.35f * (sens - 1);
  float floor_ = 0.016f - 0.0015f * (sens - 1);
  for (int i = 0; i < NK; i++) {
    if (i == DIGITAL_KEY) { key[i].on_th = 0.5f; key[i].off_th = 0.25f; continue; }
    key[i].on_th  = fmaxf(floor_, mult * key[i].nz);
    key[i].off_th = key[i].on_th * 0.5f;
  }
}

void onRequest() {
  uint8_t out[14];
  portENTER_CRITICAL(&pktLock);
  for (int i = 0; i < 14; i++) out[i] = pkt[i];
  portEXIT_CRITICAL(&pktLock);
  Wire.write(out, 14);
}

static void handleSerial() {
  static char buf[8]; static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf[n] = 0;
      if (n >= 2 && (buf[0] == 's' || buf[0] == 'S')) {
        if (buf[1] >= '1' && buf[1] <= '9') {
          sens = buf[1] - '0';
          prefs.putInt("sens", sens);
          applySensitivity();
        }
        Serial.printf("sensitivity: %d\n", sens);
      }
      n = 0;
    } else if (n < 7) buf[n++] = c;
  }
}

static void printStatus() {
  if (!Serial || Serial.availableForWrite() < 128) return;   // nobody listening
  char line[200]; int p = 0;
  p += snprintf(line + p, sizeof(line) - p, "raw: s%d", sens);
  for (int i = 0; i < NK; i++) {
    float v = key[i].filt - key[i].base; if (v < 0) v = 0;
    p += snprintf(line + p, sizeof(line) - p, " | %c %.3f/%.3f%s", "CDEFGA"[i], v,
                  key[i].on_th, key[i].on ? "*" : "");
  }
  Serial.println(line);
}

void setup() {
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(KEY_PIN[DIGITAL_KEY], INPUT_PULLUP);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  prefs.begin("tuneagon", false);
  sens = prefs.getInt("sens", SENS_DEFAULT);
  if (sens < 1 || sens > 9) sens = SENS_DEFAULT;

  // 3 blinks = hello (don't touch the keys: measuring resting levels next)
  for (int i = 0; i < 3; i++) { digitalWrite(LED_PIN, LOW); delay(120); digitalWrite(LED_PIN, HIGH); delay(120); }

  // settle the filters, then measure resting level and noise of the filtered signal
  for (int i = 0; i < NK; i++) key[i].filt = readKey(i);
  for (int n = 0; n < 50; n++) { for (int i = 0; i < NK; i++) key[i].filt += FILTER_A * (readKey(i) - key[i].filt); delay(1); }
  const int N = 300;
  float sum[NK] = {0}, sum2[NK] = {0};
  for (int n = 0; n < N; n++) {
    for (int i = 0; i < NK; i++) {
      key[i].filt += FILTER_A * (readKey(i) - key[i].filt);
      sum[i] += key[i].filt; sum2[i] += key[i].filt * key[i].filt;
    }
    delay(1);
  }
  for (int i = 0; i < NK; i++) {
    key[i].base = sum[i] / N;
    key[i].nz   = sqrtf(fmaxf(sum2[i] / N - key[i].base * key[i].base, 0.f));
  }
  applySensitivity();

  Wire.onRequest(onRequest);
  Wire.begin(I2C_ADDR, SDA_PIN, SCL_PIN, 400000);   // join the shared bus as a petal
}

void loop() {
  static uint32_t last = 0, lastPrint = 0;
  uint32_t now = millis();
  if (now == last) return;            // run the key logic once per millisecond
  last = now;

  bool any = false;
  for (int i = 0; i < NK; i++) {
    Key &K = key[i];
    float raw = readKey(i);
    if (i == DIGITAL_KEY) K.filt = raw;
    else K.filt += FILTER_A * (raw - K.filt);
    float v = K.filt - K.base; if (v < 0) v = 0;

    K.sustain += 0.02f * (v - K.sustain);
    if (K.sustain > K.full) K.full = fminf(K.sustain, FULL_MAX);
    else if (!K.on && K.full > FULL_MIN) K.full -= FULL_RELAX;
    float p = clamp01((v - K.off_th) / fmaxf(K.full - K.off_th, 1e-4f));
    K.smooth += 0.25f * (p - K.smooth);

    if (!K.on) {
      K.above = (v > K.on_th) ? (uint8_t)min(K.above + 1, 255) : 0;
      if (K.above >= (i == DIGITAL_KEY ? 1 : DEBOUNCE_MS)) {
        K.on = true; K.judged = false; K.t0 = now; K.peak = p;
      }
    }
    if (K.on && !K.judged) {
      if (p > K.peak) K.peak = p;
      if (now - K.t0 >= 8) {                        // judge the strike over 8 ms
        K.vel  = 90 + (uint8_t)(powf(clamp01(K.peak), 0.8f) * 37.f);
        K.expr = 1 + (uint8_t)(powf(clamp01(K.peak), CURVE) * 126.f);
        if (i == DIGITAL_KEY) { K.vel = DIGITAL_VEL; K.expr = DIGITAL_VEL; }
        K.judged = true;
      }
    }
    if (K.on && v < K.off_th) { K.on = false; K.judged = false; K.expr = 0; K.above = 0; }
    if (K.on && K.judged) K.expr = (i == DIGITAL_KEY) ? DIGITAL_VEL : 1 + (uint8_t)(powf(K.smooth, CURVE) * 126.f);

    // baseline tracking at rest only (never while pressed or half-pressed)
    if (i != DIGITAL_KEY && !K.on) {
      if (K.filt < K.base)               K.base += BASE_DOWN  * (K.filt - K.base);
      else if (v < K.off_th && !K.above) K.base += BASE_ADAPT * (K.filt - K.base);
    }

    if (K.on) any = true;
  }

  uint8_t bits = 0;
  for (int i = 0; i < NK; i++) if (key[i].on && key[i].judged) bits |= (1 << i);
  portENTER_CRITICAL(&pktLock);
  pkt[0] = 0xA5;
  pkt[1] = bits;
  for (int i = 0; i < NK; i++) { pkt[2 + i] = key[i].expr; pkt[8 + i] = key[i].vel; }
  portEXIT_CRITICAL(&pktLock);

  digitalWrite(LED_PIN, any ? LOW : HIGH);

  handleSerial();
  if (now - lastPrint >= 100) { lastPrint = now; printStatus(); }
}
