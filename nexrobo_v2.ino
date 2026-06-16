// ============================================================
//  NexRobo v2.0  —  ESP32-C3 | 1.3-inch OLED (SH1106 128×64)
//  Cute Desk Pet Robot: BLE Control, Touch Sensor,
//  24 Face Animations + Touch Reactions + Special Effects.
//  Original mouth style & order preserved exactly.
//
//  Required libraries (install via Arduino Library Manager):
//    • Adafruit GFX Library
//    • Adafruit SH110X          ← for SH1106 1.3-inch OLED
//    • ESP32 BLE Arduino        ← included with ESP32 board pkg
//
//  Touch sensor: wire a TTP223 (or similar digital module)
//    VCC → 3.3V,  GND → GND,  OUT → GPIO 5
//    Module output goes HIGH when touched.
//
//  If your display uses SSD1306 instead of SH1106:
//    1. Replace  #include <Adafruit_SH110X.h>
//       with     #include <Adafruit_SSD1306.h>
//    2. Replace  Adafruit_SH1106G display(...)
//       with     Adafruit_SSD1306  display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET)
//    3. Replace  display.begin(OLED_ADDR, true)
//       with     display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)
//    4. Replace  OLED_WHITE / OLED_BLACK
//       with     SSD1306_WHITE / SSD1306_BLACK
// ============================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>

// ── Display ─────────────────────────────────────────────────
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_ADDR      0x3C   // try 0x3D if 0x3C doesn't work
#define OLED_RESET     (-1)
#define OLED_WHITE     SH110X_WHITE
#define OLED_BLACK     SH110X_BLACK

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── Hardware Pins ────────────────────────────────────────────
#define SDA_PIN      3
#define SCL_PIN      4
#define TOUCH_PIN    5   // TTP223 digital touch sensor

// ── Face Geometry (scaled for 128×64 display) ────────────────
const int EYE_W             = 26;
const int EYE_H_NEUTRAL     = 22;
const int EYE_BASE_Y        = 26;   // vertical center of both eyes
const int LEFT_EYE_BASE_X   = 15;
const int RIGHT_EYE_BASE_X  = 87;
const int MOUTH_BASE_X      = 52;
const int MOUTH_Y           = 47;
const int MOUTH_W           = 24;
const int MOUTH_H           = 7;

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// ── Timing Constants ─────────────────────────────────────────
const uint32_t FIXED_ANIM_MS     = 6000;
const uint32_t GAP_MIN_MS        = 5000;
const uint32_t GAP_MAX_MS        = 9000;
const uint32_t BLINK_PERIOD_MS   = 3200;
const uint32_t BLINK_CLOSE_MS    = 110;
const uint32_t BLINK_HOLD_MS     = 70;
const uint32_t BLINK_OPEN_MS     = 110;
const uint32_t BLINK_TOTAL_MS    = BLINK_CLOSE_MS + BLINK_HOLD_MS + BLINK_OPEN_MS;
const uint32_t FRAME_DELAY_MS    = 16;
const float    SMOOTH_ALPHA      = 0.18f;

// ── Animation IDs ────────────────────────────────────────────
enum AnimType : uint8_t {
  ANIM_NONE = 0,
  // ── Original 24 face expressions (order preserved) ────────
  ANIM_SADNESS, ANIM_ANGER, ANIM_HAPPINESS, ANIM_PLEADING,
  ANIM_VULNERABLE, ANIM_DESPAIR, ANIM_SURPRISE, ANIM_DISGUST,
  ANIM_FEAR, ANIM_GUILTY, ANIM_DISAPPOINTED, ANIM_EMBARRASSED,
  ANIM_HORRIFIED, ANIM_SKEPTICAL, ANIM_ANNOYED, ANIM_CONFUSED,
  ANIM_AMAZED, ANIM_EXCITED, ANIM_FURIOUS, ANIM_SUSPICIOUS,
  ANIM_REJECTED, ANIM_BORED, ANIM_TIRED, ANIM_ASLEEP,   // 1-24
  // ── Touch pet reactions (100-105, not in shuffle queue) ───
  ANIM_TOUCH_SINGLE    = 100,   // one tap   → quick blink + smile + bob
  ANIM_TOUCH_DOUBLE    = 101,   // two taps  → happier, wider eyes
  ANIM_TOUCH_LONG      = 102,   // hold      → shy / affectionate
  ANIM_TOUCH_RUB       = 103,   // many taps → warm pet bounce
  ANIM_TOUCH_FAST      = 104,   // rapid taps→ excited / surprised
  ANIM_TOUCH_IDLE_WAKE = 105,   // touch after idle → soft wake-up
};

// ── Smooth Eye/Mouth State ───────────────────────────────────
float actLeftOffsetX = 0,  tgtLeftOffsetX = 0;
float actRightOffsetX = 0, tgtRightOffsetX = 0;
float actOffsetY = 0,      tgtOffsetY = 0;
float actLeftH  = EYE_H_NEUTRAL, tgtLeftH  = EYE_H_NEUTRAL;
float actRightH = EYE_H_NEUTRAL, tgtRightH = EYE_H_NEUTRAL;
float actMouthOffsetX = 0, tgtMouthOffsetX = 0;
int   actMouthVariant = 0, tgtMouthVariant = 0;
float actAngleL = 0,    tgtAngleL = 0;
float actAngleR = 0,    tgtAngleR = 0;
float actBottomCutL = 0, tgtBottomCutL = 0;
float actBottomCutR = 0, tgtBottomCutR = 0;
float actTopCutL = 0,   tgtTopCutL = 0;
float actTopCutR = 0,   tgtTopCutR = 0;

// ── Animation Controller ─────────────────────────────────────
AnimType currentAnim    = ANIM_NONE;
AnimType lastAnim       = ANIM_NONE;
uint32_t animStartMs    = 0;
uint32_t animDurationMs = 0;
uint32_t gapStartMs     = 0;
uint32_t gapDurationMs  = 0;
bool     inGap          = true;

// ── Blink State ──────────────────────────────────────────────
uint32_t lastBlinkTrigger = 0;
uint32_t blinkStartMs     = 0;
bool     blinking         = false;

