// NexRobo Eye + BLE + Web Bluetooth Controller (ESP32-C3)
// 24 Authentic Expressions, Dynamic Polygon Masking, 5s loop,
// Improved Tears, Smooth Blinks, BLE + Web control,
// Bluetooth connection banner shows for 3 seconds only.
// Custom text shows big on OLED with quick 0.2s pop-in animation.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Geometry constants
const int EYE_W = 24;
const int EYE_H_NEUTRAL = 20;
const int EYE_BASE_Y = 16;
const int LEFT_EYE_BASE_X = 20;
const int RIGHT_EYE_BASE_X = 84;
const int MOUTH_BASE_X = 54;
const int MOUTH_Y = 25;
const int MOUTH_W = 20;
const int MOUTH_H = 6;

#ifndef PI
#define PI 3.14159265358979323846
#endif

// Timing
const uint32_t FIXED_ANIM_MS = 5000;
const uint32_t GAP_MIN_MS  = 5000;
const uint32_t GAP_MAX_MS  = 10000;

// Blink timing
const uint32_t BLINK_PERIOD_MS = 3000;
const uint32_t BLINK_CLOSE_MS  = 120;
const uint32_t BLINK_HOLD_MS   = 80;
const uint32_t BLINK_OPEN_MS   = 120;
const uint32_t BLINK_TOTAL_MS  = BLINK_CLOSE_MS + BLINK_HOLD_MS + BLINK_OPEN_MS;

const float SMOOTH_ALPHA = 0.22f;
const uint32_t FRAME_DELAY_MS = 16;

// Animations
enum AnimType {
  ANIM_NONE = 0,
  ANIM_SADNESS, ANIM_ANGER, ANIM_HAPPINESS, ANIM_PLEADING,
  ANIM_VULNERABLE, ANIM_DESPAIR, ANIM_SURPRISE, ANIM_DISGUST,
  ANIM_FEAR, ANIM_GUILTY, ANIM_DISAPPOINTED, ANIM_EMBARRASSED,
  ANIM_HORRIFIED, ANIM_SKEPTICAL, ANIM_ANNOYED, ANIM_CONFUSED,
  ANIM_AMAZED, ANIM_EXCITED, ANIM_FURIOUS, ANIM_SUSPICIOUS,
  ANIM_REJECTED, ANIM_BORED, ANIM_TIRED, ANIM_ASLEEP
};

// Base State (smoothed)
float actLeftOffsetX = 0.0f, tgtLeftOffsetX = 0.0f;
float actRightOffsetX = 0.0f, tgtRightOffsetX = 0.0f;
float actOffsetY = 0.0f, tgtOffsetY = 0.0f;
float actLeftH = EYE_H_NEUTRAL, tgtLeftH = EYE_H_NEUTRAL;
float actRightH = EYE_H_NEUTRAL, tgtRightH = EYE_H_NEUTRAL;
float actMouthOffsetX = 0.0f, tgtMouthOffsetX = 0.0f;
int   actMouthVariant = 0, tgtMouthVariant = 0;

// Dynamic Shape Masking State (smoothed)
float actAngleL = 0.0f, tgtAngleL = 0.0f;
float actAngleR = 0.0f, tgtAngleR = 0.0f;
float actBottomCutL = 0.0f, tgtBottomCutL = 0.0f;
float actBottomCutR = 0.0f, tgtBottomCutR = 0.0f;
float actTopCutL = 0.0f, tgtTopCutL = 0.0f;
float actTopCutR = 0.0f, tgtTopCutR = 0.0f;

// Controllers
AnimType currentAnim = ANIM_NONE;
AnimType lastAnim = ANIM_NONE;
uint32_t animStartMs = 0, animDurationMs = 0;
uint32_t gapDurationMs = 0, gapStartMs = 0;
bool inGap = true;

// Blink control
uint32_t lastBlinkTrigger = 0, blinkStartMs = 0;
bool blinking = false;

// BLE
BLEServer* pServer = nullptr;
BLEService* pService = nullptr;
BLECharacteristic* pPassChar = nullptr;
BLECharacteristic* pCmdChar = nullptr;
BLECharacteristic* pTextChar = nullptr;

