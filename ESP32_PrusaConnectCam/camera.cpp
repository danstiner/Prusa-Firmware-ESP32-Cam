/**
   @file camera.cpp

   @brief Library for working with a camera

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   https://github.com/espressif/esp32-camera


   @bug: no know bug
*/
#include "camera.h"

Camera SystemCamera(&SystemConfig, &SystemLog, FLASH_GPIO_NUM);
PhotoRequests PhotoCaptureRequests(PHOTO_REQUEST_MAX_WAITING);

/**
   @brief Init camera constructor
   @param Configuration* - pointer to Configuration class
   @param Logs* - pointer to Logs class
   @param uint8_t - flash pin
   @return none
*/
Camera::Camera(Configuration* i_conf, Logs* i_log, int8_t i_FlashPin) {
  config = i_conf;
  log = i_log;

  CameraFlashPin = i_FlashPin;
  StreamLastSendMs = 0;
  StreamEverSent = false;
  frameBufferSemaphore = xSemaphoreCreateMutex();
  photoMutex = xSemaphoreCreateMutex();

  memset(&LastPhoto, 0, sizeof(LastPhoto));
  CameraInitTimeUs = 0;
  CameraCaptureFailedCounter = 0;
}

/**
   @brief Init the camera module and set the camera configuration
   @param none
   @return none
*/
void Camera::Init() {
  log->AddEvent(LogLevel_Info, F("Init camera lib"));

#if (true == ENABLE_CAMERA_FLASH)
  log->AddEvent(LogLevel_Info, F("Init GPIO"));
#if (true == CAMERA_FLASH_PWM_CTRL)
  ledcAttach(FLASH_GPIO_NUM, FLASH_PWM_FREQ, FLASH_PWM_RESOLUTION);

#elif (true == CAMERA_FLASH_DIGITAL_CTRL)
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);
#endif
  SetFlashStatus(false);
#endif

  InitCameraModule();
  ApplyCameraCfg();
  GetCameraModel();
}

/**
   @brief Init the camera module
   @param none
   @return none
*/
void Camera::InitCameraModule() {
  log->AddEvent(LogLevel_Info, F("Init camera module"));
  /* Turn-off the 'brownout detector' */
#if (true == ENABLE_BROWN_OUT_DETECTION)
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif
  esp_err_t err;

  CameraConfig.ledc_channel = LEDC_CHANNEL_0;
  CameraConfig.ledc_timer = LEDC_TIMER_0;
  CameraConfig.pin_d0 = Y2_GPIO_NUM;
  CameraConfig.pin_d1 = Y3_GPIO_NUM;
  CameraConfig.pin_d2 = Y4_GPIO_NUM;
  CameraConfig.pin_d3 = Y5_GPIO_NUM;
  CameraConfig.pin_d4 = Y6_GPIO_NUM;
  CameraConfig.pin_d5 = Y7_GPIO_NUM;
  CameraConfig.pin_d6 = Y8_GPIO_NUM;
  CameraConfig.pin_d7 = Y9_GPIO_NUM;
  CameraConfig.pin_xclk = XCLK_GPIO_NUM;
  CameraConfig.pin_pclk = PCLK_GPIO_NUM;
  CameraConfig.pin_vsync = VSYNC_GPIO_NUM;
  CameraConfig.pin_href = HREF_GPIO_NUM;
  CameraConfig.pin_sccb_sda = SIOD_GPIO_NUM;
  CameraConfig.pin_sccb_scl = SIOC_GPIO_NUM;
  CameraConfig.pin_pwdn = PWDN_GPIO_NUM;
  CameraConfig.pin_reset = RESET_GPIO_NUM;
  CameraConfig.xclk_freq_hz = 15000000;       // or 3000000; 16500000; 20000000
  CameraConfig.pixel_format = PIXFORMAT_JPEG; /* YUV422,GRAYSCALE,RGB565,JPEG */

  /* OV2640
    FRAMESIZE_QVGA (320 x 240)
    FRAMESIZE_CIF (352 x 288)
    FRAMESIZE_VGA (640 x 480)
    FRAMESIZE_SVGA (800 x 600)
    FRAMESIZE_XGA (1024 x 768)
    FRAMESIZE_SXGA (1280 x 1024)
    FRAMESIZE_UXGA (1600 x 1200)

    CAMERA_GRAB_WHEN_EMPTY - Fills buffers when they are empty. Less resources but first 'fb_count' frames might be old
    CAMERA_GRAB_LATEST     - Except when 1 frame buffer is used, queue will always contain the last 'fb_count' frames
  */

  CameraConfig.frame_size = TFrameSize;             /* FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA */
  CameraConfig.jpeg_quality = PhotoQuality;         /* 10-63 lower number means higher quality */
  CameraConfig.fb_count = 1;                        /* picture frame buffer alocation */
  CameraConfig.grab_mode = CAMERA_GRAB_LATEST;      /* CAMERA_GRAB_WHEN_EMPTY or CAMERA_GRAB_LATEST */
#if (true == ENABLE_PSRAM)
  CameraConfig.fb_location = CAMERA_FB_IN_PSRAM;    /* CAMERA_FB_IN_PSRAM or CAMERA_FB_IN_DRAM  */
#else
  CameraConfig.fb_location = CAMERA_FB_IN_DRAM;     /* CAMERA_FB_IN_PSRAM or CAMERA_FB_IN_DRAM  */
#endif
  
  if (CameraConfig.fb_location == CAMERA_FB_IN_DRAM) {
    log->AddEvent(LogLevel_Verbose, F("Camera frame buffer location: DRAM"));
  } else if (CameraConfig.fb_location == CAMERA_FB_IN_PSRAM) {
    log->AddEvent(LogLevel_Verbose, F("Camera frame buffer location: PSRAM"));
  } else {
    log->AddEvent(LogLevel_Verbose, F("Camera frame buffer location: Unknown"));
  }

  /* Camera init */
  err = esp_camera_init(&CameraConfig);
  if (err != ESP_OK) {
    log->AddEvent(LogLevel_Warning, F("Camera init failed. Error: "), String(err, HEX));
    log->AddEvent(LogLevel_Warning, F("Reset ESP32-cam!"));
    ESP.restart();
  } 
  CameraInitTimeUs = esp_timer_get_time();
}

