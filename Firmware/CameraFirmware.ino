/*
 * ESP32-S3 Camera + TFT ST7735 + SD Card Firmware
 * OV5640 Camera, 2.4" ST7735 TFT Display
 *
 * ─── 引脚分配 ────────────────────────────────────────────────────────────────
 *  Camera (DVP 并行):
 *    PWDN=9  RESET=5  XCLK=内部  SIOD=17  SIOC=18
 *    Y2-Y9 = 16,47,30,48,14,12,11,10
 *    VSYNC=6  HREF=8  PCLK=13
 *
 *  TFT ST7735 (SPI):
 *    MOSI=2   SCLK=1   CS=21   DC=7   RST=3
 *    ※ MOSI 已从 GPIO17 改为 GPIO2，与摄像头 SCCB 完全隔离，无冲突
 *
 *  SD Card (SD_MMC 1-bit):
 *    由 SD_MMC 库管理，1-bit 模式
 *
 *  Buttons:
 *    MenuBTN=41  BTN1=40  BTN2=39
 *
 * ─── 状态机 ──────────────────────────────────────────────────────────────────
 *   STATE_CAMERA  → 实时预览，BTN1=拍照保存
 *   STATE_REVIEW  → SD卡照片浏览，BTN1=上一张，BTN2=下一张
 *   MenuBTN 切换: STATE_CAMERA ↔ STATE_REVIEW
 * ────────────────────────────────────────────────────────────────────────────
 */

#include "esp_camera.h"
#include "Arduino.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"
#include <EEPROM.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "FS.h"
#include "SD_MMC.h"
#include "esp_jpg_decode.h"

// ─── Camera Pin Definitions ───────────────────────────────────────────────────
#define PWDN_GPIO_NUM     9
#define RESET_GPIO_NUM    5
#define XCLK_GPIO_NUM    -1   // Use internal clock
#define SIOD_GPIO_NUM    17
#define SIOC_GPIO_NUM    18
#define Y9_GPIO_NUM      10
#define Y8_GPIO_NUM      11
#define Y7_GPIO_NUM      12
#define Y6_GPIO_NUM      14
#define Y5_GPIO_NUM      48
#define Y4_GPIO_NUM      30
#define Y3_GPIO_NUM      47
#define Y2_GPIO_NUM      16
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     8
#define PCLK_GPIO_NUM    13

// ─── TFT Pin Definitions ──────────────────────────────────────────────────────
// 在 TFT_eSPI 的 User_Setup.h 中配置以下引脚：
//   #define TFT_MOSI  2    // ← 已从 17 改为 2（与摄像头 SCCB 完全隔离）
//   #define TFT_SCLK  1
//   #define TFT_CS   21
//   #define TFT_DC    7    // ← 已从 2 改为 7
//   #define TFT_RST   3    // ← 已从 7 改为 3
// （或在 platformio.ini / build_flags 中用 -D 宏覆盖）
#define TFT_MOSI_PIN  2
#define TFT_SCLK_PIN  1
#define TFT_CS_PIN   21
#define TFT_DC_PIN    7
#define TFT_RST_PIN   3

// ─── Button Definitions ───────────────────────────────────────────────────────
#define menuBTN  41
#define BTN1     40
#define BTN2     39

// ─── Display resolution ───────────────────────────────────────────────────────
#define TFT_W    240
#define TFT_H    320

// ─── EEPROM ───────────────────────────────────────────────────────────────────
#define EEPROM_SIZE 4  // Store int (picture count)

// ─── State Machine ────────────────────────────────────────────────────────────
typedef enum {
  STATE_CAMERA = 0,   // 实时摄像头预览
  STATE_REVIEW  = 1   // SD卡照片浏览
} AppState;

// ─── Globals ─────────────────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();

AppState currentState = STATE_CAMERA;

int totalPictures  = 0;   // 已保存照片总数（从 EEPROM 读取）
int viewIndex      = 0;   // 当前浏览的照片序号（0-based）