// ── Idle Eye Drift ───────────────────────────────────────────
float    idleDriftX   = 0.0f;
uint32_t nextDriftMs  = 0;

// ── Special Effect Overlays ───────────────────────────────────
bool     specialFxActive   = false;
uint8_t  specialFxType     = 0;   // 1=scan, 2=glitch shift, 3=pixel noise
uint32_t specialFxStartMs  = 0;
uint32_t nextSpecialFxMs   = 0;
const uint32_t SPECIAL_FX_MS = 1100;

// ── Touch Sensor State ───────────────────────────────────────
bool     touchRaw          = false;
bool     touchState        = false;
uint32_t touchDebounceMs   = 0;
const uint32_t DEBOUNCE_MS = 40;

bool     fingerDown        = false;
uint32_t fingerDownMs      = 0;
uint32_t lastFingerUpMs    = 0;
bool     longPressFired    = false;
int      tapCount          = 0;
uint32_t firstTapMs        = 0;

const uint32_t TAP_WINDOW_MS = 480;
const uint32_t LONG_PRESS_MS = 1100;
const int      RUB_TAPS      = 6;
const int      EXCITED_TAPS  = 4;

bool     inTouchAnim       = false;
AnimType touchAnimType     = ANIM_NONE;
uint32_t touchAnimStartMs  = 0;
uint32_t touchAnimDurMs    = 0;

uint32_t lastActivityMs    = 0;
const uint32_t IDLE_SLEEP_MS = 35000;  // 35 s idle → "sleepy" state

// ── BLE ──────────────────────────────────────────────────────
BLEServer*         pServer   = nullptr;
BLEService*        pService  = nullptr;
BLECharacteristic* pPassChar = nullptr;
BLECharacteristic* pCmdChar  = nullptr;
BLECharacteristic* pTextChar = nullptr;

volatile bool bleClientConnected  = false;
volatile bool bleClientAuthorized = false;
bool          bleConnectAnimActive = false;
uint32_t      bleConnectAnimStart  = 0;
const uint32_t BLE_CONNECT_MS     = 3000;

volatile bool webManualMode = false;
AnimType      webManualAnim = ANIM_NONE;

// ── Custom Text (persistent until BLE sends CLEAR) ───────────
volatile bool  customTextActive   = false;
String         customTextMessage  = "";
uint32_t       customTextStartMs  = 0;
const uint32_t CT_GLITCH_MS       = 650;   // glitch intro duration
const int      CT_MAX_LEN         = 8;

// ── BLE UUIDs ────────────────────────────────────────────────
const char* SERVICE_UUID = "12345678-1234-1234-1234-123456789abc";
const char* PASS_UUID    = "abcd1234-1a2b-3c4d-5e6f-1234567890ab";
const char* CMD_UUID     = "dcba4321-4b3c-2d1e-f0e9-0987654321ff";
const char* TEXT_UUID    = "effe1234-4b3c-2d1e-f0e9-0987654321aa";

// ─────────────────────────────────────────────────────────────
//  HELPERS
// ─────────────────────────────────────────────────────────────
int   clampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float lerpF(float a, float b, float t)    { return a + (b - a) * t; }
float easeCos(float p) { return 0.5f - 0.5f * cosf(p * PI); }
float easeOut3(float p) { float q = 1.0f - p; return 1.0f - q * q * q; }

// ─────────────────────────────────────────────────────────────
//  BLINK
// ─────────────────────────────────────────────────────────────
float computeBlinkProgress() {
  if (!blinking) return 0.0f;
  uint32_t t = millis() - blinkStartMs;
  if (t >= BLINK_TOTAL_MS) { blinking = false; return 0.0f; }
  if (t < BLINK_CLOSE_MS) return (float)t / (float)BLINK_CLOSE_MS;
  if (t < BLINK_CLOSE_MS + BLINK_HOLD_MS) return 1.0f;
  return 1.0f - (float)(t - BLINK_CLOSE_MS - BLINK_HOLD_MS) / (float)BLINK_OPEN_MS;
}

int applyBlinkToHeight(float eyeH, float bp) {
  return clampI((int)roundf(eyeH * (1.0f - bp) + 2.0f * bp), 2, 64);
}

// ─────────────────────────────────────────────────────────────
//  SMOOTH LERP STEP  (runs every frame)
// ─────────────────────────────────────────────────────────────
void smoothStep() {
#define SL(a, t) a += (t - a) * SMOOTH_ALPHA
  SL(actLeftOffsetX,  tgtLeftOffsetX);
  SL(actRightOffsetX, tgtRightOffsetX);
  SL(actOffsetY,      tgtOffsetY);
  SL(actLeftH,        tgtLeftH);
  SL(actRightH,       tgtRightH);
  SL(actMouthOffsetX, tgtMouthOffsetX);
  SL(actAngleL,       tgtAngleL);
  SL(actAngleR,       tgtAngleR);
  SL(actBottomCutL,   tgtBottomCutL);
  SL(actBottomCutR,   tgtBottomCutR);
  SL(actTopCutL,      tgtTopCutL);
  SL(actTopCutR,      tgtTopCutR);
#undef SL
  actMouthVariant = tgtMouthVariant;
}