volatile bool bleClientConnected = false;
volatile bool bleClientAuthorized = false;

bool bleConnectAnimActive = false;
uint32_t bleConnectAnimStart = 0;
const uint32_t BLE_CONNECT_ANIM_MS = 3000;

// Web Bluetooth control
volatile bool webManualMode = false;
AnimType webManualAnim = ANIM_NONE;

// Custom text display
volatile bool customTextActive = false;
String customTextMessage = "";
uint32_t customTextStartMs = 0;
const uint32_t CUSTOM_TEXT_SHOW_MS = 3000;
const uint32_t CUSTOM_TEXT_ANIM_MS = 200;
const int CUSTOM_TEXT_MAX_LEN = 7;

// UUIDs
const char* SERVICE_UUID = "12345678-1234-1234-1234-123456789abc";
const char* PASS_UUID    = "abcd1234-1a2b-3c4d-5e6f-1234567890ab";
const char* CMD_UUID     = "dcba4321-4b3c-2d1e-f0e9-0987654321ff";
const char* TEXT_UUID    = "effe1234-4b3c-2d1e-f0e9-0987654321aa";

// Helpers
int clampInt(int v, int lo, int hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }
float easeCos(float p) { return 0.5f - 0.5f * cosf(p * PI); }
float easeOutCubic(float p) { return 1.0f - powf(1.0f - p, 3.0f); }

AnimType animFromCommand(const String& cmd) {
  if (cmd == "SAD" || cmd == "SADNESS") return ANIM_SADNESS;
  if (cmd == "ANGER") return ANIM_ANGER;
  if (cmd == "HAPPY" || cmd == "HAPPINESS") return ANIM_HAPPINESS;
  if (cmd == "PLEADING") return ANIM_PLEADING;
  if (cmd == "VULNERABLE") return ANIM_VULNERABLE;
  if (cmd == "DESPAIR") return ANIM_DESPAIR;
  if (cmd == "SURPRISE") return ANIM_SURPRISE;
  if (cmd == "DISGUST") return ANIM_DISGUST;
  if (cmd == "FEAR") return ANIM_FEAR;
  if (cmd == "GUILTY") return ANIM_GUILTY;
  if (cmd == "DISAPPOINTED") return ANIM_DISAPPOINTED;
  if (cmd == "EMBARRASSED") return ANIM_EMBARRASSED;
  if (cmd == "HORRIFIED") return ANIM_HORRIFIED;
  if (cmd == "SKEPTICAL") return ANIM_SKEPTICAL;
  if (cmd == "ANNOYED") return ANIM_ANNOYED;
  if (cmd == "CONFUSED") return ANIM_CONFUSED;
  if (cmd == "AMAZED") return ANIM_AMAZED;
  if (cmd == "EXCITED") return ANIM_EXCITED;
  if (cmd == "FURIOUS") return ANIM_FURIOUS;
  if (cmd == "SUSPICIOUS") return ANIM_SUSPICIOUS;
  if (cmd == "REJECTED") return ANIM_REJECTED;
  if (cmd == "BORED") return ANIM_BORED;
  if (cmd == "TIRED") return ANIM_TIRED;
  if (cmd == "ASLEEP" || cmd == "SLEEP") return ANIM_ASLEEP;
  return ANIM_NONE;
}

void startWebManual(AnimType a) {
  webManualMode = true;
  webManualAnim = a;
  currentAnim = a;
  animStartMs = millis();
  inGap = false;
}

void startCustomTextDisplay(const String& msg) {
  String clean = msg;
  clean.trim();
  if (clean.length() > CUSTOM_TEXT_MAX_LEN) clean = clean.substring(0, CUSTOM_TEXT_MAX_LEN);
  if (clean.length() == 0) return;
  customTextMessage = clean;
  customTextStartMs = millis();
  customTextActive = true;
  inGap = false;
}

void stopWebManual() {
  webManualMode = false;
  webManualAnim = ANIM_NONE;
  startGap();
}