// 按键防抖
unsigned long lastMenuTime = 0;
unsigned long lastBtn1Time = 0;
unsigned long lastBtn2Time = 0;
#define DEBOUNCE_MS 250

// JPEG 解码缓冲区（RGB565 格式）
uint16_t *jpegBuf = nullptr;

// ─── Forward Declarations ────────────────────────────────────────────────────
bool initCamera();
bool initDisplay();
bool initSDCard();
void showCameraFrame();
void captureAndSave();
void showImageFromSD(int index);
void drawStatusBar(const char* msg);
bool readButtons(bool &menuPressed, bool &btn1Pressed, bool &btn2Pressed);

// ─── JPEG decode callback (rgb565 line-by-line to TFT) ───────────────────────
// We use a simple approach: decode JPEG → push to TFT via esp_jpg_decode
// The decoded pixel callback writes directly to the TFT

struct JpegDecode {
  int x_offset;
  int y_offset;
  uint16_t *linebuf;
  int linebuf_width;
};

static bool jpg_write_cb(void *arg, uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h, uint8_t *data) {
  if (!data) return true;
  JpegDecode *ctx = (JpegDecode *)arg;
  // data is RGB888, convert to RGB565 and push
  tft.startWrite();
  tft.setAddrWindow(ctx->x_offset + x, ctx->y_offset + y, w, h);
  for (uint32_t i = 0; i < (uint32_t)(w * h); i++) {
    uint8_t r = data[i * 3 + 0];
    uint8_t g = data[i * 3 + 1];
    uint8_t b = data[i * 3 + 2];
    uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    tft.pushColor(rgb565);
  }
  tft.endWrite();
  return true;
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // 禁用欠压检测

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("[BOOT] ESP32-S3 Camera Starting...");

  // 按键初始化
  pinMode(menuBTN, INPUT_PULLUP);
  pinMode(BTN1,    INPUT_PULLUP);
  pinMode(BTN2,    INPUT_PULLUP);

  // EEPROM 初始化，读取已保存照片数量
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, totalPictures);
  if (totalPictures < 0 || totalPictures > 9999) totalPictures = 0;
  Serial.printf("[EEPROM] Total pictures stored: %d\n", totalPictures);

  // ── Step 1: 显示器初始化（GPIO2=MOSI 与摄像头无冲突，先启动）────────────
  if (!initDisplay()) {
    Serial.println("[ERROR] Display init failed!");
    // 不 halt，继续运行
  }

  // ── Step 2: 摄像头初始化（GPIO17=SCCB SDA，与 TFT 引脚完全隔离）─────────
  if (!initCamera()) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(10, TFT_H / 2 - 10);
    tft.print("Camera FAILED");
    Serial.println("[ERROR] Camera init failed! Halting.");
    while (1) delay(1000);
  }
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, TFT_H / 2 - 10);
  tft.print("Camera OK");
  delay(400);

  // SD 卡初始化
  if (!initSDCard()) {
    drawStatusBar("SD Card FAILED");
    Serial.println("[WARN] SD Card init failed — saving disabled");
  }

  Serial.println("[BOOT] Ready.");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  bool menuPressed = false, btn1Pressed = false, btn2Pressed = false;
  readButtons(menuPressed, btn1Pressed, btn2Pressed);

  switch (currentState) {

    // ── 实时预览模式 ──────────────────────────────────────────────────────────
    case STATE_CAMERA:
      showCameraFrame();

      if (menuPressed) {
        // 切换到浏览模式，显示最新照片
        Serial.println("[STATE] → REVIEW");
        currentState = STATE_REVIEW;
        viewIndex = (totalPictures > 0) ? (totalPictures - 1) : 0;
        if (totalPictures > 0) {
          showImageFromSD(viewIndex);
        } else {
          tft.fillScreen(TFT_BLACK);
          tft.setTextColor(TFT_WHITE);
          tft.setTextSize(2);
          tft.setCursor(20, TFT_H / 2);
          tft.print("No photos yet");
          drawStatusBar("MenuBTN: back");
        }
      }

      if (btn1Pressed) {
        // BTN1 → 拍照保存
        captureAndSave();
      }
      break;

    // ── 照片浏览模式 ──────────────────────────────────────────────────────────
    case STATE_REVIEW:
      if (menuPressed) {
        // 再按 MenuBTN → 回到摄像头预览
        Serial.println("[STATE] → CAMERA");
        currentState = STATE_CAMERA;
        tft.fillScreen(TFT_BLACK);
      }

      if (btn1Pressed && totalPictures > 0) {
        // BTN1 → 上一张
        viewIndex--;
        if (viewIndex < 0) viewIndex = totalPictures - 1;
        Serial.printf("[REVIEW] Showing image %d\n", viewIndex);
        showImageFromSD(viewIndex);
      }

      if (btn2Pressed && totalPictures > 0) {
        // BTN2 → 下一张
        viewIndex++;
        if (viewIndex >= totalPictures) viewIndex = 0;
        Serial.printf("[REVIEW] Showing image %d\n", viewIndex);
        showImageFromSD(viewIndex);
      }
      break;
  }
}