/**
   @brief Load camera CFG from EEPROM
   @param none
   @return none
*/
void Camera::LoadCameraCfgFromEeprom() {
  log->AddEvent(LogLevel_Info, F("Load camera CFG from EEPROM"));
  PhotoQuality = config->LoadPhotoQuality();
  FrameSize = config->LoadFrameSize();
  TFrameSize = TransformFrameSizeDataType(config->LoadFrameSize());
  brightness = config->LoadBrightness();
  contrast = config->LoadContrast();
  saturation = config->LoadSaturation();
  awb = config->LoadAwb();
  awb_gain = config->LoadAwbGain();
  wb_mode = config->LoadAwbMode();
  aec2 = config->LoadAec2();
  ae_level = config->LoadAeLevel();
  aec_value = config->LoadAecValue();
  gain_ctrl = config->LoadGainCtrl();
  agc_gain = config->LoadAgcGain();
  bpc = config->LoadBpc();
  wpc = config->LoadWpc();
  raw_gama = config->LoadRawGama();
  hmirror = config->LoadHmirror();
  vflip = config->LoadVflip();
  lensc = config->LoadLensCorrect();
  exposure_ctrl = config->LoadExposureCtrl();
  CameraFlashEnable = config->LoadCameraFlashEnable();
  CameraFlashTime = config->LoadCameraFlashTime();
  imageExifRotation = config->LoadCameraImageExifRotation();
}

/**
   @brief transform uint8_t from web interface to framesize_t
   @param uint8_t - int value from web
   @return framesize_t
*/
framesize_t Camera::TransformFrameSizeDataType(uint8_t i_data) {
  framesize_t ret = FRAMESIZE_QVGA;
  switch (i_data) {
    case 0:
      ret = FRAMESIZE_QVGA;
      break;
    case 1:
      ret = FRAMESIZE_CIF;
      break;
    case 2:
      ret = FRAMESIZE_VGA;
      break;
    case 3:
      ret = FRAMESIZE_SVGA;
      break;
    case 4:
      ret = FRAMESIZE_XGA;
      break;
    case 5:
      ret = FRAMESIZE_SXGA;
      break;
    case 6:
      ret = FRAMESIZE_UXGA;
      break;
    default:
      ret = FRAMESIZE_QVGA;
      log->AddEvent(LogLevel_Warning, F("Bad frame size. Set default value. "), String(i_data));
      break;
  }

  return ret;
}