// ─────────────────────────────────────────────────────────────
//  DRAW FACE — original mouth variants 0-3 preserved exactly
// ─────────────────────────────────────────────────────────────
void drawFaceDetailed(int leftH, int rightH,
                      int leftOffX, int rightOffX, int offY,
                      int mouthVariant, int mouthOffX,
                      int tearLY = -100, int tearRY = -100) {
  display.clearDisplay();

  int lx  = LEFT_EYE_BASE_X  + leftOffX;
  int rx  = RIGHT_EYE_BASE_X + rightOffX;
  int ley = EYE_BASE_Y + offY - leftH  / 2;
  int rey = EYE_BASE_Y + offY - rightH / 2;

  float scaleL = (actLeftH  > 0.1f) ? (float)leftH  / actLeftH  : 1.0f;
  float scaleR = (actRightH > 0.1f) ? (float)rightH / actRightH : 1.0f;

  int c_aL = (int)roundf(actAngleL     * scaleL);
  int c_aR = (int)roundf(actAngleR     * scaleR);
  int c_bL = (int)roundf(actBottomCutL * scaleL);
  int c_bR = (int)roundf(actBottomCutR * scaleR);
  int c_tL = (int)roundf(actTopCutL    * scaleL);
  int c_tR = (int)roundf(actTopCutR    * scaleR);

  // ── Eyes ─────────────────────────────────────────────────
  display.fillRoundRect(lx, ley, EYE_W, leftH,  6, OLED_WHITE);
  display.fillRoundRect(rx, rey, EYE_W, rightH, 6, OLED_WHITE);

  // Left eye polygon masking
  if (c_tL > 0)  display.fillRect(lx-2, ley-2, EYE_W+4, c_tL+2, OLED_BLACK);
  if (c_bL > 0)  display.fillRect(lx-2, ley+leftH-c_bL, EYE_W+4, c_bL+2, OLED_BLACK);
  if      (c_aL > 0) display.fillTriangle(lx-2, ley-2, lx+EYE_W+2, ley-2, lx-2,        ley+c_aL,  OLED_BLACK);
  else if (c_aL < 0) display.fillTriangle(lx+EYE_W+2, ley-2, lx-2, ley-2, lx+EYE_W+2, ley-c_aL,  OLED_BLACK);

  // Right eye polygon masking
  if (c_tR > 0)  display.fillRect(rx-2, rey-2, EYE_W+4, c_tR+2, OLED_BLACK);
  if (c_bR > 0)  display.fillRect(rx-2, rey+rightH-c_bR, EYE_W+4, c_bR+2, OLED_BLACK);
  if      (c_aR > 0) display.fillTriangle(rx+EYE_W+2, rey-2, rx-2, rey-2, rx+EYE_W+2, rey+c_aR,  OLED_BLACK);
  else if (c_aR < 0) display.fillTriangle(rx-2, rey-2, rx+EYE_W+2, rey-2, rx-2,        rey-c_aR,  OLED_BLACK);

  // ── MOUTH — original style & order preserved exactly ──────
  int mouthX = MOUTH_BASE_X + mouthOffX;

  if (mouthVariant == 0) {
    // Neutral flat rect
    display.fillRoundRect(mouthX, MOUTH_Y, MOUTH_W, MOUTH_H, 4, OLED_WHITE);

  } else if (mouthVariant == 1) {
    // Smile arc (original arc formula)
    int cx = mouthX + MOUTH_W / 2;
    int cy = MOUTH_Y + 4;
    for (int x = -10; x <= 10; x++) {
      int ys = (int)round(-sqrt(max(0, 100 - x * x)) / 4.0);
      display.drawPixel(cx + x, cy + ys,     OLED_WHITE);
      display.drawPixel(cx + x, cy + ys + 1, OLED_WHITE);
    }

  } else if (mouthVariant == 2) {
    // Bigger / open mouth
    display.fillRoundRect(mouthX, MOUTH_Y - 2, MOUTH_W, MOUTH_H + 6, 6, OLED_WHITE);

  } else if (mouthVariant == 3) {
    // Thin line (sad / uncertain)
    display.drawLine(mouthX, MOUTH_Y + 3, mouthX + MOUTH_W, MOUTH_Y + 3, OLED_WHITE);
  }

  // ── Tears ────────────────────────────────────────────────
  if (tearLY > -90) {
    int tx = lx + EYE_W / 2 - 4;
    int ty = ley + leftH + tearLY;
    display.fillCircle(tx, ty, 2, OLED_WHITE);
    display.fillTriangle(tx-2, ty, tx+2, ty, tx, ty-4, OLED_WHITE);
  }
  if (tearRY > -90) {
    int tx = rx + EYE_W / 2 + 4;
    int ty = rey + rightH + tearRY;
    display.fillCircle(tx, ty, 2, OLED_WHITE);
    display.fillTriangle(tx-2, ty, tx+2, ty, tx, ty-4, OLED_WHITE);
  }

  display.display();
}

// Convenience wrapper using current act* state
void renderFace(float blinkProg, int tearL = -100, int tearR = -100) {
  drawFaceDetailed(
    applyBlinkToHeight(actLeftH,  blinkProg),
    applyBlinkToHeight(actRightH, blinkProg),
    (int)roundf(actLeftOffsetX),
    (int)roundf(actRightOffsetX),
    (int)roundf(actOffsetY),
    actMouthVariant,
    (int)roundf(actMouthOffsetX),
    tearL, tearR);
}

// ─────────────────────────────────────────────────────────────
//  TARGETS — NEUTRAL
// ─────────────────────────────────────────────────────────────
void setTargetsNeutral() {
  tgtLeftOffsetX = 0; tgtRightOffsetX = 0; tgtOffsetY = 0;
  tgtLeftH = EYE_H_NEUTRAL; tgtRightH = EYE_H_NEUTRAL;
  tgtMouthOffsetX = 0; tgtMouthVariant = 0;
  tgtAngleL = 0; tgtAngleR = 0;
  tgtBottomCutL = 0; tgtBottomCutR = 0;
  tgtTopCutL = 0; tgtTopCutR = 0;
}