// ---------- Drawing Core ----------
void drawFaceDetailed(int leftH, int rightH, int leftOffsetX, int rightOffsetX, int offsetY,
                      int mouthVariant, int mouthOffsetX, int tearLeftY = -100, int tearRightY = -100) {
  display.clearDisplay();

  int lx = LEFT_EYE_BASE_X + leftOffsetX;
  int rx = RIGHT_EYE_BASE_X + rightOffsetX;
  int ley = EYE_BASE_Y + offsetY - leftH / 2;
  int rey = EYE_BASE_Y + offsetY - rightH / 2;

  float scaleL = (actLeftH > 0.1f) ? (float)leftH / actLeftH : 1.0f;
  float scaleR = (actRightH > 0.1f) ? (float)rightH / actRightH : 1.0f;

  int c_angleL = (int)roundf(actAngleL * scaleL);
  int c_angleR = (int)roundf(actAngleR * scaleR);
  int c_botL = (int)roundf(actBottomCutL * scaleL);
  int c_botR = (int)roundf(actBottomCutR * scaleR);
  int c_topL = (int)roundf(actTopCutL * scaleL);
  int c_topR = (int)roundf(actTopCutR * scaleR);

  display.fillRoundRect(lx, ley, EYE_W, leftH, 6, SSD1306_WHITE);
  display.fillRoundRect(rx, rey, EYE_W, rightH, 6, SSD1306_WHITE);

  if (c_topL > 0) display.fillRect(lx - 2, ley - 2, EYE_W + 4, c_topL + 2, SSD1306_BLACK);
  if (c_botL > 0) display.fillRect(lx - 2, ley + leftH - c_botL, EYE_W + 4, c_botL + 2, SSD1306_BLACK);
  if (c_angleL > 0) display.fillTriangle(lx - 2, ley - 2, lx + EYE_W + 2, ley - 2, lx - 2, ley + c_angleL, SSD1306_BLACK);
  else if (c_angleL < 0) display.fillTriangle(lx + EYE_W + 2, ley - 2, lx - 2, ley - 2, lx + EYE_W + 2, ley - c_angleL, SSD1306_BLACK);

  if (c_topR > 0) display.fillRect(rx - 2, rey - 2, EYE_W + 4, c_topR + 2, SSD1306_BLACK);
  if (c_botR > 0) display.fillRect(rx - 2, rey + rightH - c_botR, EYE_W + 4, c_botR + 2, SSD1306_BLACK);
  if (c_angleR > 0) display.fillTriangle(rx + EYE_W + 2, rey - 2, rx - 2, rey - 2, rx + EYE_W + 2, rey + c_angleR, SSD1306_BLACK);
  else if (c_angleR < 0) display.fillTriangle(rx - 2, rey - 2, rx + EYE_W + 2, rey - 2, rx - 2, rey - c_angleR, SSD1306_BLACK);

  int mouthX = MOUTH_BASE_X + mouthOffsetX;
  if (mouthVariant == 0) {
    display.fillRoundRect(mouthX, MOUTH_Y, MOUTH_W, MOUTH_H, 4, SSD1306_WHITE);
  } else if (mouthVariant == 1) {
    int cx = mouthX + MOUTH_W / 2;
    int cy = MOUTH_Y + 4;
    for (int x = -10; x <= 10; x++) {
      int ys = round(-sqrt(max(0, 100 - x * x)) / 4.0);
      display.drawPixel(cx + x, cy + ys, SSD1306_WHITE);
      display.drawPixel(cx + x, cy + ys + 1, SSD1306_WHITE);
    }
  } else if (mouthVariant == 2) {
    display.fillRoundRect(mouthX, MOUTH_Y - 2, MOUTH_W, MOUTH_H + 6, 6, SSD1306_WHITE);
  } else if (mouthVariant == 3) {
    display.drawLine(mouthX, MOUTH_Y + 3, mouthX + MOUTH_W, MOUTH_Y + 3, SSD1306_WHITE);
  }

  if (tearLeftY > -90) {
    int tx = lx + EYE_W / 2 - 4;
    int ty = ley + leftH + tearLeftY;
    display.fillCircle(tx, ty, 2, SSD1306_WHITE);
    display.fillTriangle(tx - 2, ty, tx + 2, ty, tx, ty - 4, SSD1306_WHITE);
  }
  if (tearRightY > -90) {
    int tx = rx + EYE_W / 2 + 4;
    int ty = rey + rightH + tearRightY;
    display.fillCircle(tx, ty, 2, SSD1306_WHITE);
    display.fillTriangle(tx - 2, ty, tx + 2, ty, tx, ty - 4, SSD1306_WHITE);
  }

  display.display();
}