/**
   @brief Function set flash status
   @param bool i_data - true = on, false = off
   @return none
*/
void Camera::SetFlashStatus(bool i_data) {
#if (true == ENABLE_CAMERA_FLASH)
/* PWM control of the FLASH */
#if (true == CAMERA_FLASH_PWM_CTRL)
  if (true == i_data) {
    ledcWrite(FLASH_GPIO_NUM, FLASH_ON_STATUS);
  } else if (false == i_data) {
    ledcWrite(FLASH_GPIO_NUM, FLASH_OFF_STATUS);
  }

/* Digital control of the FLASH */
#elif (true == CAMERA_FLASH_DIGITAL_CTRL)
  if (true == i_data) {
    digitalWrite(FLASH_GPIO_NUM, HIGH);
  } else if (false == i_data) {
    digitalWrite(FLASH_GPIO_NUM, LOW);
  }
#endif

/* rgbLedWrite control of the FLASH */
#if (true == CAMERA_FLASH_NEOPIXEL)
  if (true == i_data) {
    rgbLedWrite(FLASH_NEOPIXEL_LED_PIN, RGB_BRIGHTNESS, RGB_BRIGHTNESS, RGB_BRIGHTNESS);
  } else if (false == i_data) {
    rgbLedWrite(FLASH_NEOPIXEL_LED_PIN, 0, 0, 0);
  }
#endif
#endif
}

/**
   @brief Function get flash status
   @param none
   @return bool - true = on, false = off
*/
bool Camera::GetFlashStatus() {
#if (true == ENABLE_CAMERA_FLASH)
#if (true == CAMERA_FLASH_PWM_CTRL)
  if (ledcRead(FLASH_GPIO_NUM) == FLASH_OFF_STATUS) {
    return false;
  } else if (ledcRead(FLASH_GPIO_NUM) == FLASH_ON_STATUS) {
    return true;
  }

#elif (true == CAMERA_FLASH_DIGITAL_CTRL)
  if (digitalRead(FLASH_GPIO_NUM) == LOW) {
    return false;
  } else if (digitalRead(FLASH_GPIO_NUM) == HIGH) {
    return true;
  }
#endif
#endif
  return false;
}

/**
   @brief Function set camera config. Waits for any frame or photo being taken, so the sensor exists.
   @param none
   @return none
*/
void Camera::ApplyCameraCfg() {
  xSemaphoreTake(frameBufferSemaphore, portMAX_DELAY);
  ApplyCameraCfgUnlocked();
  xSemaphoreGive(frameBufferSemaphore);
}

/**
   @brief Apply the camera configuration to the sensor. The caller holds frameBufferSemaphore.
   @param none
   @return none
*/
void Camera::ApplyCameraCfgUnlocked() {
  log->AddEvent(LogLevel_Info, F("Set camera CFG"));

  /* sensor configuration */
  sensor = esp_camera_sensor_get();
  if (NULL == sensor) {
    log->AddEvent(LogLevel_Error, F("Camera sensor is NULL"));
    return;
  }
  sensor->set_brightness(sensor, brightness);         // -2 to 2
  sensor->set_contrast(sensor, contrast);             // -2 to 2
  sensor->set_saturation(sensor, saturation);         // -2 to 2
  sensor->set_special_effect(sensor, 0);              // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  sensor->set_whitebal(sensor, awb);                  // automatic white balancing 0 = disable , 1 = enable
  sensor->set_awb_gain(sensor, awb_gain);             // automatic white balancing gain 0 = disable , 1 = enable
  sensor->set_wb_mode(sensor, wb_mode);               // white balancing mode 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  sensor->set_exposure_ctrl(sensor, exposure_ctrl);   // exposition controll 0 = disable , 1 = enable
  sensor->set_aec2(sensor, aec2);                     // enable exposition controll 0 = disable , 1 = enable
  sensor->set_ae_level(sensor, ae_level);             // automatic exposition level -2 to 2
  sensor->set_aec_value(sensor, aec_value);           // expozition time - 0 to 1200
  sensor->set_gain_ctrl(sensor, gain_ctrl);           // automatic gain controll 0 = disable , 1 = enable
  sensor->set_agc_gain(sensor, agc_gain);             // automatic gain controll level 0 to 30
  sensor->set_gainceiling(sensor, (gainceiling_t)0);  // maximum gain 0 to 6
  sensor->set_bpc(sensor, bpc);                       // bad pixel correction 0 = disable , 1 = enable
  sensor->set_wpc(sensor, wpc);                       // white pixel correction 0 = disable , 1 = enable
  sensor->set_raw_gma(sensor, raw_gama);              // raw gama correction 0 = disable , 1 = enable
  sensor->set_lenc(sensor, lensc);                    // lens correction 0 = disable , 1 = enable
  sensor->set_hmirror(sensor, hmirror);               // horizontal mirror 0 = disable , 1 = enable
  sensor->set_vflip(sensor, vflip);                   // vertical flip 0 = disable , 1 = enable
  sensor->set_dcw(sensor, 1);                         // 0 = disable , 1 = enable
  sensor->set_colorbar(sensor, 0);                    // external collor lines, 0 = disable , 1 = enable
}