// ─────────────────────────────────────────────────────────────
//  TARGETS — PER ANIMATION (original 24 + 6 touch reactions)
//  animT : 0.0 → 1.0 over animation lifetime
//  tms   : milliseconds elapsed since animation started
// ─────────────────────────────────────────────────────────────
void setTargetsForAnim(AnimType a, float animT, uint32_t tms) {
  setTargetsNeutral();
  float bounce = sinf((float)tms * 0.007f) * 1.6f;  // gentle alive bounce

  switch (a) {

    // ── Original face expressions ──────────────────────────
    case ANIM_SADNESS:
      tgtAngleL=12; tgtAngleR=12; tgtMouthVariant=3; tgtOffsetY=2; break;

    case ANIM_ANGER:
      tgtAngleL=-14; tgtAngleR=-14; tgtOffsetY=-2; break;

    case ANIM_HAPPINESS:
      tgtBottomCutL=10; tgtBottomCutR=10;
      tgtLeftH=24; tgtRightH=24;
      tgtMouthVariant=1; tgtOffsetY=bounce; break;

    case ANIM_PLEADING:
      tgtAngleL=10; tgtAngleR=10;
      tgtBottomCutL=6; tgtBottomCutR=6;
      tgtLeftH=24; tgtRightH=24; tgtOffsetY=-2; break;

    case ANIM_VULNERABLE:
      tgtAngleL=8; tgtAngleR=8; tgtBottomCutL=4; tgtBottomCutR=4; break;

    case ANIM_DESPAIR:
      tgtAngleL=14; tgtAngleR=14;
      tgtLeftH=16; tgtRightH=16;
      tgtBottomCutL=6; tgtBottomCutR=6; tgtMouthVariant=3; break;

    case ANIM_SURPRISE:
      tgtLeftH=26; tgtRightH=26; tgtOffsetY=-4; tgtMouthVariant=2; break;

    case ANIM_DISGUST:
      tgtAngleL=-8; tgtAngleR=-4;
      tgtBottomCutL=6; tgtBottomCutR=6;
      tgtLeftH=16; tgtRightH=16;
      tgtLeftOffsetX=-2; tgtRightOffsetX=-2; tgtOffsetY=2; break;

    case ANIM_FEAR:
      tgtAngleL=6; tgtAngleR=6; tgtLeftH=16; tgtRightH=16; tgtOffsetY=-2; break;

    case ANIM_GUILTY:
      tgtTopCutL=10; tgtTopCutR=10;
      tgtBottomCutL=2; tgtBottomCutR=2;
      tgtLeftOffsetX=-4; tgtRightOffsetX=-4;
      tgtOffsetY=4; tgtMouthVariant=3; break;

    case ANIM_DISAPPOINTED:
      tgtTopCutL=8; tgtTopCutR=8;
      tgtBottomCutL=4; tgtBottomCutR=4; tgtMouthVariant=3; break;

    case ANIM_EMBARRASSED:
      tgtLeftH=8; tgtRightH=8;
      tgtAngleL=4; tgtAngleR=4;
      tgtMouthVariant=3; tgtMouthOffsetX=-2; break;

    case ANIM_HORRIFIED:
      tgtLeftH=26; tgtRightH=26;
      tgtBottomCutL=6; tgtBottomCutR=6;
      tgtAngleL=6; tgtAngleR=6; tgtMouthVariant=2; break;

    case ANIM_SKEPTICAL:
      tgtTopCutL=10; tgtBottomCutL=4;
      tgtAngleR=-10; tgtBottomCutR=4;
      tgtLeftOffsetX=-2; tgtRightOffsetX=2; break;

    case ANIM_ANNOYED:
      tgtTopCutL=6; tgtTopCutR=6; tgtAngleL=-8; tgtAngleR=-8; break;

    case ANIM_CONFUSED:
      tgtLeftH=24; tgtRightH=14;
      tgtTopCutR=4; tgtLeftOffsetX=-2; tgtRightOffsetX=2; break;

    case ANIM_AMAZED:
      tgtLeftH=28; tgtRightH=28;
      tgtBottomCutL=8; tgtBottomCutR=8; tgtMouthVariant=2; break;

    case ANIM_EXCITED:
      tgtLeftH=26; tgtRightH=26;
      tgtBottomCutL=12; tgtBottomCutR=12;
      tgtOffsetY=-2.0f+bounce; tgtMouthVariant=2; break;

    case ANIM_FURIOUS:
      tgtLeftH=16; tgtRightH=16;
      tgtAngleL=-16; tgtAngleR=-16; tgtOffsetY=2; break;

    case ANIM_SUSPICIOUS:
      tgtLeftH=6; tgtRightH=6;
      tgtTopCutL=1; tgtTopCutR=1;
      tgtBottomCutL=1; tgtBottomCutR=1; break;

    case ANIM_REJECTED:
      tgtLeftH=14; tgtRightH=14;
      tgtAngleL=14; tgtAngleR=14;
      tgtBottomCutL=2; tgtBottomCutR=2;
      tgtOffsetY=4; tgtMouthVariant=3; break;

    case ANIM_BORED:
      tgtLeftH=16; tgtRightH=16;
      tgtTopCutL=8; tgtTopCutR=8;
      tgtBottomCutL=2; tgtBottomCutR=2; break;

    case ANIM_TIRED:
      tgtLeftH=12; tgtRightH=12;
      tgtTopCutL=4; tgtTopCutR=4;
      tgtAngleL=4; tgtAngleR=4; break;

    case ANIM_ASLEEP:
      tgtLeftH=2; tgtRightH=2; tgtOffsetY=4; break;

    // ── Touch pet reactions ────────────────────────────────

    case ANIM_TOUCH_SINGLE:
      // Quick blink-smile + tiny head bob
      tgtMouthVariant = 1;
      tgtOffsetY = sinf((float)tms * 0.016f) * 2.5f;
      tgtBottomCutL = 4; tgtBottomCutR = 4; break;

    case ANIM_TOUCH_DOUBLE:
      // Happier: eyes widen, mouth excited, gentle bounce
      tgtLeftH = 26; tgtRightH = 26;
      tgtBottomCutL = 10; tgtBottomCutR = 10;
      tgtMouthVariant = 2;
      tgtOffsetY = sinf((float)tms * 0.012f) * 2.0f; break;

    case ANIM_TOUCH_LONG:
      // Shy / affectionate: eyes smaller, gaze inward, soft smile
      tgtLeftH = 15; tgtRightH = 15;
      tgtAngleL = 5; tgtAngleR = 5;
      tgtMouthVariant = 1;
      tgtLeftOffsetX = -3; tgtRightOffsetX = 3;
      tgtOffsetY = 1.5f; break;

    case ANIM_TOUCH_RUB:
      // Warm pet response: happy eyes, smile, soft bounce
      tgtLeftH = 24; tgtRightH = 24;
      tgtBottomCutL = 7; tgtBottomCutR = 7;
      tgtMouthVariant = 1;
      tgtOffsetY = sinf((float)tms * 0.009f) * 3.0f; break;

    case ANIM_TOUCH_FAST:
      // Excited / playful energy: wide eyes, open mouth, rapid jitter
      tgtLeftH = 28; tgtRightH = 28;
      tgtMouthVariant = 2;
      tgtOffsetY = -3.0f + sinf((float)tms * 0.018f) * 2.5f; break;

    case ANIM_TOUCH_IDLE_WAKE:
      // Soft wake-up from sleep: eyes slowly open, then smile
      {
        float w = clampF(animT * 2.5f, 0.0f, 1.0f);
        tgtLeftH       = lerpF(2.0f, (float)EYE_H_NEUTRAL, w);
        tgtRightH      = lerpF(2.0f, (float)EYE_H_NEUTRAL, w);
        tgtMouthVariant = (animT > 0.65f) ? 1 : 0;
        tgtOffsetY      = (animT > 0.65f) ? -1.5f : 2.0f;
      }
      break;

    default: break;
  }
}