void drawCustomTextScreen(uint32_t now) {
  uint32_t elapsed = now - customTextStartMs;
  if (elapsed >= CUSTOM_TEXT_SHOW_MS) {
    customTextActive = false;
    return;
  }

  float p = min(1.0f, (float)elapsed / (float)CUSTOM_TEXT_ANIM_MS);
  float e = easeOutCubic(p);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  // Text grows quickly and stays big
  int textSize = (elapsed < 100) ? 1 : 2;
  display.setTextSize(textSize);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(customTextMessage.c_str(), 0, 0, &x1, &y1, &w, &h);

  int textX = (SCREEN_WIDTH - (int)w) / 2;
  int textY = (SCREEN_HEIGHT - (int)h) / 2 - 1;
  if (textY < 0) textY = 0;

  // Eyes move outward
  int eyeSpread = (int)roundf(12.0f * e);
  int eyeY = 7;
  int eyeH = 6;
  int leftEyeX = 26 - eyeSpread;
  int rightEyeX = 90 + eyeSpread;

  display.fillRoundRect(leftEyeX, eyeY, 12, eyeH, 3, SSD1306_WHITE);
  display.fillRoundRect(rightEyeX, eyeY, 12, eyeH, 3, SSD1306_WHITE);

  // Mouth goes down
  int mouthY = 24 + (int)roundf(3.0f * e);
  int mouthX = 58;
  int mouthW = 12;
  display.fillRoundRect(mouthX, mouthY, mouthW, 4, 2, SSD1306_WHITE);

  // Big centered text
  display.setCursor(textX, textY);
  display.print(customTextMessage);

  display.display();
}

// ---------- Blink Control ----------
float computeBlinkProgress() {
  if (!blinking) return 0.0f;
  uint32_t t = millis() - blinkStartMs;
  if (t >= BLINK_TOTAL_MS) { blinking = false; return 0.0f; }
  if (t < BLINK_CLOSE_MS) return (float)t / (float)BLINK_CLOSE_MS;
  else if (t < BLINK_CLOSE_MS + BLINK_HOLD_MS) return 1.0f;
  else return 1.0f - (float)(t - BLINK_CLOSE_MS - BLINK_HOLD_MS) / (float)BLINK_OPEN_MS;
}

int applyBlinkToHeight(float eyeH, float blinkProg) {
  float h = eyeH * (1.0f - blinkProg) + 2.0f * blinkProg;
  return clampInt((int)roundf(h), 2, 64);
}

// ---------- Targets / Animations ----------
void setTargetsNeutral() {
  tgtLeftOffsetX = 0; tgtRightOffsetX = 0; tgtOffsetY = 0;
  tgtLeftH = EYE_H_NEUTRAL; tgtRightH = EYE_H_NEUTRAL;
  tgtMouthOffsetX = 0; tgtMouthVariant = 0;
  tgtAngleL = 0; tgtAngleR = 0;
  tgtBottomCutL = 0; tgtBottomCutR = 0;
  tgtTopCutL = 0; tgtTopCutR = 0;
}