/**
   @brief Function for reinit camera module
   @param none
   @return none
*/
void Camera::ReinitCameraModule() {
  /* no frame or photo may be taken while the driver is gone */
  xSemaphoreTake(frameBufferSemaphore, portMAX_DELAY);
  esp_err_t err = esp_camera_deinit();
  if (err != ESP_OK) {
    log->AddEvent(LogLevel_Warning, F("Camera error deinit camera module. Error: "), String(err, HEX));
  }
  delay(100);
  InitCameraModule();
  ApplyCameraCfgUnlocked();
  CameraCaptureFailedCounter = 0;
  xSemaphoreGive(frameBufferSemaphore);
}

/**
   @brief Re-initialise the camera after more than CAMERA_MAX_FAIL_CAPTURE failed captures in a row. Called on every photo task tick.
   @param none
   @return none
*/
void Camera::ReinitIfCaptureFailing() {
  if (CameraCaptureFailedCounter > CAMERA_MAX_FAIL_CAPTURE) {
    log->AddEvent(LogLevel_Error, F("Camera capture failed! photo max attempts"));
    ReinitCameraModule();
  }
}

/**
   @brief Function for get camera model and type
   @param void
   @return none
*/
void Camera::GetCameraModel() {
  log->AddEvent(LogLevel_Info, F("Get camera model and type"));
  if (sensor == NULL) {
    log->AddEvent(LogLevel_Error, F("Camera sensor is NULL"));
    return;
  } 
  
  camera_sensor_info_t *info = esp_camera_sensor_get_info(&sensor->id);
  if (info == NULL) {
    log->AddEvent(LogLevel_Error, F("Camera sensor info is NULL"));
    return;
  }

  CameraType = (camera_pid_t) sensor->id.PID;
  CameraName = info->name;
  log->AddEvent(LogLevel_Info, F("Camera type: "), String(CameraType));
  log->AddEvent(LogLevel_Info, F("Camera name: "), String(CameraName));
  log->AddEvent(LogLevel_Info, F("Camera model: "), String(info->model));
  log->AddEvent(LogLevel_Info, F("Camera PID: "), String(info->pid));
  log->AddEvent(LogLevel_Info, F("Camera MAX framesize: "), String(info->max_size));
  log->AddEvent(LogLevel_Info, F("Camera support jpeg: "), String(info->support_jpeg));
}

/**
   @brief The take function: take a valid, fresh frame from the driver, copy it into io_dst and hand the driver buffer
          straight back. Holds frameBufferSemaphore throughout, and i_opt.dst_mutex around the copy only. Does not log.
   @param FrameCopy& io_dst - destination, grown if needed; unchanged unless the result is TakeResult_Ok
   @param TakeOptions& i_opt - validity check, "not before" time, limits, EXIF, destination mutex
   @param uint8_t* o_returned - if not NULL, the number of frames handed back unused
   @return TakeResult_enum
*/
TakeResult_enum Camera::TakeFrame(FrameCopy &io_dst, const TakeOptions &i_opt, uint8_t *o_returned) {
  TakeResult_enum result = TakeResult_NoValidFrame;
  uint8_t returned = 0;
  uint8_t invalid = 0;

  xSemaphoreTake(frameBufferSemaphore, portMAX_DELAY);
  const int64_t start_us = esp_timer_get_time();

  /* skip the first frames after an init, they are often dark */
  int64_t not_before_us = CameraInitTimeUs + ((int64_t)CAMERA_SETTLE_TIME * 1000);
  if (i_opt.not_before_us > not_before_us) {
    not_before_us = i_opt.not_before_us;
  }

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (NULL == fb) {
      /* no retry: on the ESP32-S3 one failed get already took up to 8 s */
      result = TakeResult_NoFrame;
      break;
    }

    const bool valid = FrameIsValid(fb, i_opt.photo_check);
    const int64_t started_us = ((int64_t)fb->timestamp.tv_sec * 1000000) + fb->timestamp.tv_usec;
    if ((true == valid) && (started_us >= not_before_us)) {
      if (NULL != i_opt.dst_mutex) {
        xSemaphoreTake(i_opt.dst_mutex, portMAX_DELAY);
      }
      const bool copied = CopyFrame(io_dst, fb, i_opt.exif);
      if (NULL != i_opt.dst_mutex) {
        xSemaphoreGive(i_opt.dst_mutex);
      }
      esp_camera_fb_return(fb);
      result = (true == copied) ? TakeResult_Ok : TakeResult_NoMemory;
      break;
    }

    /* an old frame does not count towards the limit: how many arrive before "not before" depends on the frame rate */
    esp_camera_fb_return(fb);
    returned++;
    if (false == valid) {
      invalid++;
    }
    if ((invalid >= i_opt.max_invalid) || ((esp_timer_get_time() - start_us) >= ((int64_t)CAMERA_TAKE_RETRY_BUDGET * 1000))) {
      result = TakeResult_NoValidFrame;
      break;
    }
  }

  /* only camera faults count towards a re-init */
  if (TakeResult_Ok == result) {
    CameraCaptureFailedCounter = 0;
  } else if ((TakeResult_NoMemory != result) && (CameraCaptureFailedCounter < UINT8_MAX)) {
    CameraCaptureFailedCounter++;
  }
  xSemaphoreGive(frameBufferSemaphore);

  if (NULL != o_returned) {
    *o_returned = returned;
  }
  return result;
}