// ─────────────────────────────────────────────────────────────
//  IDLE TARGETS — gentle breathing + eye drift
// ─────────────────────────────────────────────────────────────
void updateIdleTargets(float t, uint32_t now) {
  tgtOffsetY      = sinf(t * PI * 2.0f) * 1.0f;
  tgtMouthOffsetX = sinf(t * PI * 2.0f) * 0.5f;
  tgtLeftH        = EYE_H_NEUTRAL;
  tgtRightH       = EYE_H_NEUTRAL;

  // Slow horizontal eye drift (changes every 2-5 s)
  if (now >= nextDriftMs) {
    int dir = random(0, 3) - 1;   // -1, 0, or +1
    idleDriftX  = dir * 4.5f;
    nextDriftMs = now + random(2200, 5200);
  }
  tgtLeftOffsetX  = idleDriftX;
  tgtRightOffsetX = idleDriftX;
  tgtAngleL = 0; tgtAngleR = 0;
  tgtBottomCutL = 0; tgtBottomCutR = 0;
  tgtTopCutL    = 0; tgtTopCutR    = 0;
}

// ─────────────────────────────────────────────────────────────
//  ANIMATION QUEUE  (Fisher-Yates shuffle, no repeat)
// ─────────────────────────────────────────────────────────────
const int QUEUE_SIZE = 24;
int queueArr[QUEUE_SIZE];
int queueIdx = 0;

void refillQueue() {
  int idx = 0;
  for (int a = (int)ANIM_SADNESS; a <= (int)ANIM_ASLEEP; ++a)
    queueArr[idx++] = a;
  for (int i = idx - 1; i > 0; --i) {
    int j = random(0, i + 1);
    int t = queueArr[i]; queueArr[i] = queueArr[j]; queueArr[j] = t;
  }
  queueIdx = 0;
  if (idx > 1 && queueArr[0] == (int)lastAnim) {
    int t = queueArr[0]; queueArr[0] = queueArr[1]; queueArr[1] = t;
  }
}

AnimType popNextFromQueue() {
  if (queueIdx >= QUEUE_SIZE) refillQueue();
  AnimType c = (AnimType)queueArr[queueIdx++];
  if (c == lastAnim) {
    if (queueIdx >= QUEUE_SIZE) refillQueue();
    c = (AnimType)queueArr[queueIdx++];
  }
  lastAnim = c;
  return c;
}

// ─────────────────────────────────────────────────────────────
//  STATE TRANSITIONS
// ─────────────────────────────────────────────────────────────
void startGap() {
  inGap          = true;
  gapStartMs     = millis();
  gapDurationMs  = random(GAP_MIN_MS, GAP_MAX_MS);
  currentAnim    = ANIM_NONE;
  inTouchAnim    = false;
  setTargetsNeutral();
}

void startAnimation(AnimType a) {
  currentAnim    = a;
  animStartMs    = millis();
  animDurationMs = FIXED_ANIM_MS;
  inGap          = false;
  inTouchAnim    = false;
}

void startTouchAnim(AnimType a, uint32_t durMs) {
  inTouchAnim      = true;
  touchAnimType    = a;
  touchAnimStartMs = millis();
  touchAnimDurMs   = durMs;
  lastActivityMs   = millis();
}

void startWebManual(AnimType a) {
  webManualMode  = true;
  webManualAnim  = a;
  currentAnim    = a;
  animStartMs    = millis();
  inGap          = false;
  inTouchAnim    = false;
}

void stopWebManual() {
  webManualMode = false;
  webManualAnim = ANIM_NONE;
  startGap();
}

// ─────────────────────────────────────────────────────────────
//  CUSTOM TEXT (persistent until cleared via BLE)
// ─────────────────────────────────────────────────────────────
void startCustomTextDisplay(const String& msg) {
  customTextMessage  = msg;
  customTextStartMs  = millis();
  customTextActive   = true;
  inGap              = false;
  inTouchAnim        = false;
}

void clearCustomText() {
  customTextActive  = false;
  customTextMessage = "";
  startGap();
}

// Draws the custom text with glitch intro → stable display
void drawCustomTextFrame(uint32_t now) {
  uint32_t elapsed = now - customTextStartMs;
  display.clearDisplay();
  display.setTextColor(OLED_WHITE);
  display.setTextWrap(false);
  display.setTextSize(2);

  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(customTextMessage.c_str(), 0, 0, &x1, &y1, &w, &h);
  int bx = (SCREEN_WIDTH  - (int)w) / 2;
  int by = (SCREEN_HEIGHT - (int)h) / 2;

  if (elapsed < CT_GLITCH_MS) {
    // ── Glitch phase: strong at start, settles fast ───────
    float p       = (float)elapsed / (float)CT_GLITCH_MS;
    float k       = 1.0f - p;               // glitch strength
    int   jitter  = 1 + (int)(8.0f * k);

    // Main text (jittered position)
    display.setCursor(bx + random(-jitter, jitter + 1),
                      by + random(-jitter, jitter + 1));
    display.print(customTextMessage);

    // Two ghost copies (channel-split effect)
    display.setCursor(bx + random(-4, 5), by + random(-3, 4));
    display.print(customTextMessage);
    display.setCursor(bx + random(-4, 5), by + random(-3, 4));
    display.print(customTextMessage);

    // Random black glitch bars over text
    int bars = 2 + (int)(6.0f * k);
    for (int i = 0; i < bars; i++) {
      int gy  = by + random(0, (int)h + 1);
      int bh2 = random(1, 3);
      int bw  = (int)w - random(0, 20); if (bw < 4) bw = 4;
      display.fillRect(bx + random(-3, 4), gy, bw, bh2, OLED_BLACK);
    }
  } else {
    // ── Stable phase: clean centred text, stays until CLEAR
    display.setCursor(bx, by);
    display.print(customTextMessage);
  }
  display.display();
}