void setTargetsForAnim(AnimType a, float animT, uint32_t tms) {
  setTargetsNeutral();

  switch (a) {
    case ANIM_SADNESS:
      tgtAngleL = 12; tgtAngleR = 12; tgtMouthVariant = 3; tgtOffsetY = 2; break;
    case ANIM_ANGER:
      tgtAngleL = -14; tgtAngleR = -14; tgtOffsetY = -2; break;
    case ANIM_HAPPINESS:
      tgtBottomCutL = 10; tgtBottomCutR = 10; tgtLeftH = 22; tgtRightH = 22; tgtMouthVariant = 1; break;
    case ANIM_PLEADING:
      tgtAngleL = 10; tgtAngleR = 10; tgtBottomCutL = 6; tgtBottomCutR = 6; tgtLeftH = 22; tgtRightH = 22; tgtOffsetY = -2; break;
    case ANIM_VULNERABLE:
      tgtAngleL = 8; tgtAngleR = 8; tgtBottomCutL = 4; tgtBottomCutR = 4; break;
    case ANIM_DESPAIR:
      tgtAngleL = 14; tgtAngleR = 14; tgtLeftH = 16; tgtRightH = 16; tgtBottomCutL = 6; tgtBottomCutR = 6; tgtMouthVariant = 3; break;
    case ANIM_SURPRISE:
      tgtLeftH = 24; tgtRightH = 24; tgtOffsetY = -4; break;
    case ANIM_DISGUST:
      tgtAngleL = -8; tgtAngleR = -4; tgtBottomCutL = 6; tgtBottomCutR = 6; tgtLeftH = 16; tgtRightH = 16; tgtLeftOffsetX = -2; tgtRightOffsetX = -2; tgtOffsetY = 2; break;
    case ANIM_FEAR:
      tgtAngleL = 6; tgtAngleR = 6; tgtLeftH = 16; tgtRightH = 16; tgtOffsetY = -2; break;
    case ANIM_GUILTY:
      tgtTopCutL = 10; tgtTopCutR = 10; tgtBottomCutL = 2; tgtBottomCutR = 2; tgtLeftOffsetX = -4; tgtRightOffsetX = -4; tgtOffsetY = 4; tgtMouthVariant = 3; break;
    case ANIM_DISAPPOINTED:
      tgtTopCutL = 8; tgtTopCutR = 8; tgtBottomCutL = 4; tgtBottomCutR = 4; tgtMouthVariant = 3; break;
    case ANIM_EMBARRASSED:
      tgtLeftH = 8; tgtRightH = 8; tgtAngleL = 4; tgtAngleR = 4; tgtMouthVariant = 3; tgtMouthOffsetX = -2; break;
    case ANIM_HORRIFIED:
      tgtLeftH = 24; tgtRightH = 24; tgtBottomCutL = 6; tgtBottomCutR = 6; tgtAngleL = 6; tgtAngleR = 6; break;
    case ANIM_SKEPTICAL:
      tgtTopCutL = 10; tgtBottomCutL = 4; tgtAngleR = -10; tgtBottomCutR = 4; tgtLeftOffsetX = -2; tgtRightOffsetX = 2; break;
    case ANIM_ANNOYED:
      tgtTopCutL = 6; tgtTopCutR = 6; tgtAngleL = -8; tgtAngleR = -8; break;
    case ANIM_CONFUSED:
      tgtLeftH = 22; tgtRightH = 14; tgtTopCutR = 4; tgtLeftOffsetX = -2; tgtRightOffsetX = 2; break;
    case ANIM_AMAZED:
      tgtLeftH = 26; tgtRightH = 26; tgtBottomCutL = 8; tgtBottomCutR = 8; tgtMouthVariant = 2; break;
    case ANIM_EXCITED:
      tgtLeftH = 24; tgtRightH = 24; tgtBottomCutL = 12; tgtBottomCutR = 12; tgtOffsetY = -2; tgtMouthVariant = 2; break;
    case ANIM_FURIOUS:
      tgtLeftH = 16; tgtRightH = 16; tgtAngleL = -16; tgtAngleR = -16; tgtOffsetY = 2; break;
    case ANIM_SUSPICIOUS:
      tgtLeftH = 6; tgtRightH = 6; tgtTopCutL = 1; tgtTopCutR = 1; tgtBottomCutL = 1; tgtBottomCutR = 1; break;
    case ANIM_REJECTED:
      tgtLeftH = 14; tgtRightH = 14; tgtAngleL = 14; tgtAngleR = 14; tgtBottomCutL = 2; tgtBottomCutR = 2; tgtOffsetY = 4; tgtMouthVariant = 3; break;
    case ANIM_BORED:
      tgtLeftH = 16; tgtRightH = 16; tgtTopCutL = 8; tgtTopCutR = 8; tgtBottomCutL = 2; tgtBottomCutR = 2; break;
    case ANIM_TIRED:
      tgtLeftH = 12; tgtRightH = 12; tgtTopCutL = 4; tgtTopCutR = 4; tgtAngleL = 4; tgtAngleR = 4; break;
    case ANIM_ASLEEP:
      tgtLeftH = 2; tgtRightH = 2; tgtOffsetY = 4; break;
    default:
      break;
  }
}