/**
   @brief Check a driver frame: long enough, within the JPEG buffer size, and for photos byte 15 is 0
   @param camera_fb_t* i_fb - the frame
   @param bool i_photo_check - also check byte 15
   @return bool
*/
bool Camera::FrameIsValid(const camera_fb_t *i_fb, bool i_photo_check) {
  if (i_fb->len <= 100) {
    return false;
  }
  /* the driver never fills more than width x height / 5 */
  if (i_fb->len > ((size_t)i_fb->width * i_fb->height / 5)) {
    return false;
  }
  if ((true == i_photo_check) && (0x00 != i_fb->buf[15])) {
    return false;
  }
  return true;
}

/**
   @brief Copy a driver frame into io_dst, with the EXIF header in front when asked. When the frame does not fit,
          io_dst grows to width x height / 5 plus the header, so later frames of this resolution fit; it never shrinks.
   @param FrameCopy& io_dst - destination
   @param camera_fb_t* i_fb - driver frame
   @param bool i_exif - write the EXIF header in front
   @return bool - false when io_dst could not grow; it then keeps its old content
*/
bool Camera::CopyFrame(FrameCopy &io_dst, camera_fb_t *i_fb, bool i_exif) {
  const uint8_t *exif = NULL;
  size_t exif_len = 0;
  size_t offset = 0;

  if (true == i_exif) {
    /* the header replaces the JPEG's SOI and JFIF segment */
    update_exif_from_cfg(imageExifRotation);
    get_exif_header(i_fb, &exif, &exif_len);
    offset = get_jpeg_data_offset(i_fb);
    if ((NULL == exif) || (0 == offset)) {
      exif = NULL;
      exif_len = 0;
      offset = 0;
    }
  }

  const size_t need = exif_len + i_fb->len - offset;
  if (io_dst.cap < need) {
    size_t want = ((size_t)i_fb->width * i_fb->height / 5) + exif_len;
    if (want < need) {
      want = need;
    }
    /* PSRAM only: never fall back to internal RAM, which the upload's TLS needs */
    uint8_t *buf = (uint8_t *)heap_caps_realloc(io_dst.fb.buf, want, MALLOC_CAP_SPIRAM);
    if (NULL == buf) {
      return false;
    }
    io_dst.fb.buf = buf;
    io_dst.cap = want;
  }

  if (0 != exif_len) {
    memcpy(io_dst.fb.buf, exif, exif_len);
  }
  memcpy(io_dst.fb.buf + exif_len, i_fb->buf + offset, i_fb->len - offset);
  io_dst.fb.len = need;
  io_dst.fb.width = i_fb->width;
  io_dst.fb.height = i_fb->height;
  io_dst.fb.format = i_fb->format;
  io_dst.fb.timestamp = i_fb->timestamp;
  return true;
}

/**
   @brief Take a photo into P, the last photo, with the EXIF header in front.
          Uses the flash only when no stream is running, so the stream does not flicker.
   @param none
   @return bool - true when P holds the new photo; on failure P keeps the previous one
*/
bool Camera::CapturePhoto() {
  const bool flash = (true == CameraFlashEnable) && (false == GetStreamStatus());
  if (true == flash) {
    SetFlashStatus(true);
    delay(CameraFlashTime);
  }

  /* a frame that started now, after the flash came on */
  TakeOptions opt;
  opt.photo_check = true;
  opt.not_before_us = esp_timer_get_time();
  opt.max_invalid = CAMERA_TAKE_MAX_INVALID;
  opt.exif = true;
  opt.dst_mutex = photoMutex;
  uint8_t returned = 0;
  TakeResult_enum result = TakeFrame(LastPhoto, opt, &returned);

  /* turn the flash off, unless /light turned it on meanwhile */
  if ((true == flash) && (true == CameraFlashEnable)) {
    SetFlashStatus(false);
  }

  char buf[150] = { '\0' };
  switch (result) {
    case TakeResult_Ok:
      snprintf(buf, sizeof(buf), "Photo OK. Size: %u bytes, resolution: %u x %u, frames handed back: %u", (unsigned)LastPhoto.fb.len, (unsigned)LastPhoto.fb.width, (unsigned)LastPhoto.fb.height, (unsigned)returned);
      log->AddEvent(LogLevel_Info, buf);
      break;
    case TakeResult_NoFrame:
      log->AddEvent(LogLevel_Error, F("Camera capture failed! photo. Failed count: "), String(CameraCaptureFailedCounter));
      break;
    case TakeResult_NoValidFrame:
      log->AddEvent(LogLevel_Error, F("No valid photo frame. Frames handed back: "), String(returned));
      break;
    case TakeResult_NoMemory:
      log->AddEvent(LogLevel_Error, F("Not enough memory for the photo"));
      break;
  }
  return (TakeResult_Ok == result);
}