// ─────────────────────────────────────────────────────────────
//  LOGO GLITCH (startup)
// ─────────────────────────────────────────────────────────────
void drawLogoGlitchFrame(bool entering, float p) {
  display.clearDisplay();
  display.setTextColor(OLED_WHITE);
  display.setTextSize(2);
  display.setTextWrap(false);

  const char* txt = "NexRobo";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  int bx = (SCREEN_WIDTH  - (int)w) / 2;
  int by = (SCREEN_HEIGHT - (int)h) / 2;

  float k      = entering ? (1.0f - p) : p;
  k            = clampF(k, 0.0f, 1.0f);
  int jitter   = 1 + (int)(6.0f * k);

  display.setCursor(bx + random(-jitter, jitter + 1),
                    by + random(-jitter, jitter + 1));
  display.print(txt);
  display.setCursor(bx + random(-3, 4), by + random(-2, 3));
  display.print(txt);
  display.setCursor(bx + random(-3, 4), by + random(-2, 3));
  display.print(txt);

  int bars = 3 + jitter;
  for (int i = 0; i < bars; i++) {
    int gy  = by + random(0, (int)h + 1);
    int bh2 = random(1, 3);
    int bw  = (int)w - random(0, 16); if (bw < 2) bw = 2;
    display.fillRect(bx + random(-2, 4), gy, bw, bh2, OLED_BLACK);
  }
  display.display();
}

void showLogo() {
  const uint32_t GLITCH_MS = 420;
  const uint32_t HOLD_MS   = 2600;
  const uint32_t FRAME_MS  = 16;
  uint32_t start = millis();

  // Glitch in
  while (millis() - start < GLITCH_MS) {
    drawLogoGlitchFrame(true, (float)(millis() - start) / (float)GLITCH_MS);
    delay(FRAME_MS);
  }
  // Clean hold
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(OLED_WHITE);
  const char* txt = "NexRobo";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, (SCREEN_HEIGHT - (int)h) / 2);
  display.print(txt);
  display.display();
  delay(HOLD_MS);

  // Glitch out
  start = millis();
  while (millis() - start < GLITCH_MS) {
    drawLogoGlitchFrame(false, (float)(millis() - start) / (float)GLITCH_MS);
    delay(FRAME_MS);
  }
  display.clearDisplay();
  display.display();
}

// ─────────────────────────────────────────────────────────────
//  BLE CONNECT ANIMATION (face + banner for 3 s)
// ─────────────────────────────────────────────────────────────
void drawBLEConnectedAnim(uint32_t now) {
  uint32_t t = now - bleConnectAnimStart;
  if (t > BLE_CONNECT_MS) { bleConnectAnimActive = false; return; }

  float bp = computeBlinkProgress();
  drawFaceDetailed(
    applyBlinkToHeight(actLeftH,  bp), applyBlinkToHeight(actRightH, bp),
    (int)roundf(actLeftOffsetX), (int)roundf(actRightOffsetX),
    (int)roundf(actOffsetY), actMouthVariant, (int)roundf(actMouthOffsetX));

  // Overlay banner at top of screen
  display.setTextSize(1);
  display.setTextColor(OLED_WHITE);
  const char* msg = "BT Connected";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  display.fillRect(0, 0, SCREEN_WIDTH, (int)h + 2, OLED_BLACK);
  display.setCursor((SCREEN_WIDTH - (int)w) / 2, 1);
  display.print(msg);
  display.display();
}

// ─────────────────────────────────────────────────────────────
//  SPECIAL EFFECT OVERLAYS (rare; drawn on top of face)
//  Each adds pixels to the already-displayed buffer, then refreshes.
// ─────────────────────────────────────────────────────────────
void applySpecialFxOverlay(uint32_t now) {
  uint32_t t = now - specialFxStartMs;
  if (t >= SPECIAL_FX_MS) { specialFxActive = false; return; }
  float p = (float)t / (float)SPECIAL_FX_MS;

  switch (specialFxType) {

    case 1: {
      // Scan line — bright bar sweeps down the screen
      int scanY = (int)(p * SCREEN_HEIGHT);
      display.drawFastHLine(0, scanY,     SCREEN_WIDTH, OLED_WHITE);
      if (scanY > 0)
        display.drawFastHLine(0, scanY - 1, SCREEN_WIDTH, OLED_WHITE);
      display.display();
      break;
    }

    case 2: {
      // Glitch shift — random horizontal block offsets
      int bands = random(2, 5);
      for (int i = 0; i < bands; i++) {
        int y  = random(0, SCREEN_HEIGHT - 4);
        int bh = random(2, 5);
        int dx = random(-7, 8);
        if (dx != 0) {
          int fw = abs(dx);
          int fx = (dx > 0) ? 0 : SCREEN_WIDTH - fw;
          display.fillRect(fx, y, fw, bh, OLED_BLACK);
        }
      }
      display.display();
      break;
    }

    case 3: {
      // Pixel noise burst — density fades out
      int density = (int)((1.0f - p) * 55);
      for (int i = 0; i < density; i++)
        display.drawPixel(random(SCREEN_WIDTH), random(SCREEN_HEIGHT), OLED_WHITE);
      display.display();
      break;
    }

    default:
      specialFxActive = false;
      break;
  }
}