void updateIdleTargets(float t) {
  tgtOffsetY = sinf(t * PI * 2.0f) * 1.2f;
  tgtMouthOffsetX = sinf(t * PI * 2.0f) * 0.6f;
  tgtLeftH = EYE_H_NEUTRAL;
  tgtRightH = EYE_H_NEUTRAL;
}

// ---------- Animation Queue ----------
const int QUEUE_SIZE = 24;
int queueArr[QUEUE_SIZE];
int queueIdx = 0;

void refillQueue() {
  int idx = 0;
  for (int a = ANIM_SADNESS; a <= ANIM_ASLEEP; ++a) queueArr[idx++] = a;
  for (int i = idx - 1; i > 0; --i) {
    int j = random(0, i + 1);
    int tmp = queueArr[i]; queueArr[i] = queueArr[j]; queueArr[j] = tmp;
  }
  queueIdx = 0;
  if (queueArr[0] == (int)lastAnim && idx > 1) {
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

// ---------- BLE callbacks ----------
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleClientConnected = true;
    bleConnectAnimActive = true;
    bleConnectAnimStart = millis();
  }

  void onDisconnect(BLEServer* pServer) {
    bleClientConnected = false;
    bleClientAuthorized = false;

    if (webManualMode || webManualAnim != ANIM_NONE) {
      stopWebManual();
    }

    pServer->getAdvertising()->start();
  }
};

class PassCharCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String val = String(pChar->getValue().c_str());
    val.trim();
    if (val.length() == 0) return;
    val.toLowerCase();
    if (val == "nexrobo") bleClientAuthorized = true;
    else bleClientAuthorized = false;
  }
};

class CmdCharCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    if (!bleClientAuthorized) return;

    String val = String(pChar->getValue().c_str());
    val.trim();
    val.toUpperCase();

    if (val == "AUTO") {
      stopWebManual();
      return;
    }

    if (val == "MANUAL") {
      webManualMode = true;
      return;
    }

    AnimType a = animFromCommand(val);
    if (a != ANIM_NONE) {
      startWebManual(a);
    }
  }
};

class TextCharCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    if (!bleClientAuthorized) return;

    String val = String(pChar->getValue().c_str());
    val.trim();
    if (val.length() == 0) return;
    if (val.length() > CUSTOM_TEXT_MAX_LEN) val = val.substring(0, CUSTOM_TEXT_MAX_LEN);
    startCustomTextDisplay(val);
  }
};

// ---------- Smooth Core ----------
void smoothStep() {
  actLeftOffsetX  += (tgtLeftOffsetX  - actLeftOffsetX)  * SMOOTH_ALPHA;
  actRightOffsetX += (tgtRightOffsetX - actRightOffsetX) * SMOOTH_ALPHA;
  actOffsetY      += (tgtOffsetY      - actOffsetY)      * SMOOTH_ALPHA;
  actLeftH        += (tgtLeftH        - actLeftH)        * SMOOTH_ALPHA;
  actRightH       += (tgtRightH       - actRightH)       * SMOOTH_ALPHA;
  actMouthOffsetX += (tgtMouthOffsetX - actMouthOffsetX) * SMOOTH_ALPHA;
  actMouthVariant = tgtMouthVariant;

  actAngleL += (tgtAngleL - actAngleL) * SMOOTH_ALPHA;
  actAngleR += (tgtAngleR - actAngleR) * SMOOTH_ALPHA;
  actBottomCutL += (tgtBottomCutL - actBottomCutL) * SMOOTH_ALPHA;
  actBottomCutR += (tgtBottomCutR - actBottomCutR) * SMOOTH_ALPHA;
  actTopCutL += (tgtTopCutL - actTopCutL) * SMOOTH_ALPHA;
  actTopCutR += (tgtTopCutR - actTopCutR) * SMOOTH_ALPHA;
}