/**
   @brief Take the next stream frame into a viewer's copy (V)
   @param FrameCopy& io_jpg - the viewer's copy
   @param int64_t i_not_before_us - reject frames that started earlier; the connection time for a viewer's first frame, else 0
   @return bool - false when no frame could be taken; the stream then ends
*/
bool Camera::CaptureStream(FrameCopy &io_jpg, int64_t i_not_before_us) {
  TakeOptions opt;
  opt.photo_check = false;
  opt.not_before_us = i_not_before_us;
  opt.max_invalid = CAMERA_TAKE_MAX_INVALID;
  opt.exif = (true == CAMERA_EXIF_ROTATION_STREAM) && (1 != imageExifRotation);
  opt.dst_mutex = NULL;
  return (TakeResult_Ok == TakeFrame(io_jpg, opt, NULL));
}

/**
   @brief Record a stream send callback, for GetStreamStatus()
   @param none
   @return none
*/
void Camera::StreamMarkSend() {
  StreamLastSendMs = millis();
  StreamEverSent = true;
}

/**
   @brief Get Stream Status
   @param none
   @return bool - true when a stream sent data within the last HTTP_CLIENT_SILENT_TIMEOUT; a frozen viewer stops counting
*/
bool Camera::GetStreamStatus() {
  /* read before millis(), so a send between the two reads cannot wrap the difference */
  const uint32_t last_send_ms = StreamLastSendMs;
  return (true == StreamEverSent) && ((millis() - last_send_ms) < HTTP_CLIENT_SILENT_TIMEOUT);
}

/**
   @brief Set Frame Size
   @param uint16_t - frame size
   @return none
*/
void Camera::StreamSetFrameSize(uint16_t i_data) {
  StreamAverageSize = (StreamAverageSize + i_data) / 2;
}

/**
   @brief Set average Frame Fps
   @param float - frame fps
   @return none
*/
void Camera::StreamSetFrameFps(float i_data) {
  StreamAverageFps = (StreamAverageFps + i_data) / 2.0;
}

/**
   @brief Get average stream Frame Size
   @param none
   @return uint16_t - frame size
*/
uint16_t Camera::StreamGetFrameAverageSize() {
  return StreamAverageSize;
}

/**
   @brief Get average stream Frame Fps
   @param none
   @return float - frame fps
*/
float Camera::StreamGetFrameAverageFps() {
  return StreamAverageFps;
}

/**
   @brief Clear Frame Data
   @param none
   @return none
*/
void Camera::StreamClearFrameData() {
  StreamAverageFps = 0.0;
  StreamAverageSize = 0;
}

/**
   @brief Get Photo Frame Buffer
   @param none
   @return camera_fb_t* - P, the last photo with EXIF in front. Only the photo task (its only writer) reads it directly; downloads copy it under photoMutex.
*/
camera_fb_t* Camera::GetPhotoFb() {
  return &LastPhoto.fb;
}

/**
   @brief Set Photo Quality
   @param uint8_t - photo quality
   @return none
*/
void Camera::SetPhotoQuality(uint8_t i_data) {
  config->SavePhotoQuality(i_data);
  PhotoQuality = i_data;
  ReinitCameraModule();
}

/**
   @brief Set Frame Size
   @param uint8_t - frame size
   @return none
*/
void Camera::SetFrameSize(uint8_t i_data) {
  config->SaveFrameSize(i_data);
  FrameSize = i_data;
  TFrameSize = TransformFrameSizeDataType(i_data);
  ReinitCameraModule();
}