// ─── Camera Init ─────────────────────────────────────────────────────────────
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_QVGA;  // 320x240，适合TFT显示
    config.jpeg_quality = 12;
    config.fb_count     = 2;
    Serial.println("[CAM] PSRAM found — QVGA, 2 buffers");
  } else {
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count     = 1;
    Serial.println("[CAM] No PSRAM — QVGA, 1 buffer");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init failed: 0x%x\n", err);
    return false;
  }

  // OV5640 sensor settings
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);
    s->set_saturation(s, 0);
    s->set_gainceiling(s, (gainceiling_t)6);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 0);
    s->set_gain_ctrl(s, 1);
    s->set_agc_gain(s, 0);
    s->set_bpc(s, 0);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    Serial.println("[CAM] OV5640 sensor configured");
  }

  return true;
}

// ─── Display Init ────────────────────────────────────────────────────────────
bool initDisplay() {
  tft.init();
  tft.setRotation(0);          // Portrait
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, TFT_H / 2 - 10);
  tft.print("Initializing...");
  Serial.println("[TFT] Display initialized");
  return true;
}

// ─── SD Card Init ────────────────────────────────────────────────────────────
bool initSDCard() {
  // SD_MMC uses built-in MMC interface (1-bit mode for fewer pins)
  if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
    Serial.println("[SD] Mount failed");
    return false;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[SD] No card detected");
    return false;
  }
  Serial.printf("[SD] Card type: %d, Size: %llu MB\n",
                cardType, SD_MMC.cardSize() / (1024 * 1024));
  return true;
}

// ─── Show Camera Frame ───────────────────────────────────────────────────────
void showCameraFrame() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM] Capture failed");
    delay(100);
    return;
  }

  if (fb->format == PIXFORMAT_JPEG) {
    // JPEG → RGB565 → TFT
    JpegDecode ctx;
    ctx.x_offset = 0;
    ctx.y_offset = 0;
    // esp_jpg_decode 解码 JPEG 并回调写入 TFT
    esp_jpg_decode(fb->buf, fb->len, JPG_SCALE_NONE,
                   jpg_write_cb, (void *)&ctx);
  } else if (fb->format == PIXFORMAT_RGB565) {
    tft.startWrite();
    tft.setAddrWindow(0, 0, TFT_W, TFT_H);
    tft.pushPixels((uint16_t *)fb->buf, TFT_W * TFT_H);
    tft.endWrite();
  }

  esp_camera_fb_return(fb);
}