void drawBLEConnectedAnimation(uint32_t animNow) {
  uint32_t t = animNow - bleConnectAnimStart;
  if (t > BLE_CONNECT_ANIM_MS) {
    bleConnectAnimActive = false;
    return;
  }

  int dispLeftH = applyBlinkToHeight(actLeftH, computeBlinkProgress());
  int dispRightH = applyBlinkToHeight(actRightH, computeBlinkProgress());

  drawFaceDetailed(
    dispLeftH, dispRightH,
    (int)roundf(actLeftOffsetX),
    (int)roundf(actRightOffsetX),
    (int)roundf(actOffsetY),
    actMouthVariant,
    (int)roundf(actMouthOffsetX)
  );

  const char* msg = "Bluetooth Connected";
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  int textX = (SCREEN_WIDTH - (int)w) / 2;
  display.setCursor(textX, 0);
  display.print(msg);
  display.display();
}

void startAnimation(AnimType a) {
  currentAnim = a;
  animStartMs = millis();
  animDurationMs = FIXED_ANIM_MS;
  inGap = false;
}

void startGap() {
  inGap = true;
  gapStartMs = millis();
  gapDurationMs = random(GAP_MIN_MS, GAP_MAX_MS);
  currentAnim = ANIM_NONE;
  setTargetsNeutral();
}

void drawLogoGlitchFrame(bool entering, float p) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setTextWrap(false);

  const char* txt = "NexRobo";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);

  int baseX = (SCREEN_WIDTH - (int)w) / 2;
  int baseY = (SCREEN_HEIGHT - (int)h) / 2;

  float k = entering ? (1.0f - p) : p;
  if (k < 0.0f) k = 0.0f;
  if (k > 1.0f) k = 1.0f;

  int jitter = 1 + (int)roundf(6.0f * k);
  int mainX = baseX + random(-jitter, jitter + 1);
  int mainY = baseY + random(-jitter, jitter + 1);

  display.setCursor(mainX, mainY);
  display.print(txt);

  display.setCursor(mainX + random(-3, 4), mainY + random(-2, 3));
  display.print(txt);

  display.setCursor(mainX + random(-3, 4), mainY + random(-2, 3));
  display.print(txt);

  int bars = 3 + jitter;
  for (int i = 0; i < bars; i++) {
    int y = mainY + random(0, (int)h + 1);
    int bh = random(1, 3);
    int x = baseX + random(-2, 4);
    int ww = (int)w - random(0, 16);
    if (ww > 0) display.fillRect(x, y, ww, bh, SSD1306_BLACK);
  }

  display.display();
}

void showLogo() {
  const uint32_t GLITCH_MS = 400;
  const uint32_t HOLD_MS = 3000;
  const uint32_t FRAME_MS = 16;

  uint32_t start = millis();
  while (millis() - start < GLITCH_MS) {
    float p = (float)(millis() - start) / (float)GLITCH_MS;
    drawLogoGlitchFrame(true, p);
    delay(FRAME_MS);
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  const char* txt = "NexRobo";
  int textW = 6 * 7 * 2;
  display.setCursor((SCREEN_WIDTH - textW) / 2, (SCREEN_HEIGHT - 16) / 2);
  display.print(txt);
  display.display();
  delay(HOLD_MS);

  start = millis();
  while (millis() - start < GLITCH_MS) {
    float p = (float)(millis() - start) / (float)GLITCH_MS;
    drawLogoGlitchFrame(false, p);
    delay(FRAME_MS);
  }

  display.clearDisplay();
  display.display();
}

void drawCustomTextLoop(uint32_t now) {
  if (!customTextActive) return;
  uint32_t elapsed = now - customTextStartMs;
  if (elapsed >= CUSTOM_TEXT_SHOW_MS) {
    customTextActive = false;
    return;
  }
  drawCustomTextScreen(now);
}

void startBLE() {
  BLEDevice::init("NexRobo");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  pService = pServer->createService(SERVICE_UUID);

  pPassChar = pService->createCharacteristic(
    PASS_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ
  );
  pPassChar->setCallbacks(new PassCharCallbacks());
  pPassChar->setValue("send password");

  pCmdChar = pService->createCharacteristic(
    CMD_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ
  );
  pCmdChar->setCallbacks(new CmdCharCallbacks());
  pCmdChar->setValue("AUTO");

  pTextChar = pService->createCharacteristic(
    TEXT_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ
  );
  pTextChar->setCallbacks(new TextCharCallbacks());
  pTextChar->setValue("send text");

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->setScanResponse(true);
  pServer->getAdvertising()->start();
}

void setup() {
  Wire.begin(3, 4);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) delay(1000);
  }

  randomSeed(micros());

  showLogo();
  startBLE();
  refillQueue();
  startGap();

  lastBlinkTrigger = millis();
}

