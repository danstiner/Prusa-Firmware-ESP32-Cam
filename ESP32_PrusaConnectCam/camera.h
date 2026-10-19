/**
   @file camera.h

   @brief Library for working with a camera

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/

#pragma once

#include <atomic>
#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

/* A JPEG copied out of the driver. fb describes the copy; cap is the allocated size of fb.buf, which only grows. */
/* Must stay above #include "cfg.h": cfg.h reaches WebStream.h, which holds a FrameCopy. */
struct FrameCopy {
  camera_fb_t fb;  ///< buf, len, width, height, format and timestamp of the copy
  size_t cap;      ///< allocated size of fb.buf
};

#include "micro_sd.h"
#include "log.h"
#include "cfg.h"
#include "exif.h"
#include "module_templates.h"
#include "mcu_cfg.h"
#include "var.h"
#include "photo_requests.h"

class Configuration;

/* What the take function accepts, and how it copies */
struct TakeOptions {
  bool photo_check;             ///< also require byte 15 to be 0, a check verified on the OV2640 only
  int64_t not_before_us;        ///< reject frames that started earlier, esp_timer_get_time() clock
  uint8_t max_invalid;          ///< stop after this many invalid frames
  bool exif;                    ///< write the EXIF header in front of the image
  SemaphoreHandle_t dst_mutex;  ///< taken around the copy into the destination, or NULL
};

/* Result of the take function */
enum TakeResult_enum {
  TakeResult_Ok,            ///< the destination holds the new frame
  TakeResult_NoFrame,       ///< the driver returned no frame
  TakeResult_NoValidFrame,  ///< no valid, fresh frame within the limits
  TakeResult_NoMemory,      ///< the destination could not grow; it keeps its old content
};

class Camera {
private:
  uint8_t PhotoQuality;      ///< photo quality
  uint8_t FrameSize;         ///< framesize
  int8_t brightness;         ///< brightness
  int8_t contrast;           ///< contrast
  int8_t saturation;         ///< saturation
  bool awb;                  ///< automatic white balancing
  bool awb_gain;             ///< automatic white balancing gain
  uint8_t wb_mode;           ///< white balancing mode
  bool aec2;                 ///< automatic exposition controll
  int8_t ae_level;           ///< automatic exposition level
  uint16_t aec_value;        ///< automatic exposition time
  bool gain_ctrl;            ///< automatic gain
  uint8_t agc_gain;          ///< automatic gain level
  bool bpc;                  ///< bad pixel correction
  bool wpc;                  ///< white pixel correction
  bool raw_gama;             ///< raw gama correction
  bool hmirror;              ///< horizontal mirror
  bool vflip;                ///< vertical flip
  bool lensc;                ///< lens corection
  bool exposure_ctrl;        ///< exposure control
  bool CameraFlashEnable;    ///< enable/disable camera flash function
  uint16_t CameraFlashTime;  ///< camera fash duration time
  int8_t CameraFlashPin;     ///< GPIO pin for LED
  framesize_t TFrameSize;    ///< framesize_t type for camera module
  uint8_t imageExifRotation; ///< image rotation. 0 degree: value 1, 90 degree: value 6, 180 degree: value 3, 270 degree: value 8

  /* OV2640 camera module pinout and cfg*/
  camera_config_t CameraConfig;             ///< camera configuration
  sensor_t* sensor;                         ///< sensor
  std::atomic<uint32_t> StreamLastSendMs;   ///< millis() of the last stream send callback
  std::atomic<bool> StreamEverSent;         ///< a stream send callback ran since boot
  SemaphoreHandle_t frameBufferSemaphore;   ///< camera mutex: the driver, re-init and settings
  float StreamAverageFps;                   ///< stream average fps
  uint16_t StreamAverageSize;               ///< stream average size
  uint8_t CameraCaptureFailedCounter;       ///< camera capture failed counter, changed only under frameBufferSemaphore
  camera_pid_t CameraType;                  ///< camera type
  String CameraName;                        ///< camera name
  FrameCopy LastPhoto;                      ///< P: the last photo, EXIF in front; written only by the photo task (and setup() once)
  SemaphoreHandle_t photoMutex;             ///< guards LastPhoto's buffer and length
  int64_t CameraInitTimeUs;                 ///< when the camera was last initialised, esp_timer_get_time() clock

  Configuration *config;                    ///< pointer to Configuration object
  Logs *log;                                ///< pointer to Logs object

  void InitCameraModule();
  void ApplyCameraCfgUnlocked();
  TakeResult_enum TakeFrame(FrameCopy &, const TakeOptions &, uint8_t *);
  bool FrameIsValid(const camera_fb_t *, bool);
  bool CopyFrame(FrameCopy &, camera_fb_t *, bool);

public:
  Camera(Configuration*, Logs*, int8_t);
  ~Camera(){};
  void Init();
  void ApplyCameraCfg();
  void LoadCameraCfgFromEeprom();
  void ReinitCameraModule();
  void ReinitIfCaptureFailing();
  void GetCameraModel();
  bool CapturePhoto();
  bool CaptureStream(FrameCopy &, int64_t);
  void StreamMarkSend();
  bool GetStreamStatus();

  void StreamSetFrameSize(uint16_t);
  void StreamSetFrameFps(float);
  uint16_t StreamGetFrameAverageSize();
  float StreamGetFrameAverageFps();
  void StreamClearFrameData();

  camera_fb_t *GetPhotoFb();
  framesize_t TransformFrameSizeDataType(uint8_t);

  void SetFlashStatus(bool);
  bool GetFlashStatus();

  void SetPhotoQuality(uint8_t);
  void SetFrameSize(uint8_t);
  void SetBrightness(int8_t);
  void SetContrast(int8_t);
  void SetSaturation(int8_t);
  void SetAwb(bool);
  void SetAwbGain(bool);
  void SetAwbMode(uint8_t);
  void SetAec2(bool);
  void SetAeLevel(int8_t);
  void SetAecValue(uint16_t);
  void SetGainCtrl(bool);
  void SetAgcGain(uint8_t);
  void SetBpc(bool);
  void SetWpc(bool);
  void SetRawGama(bool);
  void SetHMirror(bool);
  void SetVFlip(bool);
  void SetLensC(bool);
  void SetExposureCtrl(bool);
  void SetCameraFlashEnable(bool);
  void SetCameraFlashTime(uint16_t);
  void SetCameraImageRotation(uint8_t);

  uint8_t GetPhotoQuality();
  uint8_t GetFrameSize();
  uint16_t GetFrameSizeWidth();
  uint16_t GetFrameSizeHeight();
  int8_t GetBrightness();
  int8_t GetContrast();
  int8_t GetSaturation();
  bool GetAwb();
  bool GetAwbGain();
  uint8_t GetAwbMode();
  bool GetAec2();
  int8_t GetAeLevel();
  uint16_t GetAecValue();
  bool GetGainCtrl();
  uint8_t GetAgcGaint();
  bool GetBpc();
  bool GetWpc();
  bool GetRawGama();
  bool GetHMirror();
  bool GetVFlip();
  bool GetLensC();
  bool GetExposureCtrl();
  bool GetCameraFlashEnable();
  uint16_t GetCameraFlashTime();
  uint8_t GetCameraImageRotation();
};

extern Camera SystemCamera; ///< Camera object
extern PhotoRequests PhotoCaptureRequests; ///< /action_capture tickets, served by the photo task

/* EOF */