/**
   @brief Set Brightness
   @param int8_t - brightness
   @return none
*/
void Camera::SetBrightness(int8_t i_data) {
  config->SaveBrightness(i_data);
  brightness = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set Contrast
   @param int8_t - contrast
   @return none
*/
void Camera::SetContrast(int8_t i_data) {
  config->SaveContrast(i_data);
  contrast = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set Saturation
   @param int8_t - saturation
   @return none
*/
void Camera::SetSaturation(int8_t i_data) {
  config->SaveSaturation(i_data);
  saturation = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set automatic white balance
   @param bool - automatic white balance
   @return none
*/
void Camera::SetAwb(bool i_data) {
  config->SaveAwb(i_data);
  awb = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set automatic white balance gain
   @param bool - automatic white balance gain
   @return none
*/
void Camera::SetAwbGain(bool i_data) {
  config->SaveAwbGain(i_data);
  awb_gain = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set automatic white balance mode
   @param uint8_t - automatic white balance mode
   @return none
*/
void Camera::SetAwbMode(uint8_t i_data) {
  config->SaveAwbMode(i_data);
  wb_mode = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set automatic exposure control 2
   @param bool - automatic exposure control 2
   @return none
*/
void Camera::SetAec2(bool i_data) {
  config->SaveAec2(i_data);
  aec2 = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set automatic exposure level
   @param int8_t - automatic exposure level
   @return none
*/
void Camera::SetAeLevel(int8_t i_data) {
  config->SaveAeLevel(i_data);
  ae_level = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set automatic exposition control value
   @param uint16_t - automatic exposure control value
   @return none
*/
void Camera::SetAecValue(uint16_t i_data) {
  config->SaveAecValue(i_data);
  aec_value = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set Gain Ctrl
   @param bool - gain ctrl
   @return none
*/
void Camera::SetGainCtrl(bool i_data) {
  config->SaveGainCtrl(i_data);
  gain_ctrl = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set automatic gain control gain
   @param uint8_t - automatic gain control gain
   @return none
*/
void Camera::SetAgcGain(uint8_t i_data) {
  config->SaveAgcGain(i_data);
  agc_gain = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set bad pixel correction
   @param bool - bad pixel correction
   @return none
*/
void Camera::SetBpc(bool i_data) {
  config->SaveBpc(i_data);
  bpc = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set white pixel correction
   @param bool - white pixel correction
   @return none
*/
void Camera::SetWpc(bool i_data) {
  config->SaveWpc(i_data);
  wpc = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set Raw Gama
   @param bool - raw gama
   @return none
*/
void Camera::SetRawGama(bool i_data) {
  config->SaveRawGama(i_data);
  raw_gama = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set horizontal Mirror
   @param bool - horizontal mirror
   @return none
*/
void Camera::SetHMirror(bool i_data) {
  config->SaveHmirror(i_data);
  hmirror = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set V Flip
   @param bool - vflip
   @return none
*/
void Camera::SetVFlip(bool i_data) {
  config->SaveVflip(i_data);
  vflip = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set Lens correction
   @param bool - lens correction
   @return none
*/
void Camera::SetLensC(bool i_data) {
  config->SaveLensCorrect(i_data);
  lensc = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set Exposure Ctrl
   @param bool - exposure ctrl
   @return none
*/
void Camera::SetExposureCtrl(bool i_data) {
  config->SaveExposureCtrl(i_data);
  exposure_ctrl = i_data;
  ApplyCameraCfg();
}

/**
   @brief Set Camera Flash Enable
   @param bool - flash enable/disable
   @return none
*/
void Camera::SetCameraFlashEnable(bool i_data) {
  config->SaveCameraFlashEnable(i_data);
  CameraFlashEnable = i_data;
}

/**
   @brief Set Camera Flash Time
   @param uint16_t - flash time
   @return none
*/
void Camera::SetCameraFlashTime(uint16_t i_data) {
  config->SaveCameraFlashTime(i_data);
  CameraFlashTime = i_data;
}

void Camera::SetCameraImageRotation(uint8_t i_data) {
  config->SaveCameraImageExifRotation(i_data);
  imageExifRotation = i_data;
}

/**
   @brief Get Photo Quality
   @param none
   @return uint8_t - photo quality
*/
uint8_t Camera::GetPhotoQuality() {
  return PhotoQuality;
}

/**
   @brief Get Frame Size
   @param none
   @return uint8_t - frame size
*/
uint8_t Camera::GetFrameSize() {
  return FrameSize;
}

/**
 * @brief transform framesize_t to uint16_t width
 * 
    FRAMESIZE_QVGA (320 x 240)
    FRAMESIZE_CIF (352 x 288)
    FRAMESIZE_VGA (640 x 480)
    FRAMESIZE_SVGA (800 x 600)
    FRAMESIZE_XGA (1024 x 768)
    FRAMESIZE_SXGA (1280 x 1024)
    FRAMESIZE_UXGA (1600 x 1200)
 * 
 * @return uint16_t 
 */
uint16_t Camera::GetFrameSizeWidth() {
  uint16_t ret = 0;

  switch (FrameSize) {
    case 0:
      ret = 320;
      break;
    case 1:
      ret = 352;
      break;
    case 2:
      ret = 640;
      break;
    case 3:
      ret = 800;
      break;
    case 4:
      ret = 1024;
      break;
    case 5:
      ret = 1280;
      break;
    case 6:
      ret = 1600;
      break;
    default:
      ret = 320;
      break;
  }

  return ret;
}

/**
 * @brief transform framesize_t to uint16_t height
 * 
    FRAMESIZE_QVGA (320 x 240)
    FRAMESIZE_CIF (352 x 288)
    FRAMESIZE_VGA (640 x 480)
    FRAMESIZE_SVGA (800 x 600)
    FRAMESIZE_XGA (1024 x 768)
    FRAMESIZE_SXGA (1280 x 1024)
    FRAMESIZE_UXGA (1600 x 1200)
 * 
 * @return uint16_t 
 */
uint16_t Camera::GetFrameSizeHeight() {
  uint16_t ret = 0;
  switch (FrameSize) {
    case 0:
      ret = 240;
      break;
    case 1:
      ret = 288;
      break;
    case 2:
      ret = 480;
      break;
    case 3:
      ret = 600;
      break;
    case 4:
      ret = 768;
      break;
    case 5:
      ret = 1024;
      break;
    case 6:
      ret = 1200;
      break;
    default:
      ret = 240;
      break;
  }

  return ret;
}

/**
   @brief Get Brightness
   @param none
   @return int8_t - brightness
*/
int8_t Camera::GetBrightness() {
  return brightness;
}

/**
   @brief Get Contrast
   @param none
   @return int8_t - contrast
*/
int8_t Camera::GetContrast() {
  return contrast;
}

/**
   @brief Get Saturation
   @param none
   @return int8_t - saturation
*/
int8_t Camera::GetSaturation() {
  return saturation;
}

/**
   @brief Get Auto white balance status
   @param none
   @return bool - Auto white balance status
*/
bool Camera::GetAwb() {
  return awb;
}

/**
   @brief Get Auto white balance gain status
   @param none
   @return bool -  Auto white balance gain status
*/
bool Camera::GetAwbGain() {
  return awb_gain;
}

/**
   @brief Get Auto white balance mode
   @param none
   @return uint8_t - Auto white balance mode
*/
uint8_t Camera::GetAwbMode() {
  return wb_mode;
}

/**
   @brief Get automatic exposure control 2 status
   @param none
   @return bool - Automatic exposure control 2 status
*/
bool Camera::GetAec2() {
  return aec2;
}

/**
   @brief Get automatic exposure level
   @param none
   @return int8_t - Automatic exposure level
*/
int8_t Camera::GetAeLevel() {
  return ae_level;
}

/**
   @brief Get automatic exposure control value
   @param none
   @return uint16_t - Automatic exposure control value
*/
uint16_t Camera::GetAecValue() {
  return aec_value;
}

/**
   @brief Get Gain Ctrl status
   @param none
   @return bool - Gain control status
*/
bool Camera::GetGainCtrl() {
  return gain_ctrl;
}

/**
   @brief Get Agc Gaint
   @param none
   @return uint8_t - agc gain
*/
uint8_t Camera::GetAgcGaint() {
  return agc_gain;
}

/**
   @brief Get Bpc
   @param none
   @return bool - bpc status
*/
bool Camera::GetBpc() {
  return bpc;
}

/**
   @brief Get Wpc
   @param none
   @return bool - wpc status
*/
bool Camera::GetWpc() {
  return wpc;
}

/**
   @brief Get Raw Gama value
   @param none
   @return bool - raw gamma value
*/
bool Camera::GetRawGama() {
  return raw_gama;
}

/**
   @brief Get horizontal Mirror status
   @param none
   @return bool - horizontal mirror status
*/
bool Camera::GetHMirror() {
  return hmirror;
}

/**
   @brief Get vertical Flip status
   @param none
   @return bool - vertical flip status
*/
bool Camera::GetVFlip() {
  return vflip;
}

/**
   @brief Get Lens correction status
   @param none
   @return bool - lens correction status
*/
bool Camera::GetLensC() {
  return lensc;
}

/**
   @brief Get exposure control status
   @param none
   @return bool - exposure control status
*/
bool Camera::GetExposureCtrl() {
  return exposure_ctrl;
}

/**
   @brief Get Camera Flash Enable status
   @param none
   @return bool - camera flash enable status
*/
bool Camera::GetCameraFlashEnable() {
  return CameraFlashEnable;
}

/**
 * @brief Get camera flash time
 * @param none
 * @return uint16_t - camera flash time
 */
uint16_t Camera::GetCameraFlashTime() {
  return CameraFlashTime;
}

uint8_t Camera::GetCameraImageRotation() {
  return imageExifRotation;
}

/* EOF */