// ─── Capture & Save to SD ────────────────────────────────────────────────────
void captureAndSave() {
  // 拍摄一帧（提高画质）
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, FRAMESIZE_SVGA);  // 拍照用更高分辨率
    s->set_jpeg_quality(s, 8);
  }
  delay(200);  // 让曝光稳定

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM] Capture failed");
    drawStatusBar("Capture FAILED");
    delay(1000);
    return;
  }

  // 文件名: /pic_0001.jpg
  char path[32];
  snprintf(path, sizeof(path), "/pic_%04d.jpg", totalPictures);

  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[SD] Failed to open %s for writing\n", path);
    drawStatusBar("SD Write FAILED");
  } else {
    f.write(fb->buf, fb->len);
    f.close();
    totalPictures++;
    EEPROM.put(0, totalPictures);
    EEPROM.commit();
    Serial.printf("[SD] Saved: %s (%zu bytes)\n", path, fb->len);

    // 显示保存成功提示
    char msg[40];
    snprintf(msg, sizeof(msg), "Saved: pic_%04d.jpg", totalPictures - 1);
    drawStatusBar(msg);
  }

  esp_camera_fb_return(fb);

  // 恢复预览分辨率
  if (s) {
    s->set_framesize(s, FRAMESIZE_QVGA);
    s->set_jpeg_quality(s, 12);
  }

  delay(1000);  // 显示保存提示1秒
}

// ─── Show Image from SD ──────────────────────────────────────────────────────
void showImageFromSD(int index) {
  if (totalPictures == 0) return;

  char path[32];
  snprintf(path, sizeof(path), "/pic_%04d.jpg", index);

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[SD] Failed to open %s\n", path);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(10, TFT_H / 2);
    tft.printf("Cannot open\npic_%04d.jpg", index);
    return;
  }

  size_t fileSize = f.size();
  uint8_t *jpgData = (uint8_t *)malloc(fileSize);
  if (!jpgData) {
    Serial.println("[MEM] malloc failed for JPEG buffer");
    f.close();
    return;
  }

  f.read(jpgData, fileSize);
  f.close();

  tft.fillScreen(TFT_BLACK);

  JpegDecode ctx;
  ctx.x_offset = 0;
  ctx.y_offset = 0;
  esp_jpg_decode(jpgData, fileSize, JPG_SCALE_NONE, jpg_write_cb, (void *)&ctx);

  free(jpgData);

  // 显示状态栏
  char msg[40];
  snprintf(msg, sizeof(msg), "%d / %d  B1:Prev B2:Next", index + 1, totalPictures);
  drawStatusBar(msg);

  Serial.printf("[REVIEW] Displayed: %s\n", path);
}

// ─── Status Bar ──────────────────────────────────────────────────────────────
void drawStatusBar(const char *msg) {
  // 在屏幕底部显示状态信息
  tft.fillRect(0, TFT_H - 20, TFT_W, 20, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(1);
  tft.setCursor(2, TFT_H - 14);
  tft.print(msg);
}

// ─── Button Reading with Debounce ────────────────────────────────────────────
bool readButtons(bool &menuPressed, bool &btn1Pressed, bool &btn2Pressed) {
  unsigned long now = millis();

  menuPressed = false;
  btn1Pressed = false;
  btn2Pressed = false;

  // LOW = 按下 (INPUT_PULLUP)
  if (digitalRead(menuBTN) == LOW && (now - lastMenuTime) > DEBOUNCE_MS) {
    menuPressed   = true;
    lastMenuTime  = now;
    Serial.println("[BTN] MENU pressed");
  }

  if (digitalRead(BTN1) == LOW && (now - lastBtn1Time) > DEBOUNCE_MS) {
    btn1Pressed  = true;
    lastBtn1Time = now;
    Serial.println("[BTN] BTN1 pressed");
  }

  if (digitalRead(BTN2) == LOW && (now - lastBtn2Time) > DEBOUNCE_MS) {
    btn2Pressed  = true;
    lastBtn2Time = now;
    Serial.println("[BTN] BTN2 pressed");
  }

  return menuPressed || btn1Pressed || btn2Pressed;
}