void loop() {
  uint32_t now = millis();

  if (!blinking && now - lastBlinkTrigger >= BLINK_PERIOD_MS) {
    blinking = true;
    blinkStartMs = now;
    lastBlinkTrigger = now;
  }

  if (customTextActive) {
    drawCustomTextLoop(now);
    delay(FRAME_DELAY_MS);
    return;
  }

  float blinkProg = computeBlinkProgress();

  if (bleConnectAnimActive) {
    if (inGap) updateIdleTargets(min(1.0f, (float)(now - gapStartMs) / gapDurationMs));
    else setTargetsForAnim(currentAnim, min(1.0f, (float)(now - animStartMs) / animDurationMs), now - animStartMs);

    smoothStep();
    drawBLEConnectedAnimation(now);
    delay(FRAME_DELAY_MS);
    return;
  }

  if (webManualMode && webManualAnim != ANIM_NONE) {
    currentAnim = webManualAnim;

    uint32_t animElapsed = now - animStartMs;
    setTargetsForAnim(currentAnim, min(1.0f, (float)animElapsed / (float)FIXED_ANIM_MS), animElapsed);
    smoothStep();

    int tearLeftY = -100, tearRightY = -100;
    if (currentAnim == ANIM_DESPAIR || currentAnim == ANIM_SADNESS) {
      uint32_t pos = animElapsed % 1200;
      if (pos < 800) {
        float p = (float)pos / 800.0f;
        tearLeftY = tearRightY = (int)roundf((p * p) * 14.0f);
      } else {
        tearLeftY = 14;
        tearRightY = 14;
      }
    }

    drawFaceDetailed(
      applyBlinkToHeight(actLeftH, blinkProg),
      applyBlinkToHeight(actRightH, blinkProg),
      (int)roundf(actLeftOffsetX),
      (int)roundf(actRightOffsetX),
      (int)roundf(actOffsetY),
      actMouthVariant,
      (int)roundf(actMouthOffsetX),
      tearLeftY,
      tearRightY
    );

    delay(FRAME_DELAY_MS);
    return;
  }

  if (inGap) {
    uint32_t elapsed = now - gapStartMs;
    updateIdleTargets(min(1.0f, (float)elapsed / gapDurationMs));
    smoothStep();

    drawFaceDetailed(
      applyBlinkToHeight(actLeftH, blinkProg),
      applyBlinkToHeight(actRightH, blinkProg),
      (int)roundf(actLeftOffsetX),
      (int)roundf(actRightOffsetX),
      (int)roundf(actOffsetY),
      actMouthVariant,
      (int)roundf(actMouthOffsetX)
    );

    if (elapsed >= gapDurationMs) {
      inGap = false;
      startAnimation(popNextFromQueue());
    }
  } else {
    uint32_t animElapsed = now - animStartMs;
    setTargetsForAnim(currentAnim, min(1.0f, (float)animElapsed / animDurationMs), animElapsed);
    smoothStep();

    int tearLeftY = -100, tearRightY = -100;
    if (currentAnim == ANIM_DESPAIR || currentAnim == ANIM_SADNESS) {
      uint32_t pos = animElapsed % 1200;
      if (pos < 800) {
        float p = (float)pos / 800.0f;
        tearLeftY = tearRightY = (int)roundf((p * p) * 14.0f);
      } else {
        tearLeftY = 14;
        tearRightY = 14;
      }
    }

    drawFaceDetailed(
      applyBlinkToHeight(actLeftH, blinkProg),
      applyBlinkToHeight(actRightH, blinkProg),
      (int)roundf(actLeftOffsetX),
      (int)roundf(actRightOffsetX),
      (int)roundf(actOffsetY),
      actMouthVariant,
      (int)roundf(actMouthOffsetX),
      tearLeftY,
      tearRightY
    );

    if (animElapsed >= animDurationMs) startGap();
  }

  delay(FRAME_DELAY_MS);
}