void triggerSpecialEffect() {
  specialFxType    = (uint8_t)random(1, 4);
  specialFxStartMs = millis();
  specialFxActive  = true;
  nextSpecialFxMs  = millis() + random(22000, 50000);
}

// ─────────────────────────────────────────────────────────────
//  TOUCH DETECTION
// ─────────────────────────────────────────────────────────────
void processTouchSensor(uint32_t now) {
  bool raw = (digitalRead(TOUCH_PIN) == HIGH);

  // Debounce
  if (raw != touchRaw) { touchRaw = raw; touchDebounceMs = now; }
  if (now - touchDebounceMs < DEBOUNCE_MS) return;

  bool prev  = touchState;
  touchState = touchRaw;

  bool wasSleepy = (now - lastActivityMs >= IDLE_SLEEP_MS);

  // ── Rising edge: finger touched ──────────────────────────
  if (touchState && !prev) {
    if (!fingerDown) {
      fingerDown    = true;
      fingerDownMs  = now;
      longPressFired = false;
      if (tapCount == 0) firstTapMs = now;
      tapCount++;
    }
  }

  // ── Falling edge: finger lifted ──────────────────────────
  if (!touchState && prev && fingerDown) {
    fingerDown     = false;
    lastFingerUpMs = now;
  }

  // ── Long-press check (fires once while held) ──────────────
  if (fingerDown && !longPressFired && !inTouchAnim &&
      (now - fingerDownMs >= LONG_PRESS_MS)) {
    longPressFired = true;
    tapCount       = 0;
    if (wasSleepy) startTouchAnim(ANIM_TOUCH_IDLE_WAKE, 2600);
    else           startTouchAnim(ANIM_TOUCH_LONG,      2800);
  }

  // ── Tap-pattern evaluation (after lift + window) ──────────
  if (!fingerDown && !longPressFired && tapCount > 0 &&
      (now - lastFingerUpMs >= TAP_WINDOW_MS)) {
    int tc  = tapCount;
    tapCount = 0;

    if (wasSleepy) {
      startTouchAnim(ANIM_TOUCH_IDLE_WAKE, 2600);
    } else if (tc >= RUB_TAPS) {
      startTouchAnim(ANIM_TOUCH_RUB,    3400);
    } else if (tc >= EXCITED_TAPS) {
      startTouchAnim(ANIM_TOUCH_FAST,   2200);
    } else if (tc == 2) {
      startTouchAnim(ANIM_TOUCH_DOUBLE, 2500);
    } else {
      startTouchAnim(ANIM_TOUCH_SINGLE, 1800);
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  BLE COMMAND → AnimType
// ─────────────────────────────────────────────────────────────
AnimType animFromCommand(const String& cmd) {
  if (cmd == "SAD"       || cmd == "SADNESS")   return ANIM_SADNESS;
  if (cmd == "ANGER")                           return ANIM_ANGER;
  if (cmd == "HAPPY"     || cmd == "HAPPINESS") return ANIM_HAPPINESS;
  if (cmd == "PLEADING")                        return ANIM_PLEADING;
  if (cmd == "VULNERABLE")                      return ANIM_VULNERABLE;
  if (cmd == "DESPAIR")                         return ANIM_DESPAIR;
  if (cmd == "SURPRISE")                        return ANIM_SURPRISE;
  if (cmd == "DISGUST")                         return ANIM_DISGUST;
  if (cmd == "FEAR")                            return ANIM_FEAR;
  if (cmd == "GUILTY")                          return ANIM_GUILTY;
  if (cmd == "DISAPPOINTED")                    return ANIM_DISAPPOINTED;
  if (cmd == "EMBARRASSED")                     return ANIM_EMBARRASSED;
  if (cmd == "HORRIFIED")                       return ANIM_HORRIFIED;
  if (cmd == "SKEPTICAL")                       return ANIM_SKEPTICAL;
  if (cmd == "ANNOYED")                         return ANIM_ANNOYED;
  if (cmd == "CONFUSED")                        return ANIM_CONFUSED;
  if (cmd == "AMAZED")                          return ANIM_AMAZED;
  if (cmd == "EXCITED")                         return ANIM_EXCITED;
  if (cmd == "FURIOUS")                         return ANIM_FURIOUS;
  if (cmd == "SUSPICIOUS")                      return ANIM_SUSPICIOUS;
  if (cmd == "REJECTED")                        return ANIM_REJECTED;
  if (cmd == "BORED")                           return ANIM_BORED;
  if (cmd == "TIRED")                           return ANIM_TIRED;
  if (cmd == "ASLEEP"    || cmd == "SLEEP")     return ANIM_ASLEEP;
  return ANIM_NONE;
}

// ─────────────────────────────────────────────────────────────
//  BLE CALLBACKS
// ─────────────────────────────────────────────────────────────
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* p) override {
    bleClientConnected   = true;
    bleConnectAnimActive = true;
    bleConnectAnimStart  = millis();
  }
  void onDisconnect(BLEServer* p) override {
    bleClientConnected  = false;
    bleClientAuthorized = false;
    if (webManualMode) stopWebManual();
    p->getAdvertising()->start();
  }
};

class PassCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = String(c->getValue().c_str());
    v.trim(); v.toLowerCase();
    bleClientAuthorized = (v == "nexrobo");
  }
};

class CmdCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    if (!bleClientAuthorized) return;
    String v = String(c->getValue().c_str());
    v.trim(); v.toUpperCase();

    if (v == "AUTO")                      { stopWebManual();                            return; }
    if (v == "MANUAL")                    { webManualMode = true;                       return; }
    if (v == "CLEAR" || v == "CLEARTEXT") { clearCustomText();                          return; }
    if (v == "LOGO")                      { startCustomTextDisplay(String("NexRobo")); return; }

    AnimType a = animFromCommand(v);
    if (a != ANIM_NONE) startWebManual(a);
  }
};

class TextCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    if (!bleClientAuthorized) return;
    String v = String(c->getValue().c_str());
    v.trim();

    // Empty string or "CLEAR" → remove text
    if (v.length() == 0 || v.equalsIgnoreCase("CLEAR")) {
      clearCustomText();
      return;
    }
    // Truncate to max length
    if ((int)v.length() > CT_MAX_LEN) v = v.substring(0, CT_MAX_LEN);
    startCustomTextDisplay(v);
  }
};

// ─────────────────────────────────────────────────────────────
//  BLE INIT
// ─────────────────────────────────────────────────────────────
void startBLE() {
  BLEDevice::init("NexRobo");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  pService = pServer->createService(SERVICE_UUID);

  pPassChar = pService->createCharacteristic(PASS_UUID,
                BLECharacteristic::PROPERTY_WRITE |
                BLECharacteristic::PROPERTY_READ);
  pPassChar->setCallbacks(new PassCharCallbacks());
  pPassChar->setValue("send password");

  pCmdChar = pService->createCharacteristic(CMD_UUID,
               BLECharacteristic::PROPERTY_WRITE |
               BLECharacteristic::PROPERTY_READ);
  pCmdChar->setCallbacks(new CmdCharCallbacks());
  pCmdChar->setValue("AUTO");

  pTextChar = pService->createCharacteristic(TEXT_UUID,
                BLECharacteristic::PROPERTY_WRITE |
                BLECharacteristic::PROPERTY_READ);
  pTextChar->setCallbacks(new TextCharCallbacks());
  pTextChar->setValue("send text");

  pService->start();

  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(pService->getUUID());
  pAdv->setScanResponse(true);
  pServer->getAdvertising()->start();
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(OLED_ADDR, true)) {
    // Hang visibly if display fails
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.display();

  pinMode(TOUCH_PIN, INPUT);
  randomSeed(micros());

  showLogo();       // blocking glitch intro → clean logo → glitch out
  startBLE();
  refillQueue();
  startGap();

  uint32_t now     = millis();
  lastBlinkTrigger = now;
  lastActivityMs   = now;
  nextSpecialFxMs  = now + random(20000, 40000);
  nextDriftMs      = now + random(2000, 5000);
}

// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // ── Touch input (always polled) ──────────────────────────
  processTouchSensor(now);

  // ── Blink trigger ────────────────────────────────────────
  if (!blinking && now - lastBlinkTrigger >= BLINK_PERIOD_MS) {
    blinking         = true;
    blinkStartMs     = now;
    lastBlinkTrigger = now;
  }
  float bp = computeBlinkProgress();

  // ──────────────────────────────────────────────────────────
  //  PRIORITY 1: Custom text mode
  //  Text stays on screen until BLE sends CLEAR.
  // ──────────────────────────────────────────────────────────
  if (customTextActive) {
    drawCustomTextFrame(now);
    delay(FRAME_DELAY_MS);
    return;
  }

  // ──────────────────────────────────────────────────────────
  //  PRIORITY 2: BLE connect banner (3 s overlay)
  // ──────────────────────────────────────────────────────────
  if (bleConnectAnimActive) {
    if (inGap)
      updateIdleTargets(
        min(1.0f, (float)(now - gapStartMs) / (float)gapDurationMs), now);
    else
      setTargetsForAnim(currentAnim,
        min(1.0f, (float)(now - animStartMs) / (float)animDurationMs),
        now - animStartMs);
    smoothStep();
    drawBLEConnectedAnim(now);
    delay(FRAME_DELAY_MS);
    return;
  }

  // ──────────────────────────────────────────────────────────
  //  PRIORITY 3: Touch reaction animation
  // ──────────────────────────────────────────────────────────
  if (inTouchAnim) {
    uint32_t te = now - touchAnimStartMs;
    if (te >= touchAnimDurMs) {
      inTouchAnim = false;
      startGap();
    } else {
      float animT = min(1.0f, (float)te / (float)touchAnimDurMs);
      setTargetsForAnim(touchAnimType, animT, te);
      smoothStep();
      renderFace(bp);
    }
    delay(FRAME_DELAY_MS);
    return;
  }

  // ──────────────────────────────────────────────────────────
  //  PRIORITY 4: Web / BLE manual command
  // ──────────────────────────────────────────────────────────
  if (webManualMode && webManualAnim != ANIM_NONE) {
    uint32_t ae = now - animStartMs;
    setTargetsForAnim(webManualAnim,
      min(1.0f, (float)ae / (float)FIXED_ANIM_MS), ae);
    smoothStep();

    int tearL = -100, tearR = -100;
    if (webManualAnim == ANIM_DESPAIR || webManualAnim == ANIM_SADNESS) {
      uint32_t pos = ae % 1200;
      float p = (pos < 800) ? (float)pos / 800.0f : 1.0f;
      tearL = tearR = (int)roundf(p * p * 14.0f);
    }
    renderFace(bp, tearL, tearR);
    delay(FRAME_DELAY_MS);
    return;
  }

  // ──────────────────────────────────────────────────────────
  //  PRIORITY 5: Auto animation loop
  // ──────────────────────────────────────────────────────────
  if (inGap) {
    uint32_t elapsed = now - gapStartMs;
    updateIdleTargets(min(1.0f, (float)elapsed / (float)gapDurationMs), now);
    smoothStep();
    renderFace(bp);

    // Rare special effect overlays during idle
    if (!specialFxActive && now >= nextSpecialFxMs)
      triggerSpecialEffect();
    if (specialFxActive)
      applySpecialFxOverlay(now);

    if (elapsed >= gapDurationMs)
      startAnimation(popNextFromQueue());

  } else {
    uint32_t ae = now - animStartMs;
    setTargetsForAnim(currentAnim,
      min(1.0f, (float)ae / (float)animDurationMs), ae);
    smoothStep();

    int tearL = -100, tearR = -100;
    if (currentAnim == ANIM_DESPAIR || currentAnim == ANIM_SADNESS) {
      uint32_t pos = ae % 1200;
      float p = (pos < 800) ? (float)pos / 800.0f : 1.0f;
      tearL = tearR = (int)roundf(p * p * 14.0f);
    }
    renderFace(bp, tearL, tearR);

    if (ae >= animDurationMs) startGap();
  }

  delay(FRAME_DELAY_MS);
}
