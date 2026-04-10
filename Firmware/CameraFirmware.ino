#include "esp_camera.h"
#include <WiFi.h>
#include "board_config.h"
#include "Arduino.h"
#include "soc/soc.h"           // Disable brownour problems
#include "soc/rtc_cntl_reg.h"  // Disable brownour problems
#include "driver/rtc_io.h"
#include <EEPROM.h>            // read and write from flash memory
#include <SPI.h>
#include "SdFat.h"
#include <TFT_eSPI.h>

#define TFT_CS  21
#define SD_CS   15

// define the number of bytes you want to access
#define EEPROM_SIZE 1
 
RTC_DATA_ATTR int bootCount = 0;
 
int pictureNumber = 0;
  
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  Serial.begin(115200);
 
  Serial.setDebugOutput(true);
 
  camera_config_t config;
  config.pin_d0 = GPIO_NUM_16;
  config.pin_d1 = GPIO_NUM_47;
  config.pin_d2 = GPIO_NUM_30;
  config.pin_d3 = GPIO_NUM_48;
  config.pin_d4 = GPIO_NUM_14;
  config.pin_d5 = GPIO_NUM_12;
  config.pin_d6 = GPIO_NUM_11;
  config.pin_d7 = GPIO_NUM_10;
  config.pin_xclk;
  config.pin_pclk = GPIO_NUM_13;
  config.pin_vsync = GPIO_NUM_6;
  config.pin_href = GPIO_NUM_8;
  config.pin_sccb_sda = GPIO_NUM_17;
  config.pin_sccb_scl = GPIO_NUM_18;
  config.pin_pwdn = GPIO_NUM_9;
  config.pin_reset = GPIO_NUM_5;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
 
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
 
  // Init  Camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
 
  Serial.println("Starting SD Card");
 
  delay(500);
  if(!SD_MMC.begin()){
    Serial.println("SD Card Mount Failed");
    //return;
  }
 
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD Card attached");
    return;
  }
   
  camera_fb_t * fb = NULL;
 
  // Take Picture with Camera
  fb = esp_camera_fb_get();  
  delay(1000);//This is key to avoid an issue with the image being very dark and green. If needed adjust total delay time.
  fb = esp_camera_fb_get();
  if(!fb) {
    Serial.println("Camera capture failed");
    return;
  }
  // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);
  pictureNumber = EEPROM.read(0) + 1;
 
  // Path where new picture will be saved in SD Card
  String path = "/picture" + String(pictureNumber) +".jpg";
 
  fs::FS &fs = SD_MMC;
  Serial.printf("Picture file name: %s\n", path.c_str());
 
  File file = fs.open(path.c_str(), FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file in writing mode");
  }
  else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved file to path: %s\n", path.c_str());
    EEPROM.write(0, pictureNumber);
    EEPROM.commit();
  }
  file.close();
  esp_camera_fb_return(fb);
  
  delay(1000);
  
  // Turns off the ESP32-CAM white on-board LED (flash) connected to GPIO 4
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_41, 0);
 
  delay(500);
  esp_deep_sleep_start();
  Serial.println("This will never be printed");
} 
 
void loop() {
}


