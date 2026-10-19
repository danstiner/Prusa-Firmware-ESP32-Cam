/**
   @file stream.h

   @brief stream library

   Inspiration is from git repository https://gist.github.com/me-no-dev/d34fba51a8f059ac559bf62002e61aa3

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "WebServer.h"
#include "mcu_cfg.h"
#include "var.h"
#include "log.h"
#include "camera.h"
#include "photo_requests.h"

class Camera;

class AsyncBufferResponse : public AsyncAbstractResponse {
private:
  uint8_t *_buf;    ///< pointer to frame buffer
  size_t _len;      ///< length of frame buffer
  size_t _index;    ///< index of frame 

public:
  AsyncBufferResponse(uint8_t *, size_t, const char *);
  ~AsyncBufferResponse();
  bool _sourceValid() const;
  virtual size_t _fillBuffer(uint8_t *, size_t) override;
  size_t _content(uint8_t *, size_t, size_t);
};

class AsyncFrameResponse : public AsyncAbstractResponse {
private:
  camera_fb_t *fb;    ///< pointer to frame buffer
  size_t _index;      ///< index of frame 

public:
  AsyncFrameResponse(camera_fb_t *, const char *);
  ~AsyncFrameResponse();
  bool _sourceValid() const;
  virtual size_t _fillBuffer(uint8_t *, size_t) override;
  size_t _content(uint8_t *, size_t, size_t);
};

class AsyncCaptureResponse : public AsyncAbstractResponse {
private:
  PhotoRequests *requests;  ///< ticket source
  uint32_t ticket;          ///< ticket this request waits for
  uint32_t start_ms;        ///< when the request arrived
  bool done;                ///< the photo is done, or the wait timed out
  size_t _index;            ///< bytes of the body sent

public:
  AsyncCaptureResponse(PhotoRequests *, uint32_t);
  bool _sourceValid() const;
  virtual size_t _fillBuffer(uint8_t *, size_t) override;
};

class AsyncJpegStreamResponse : public AsyncAbstractResponse {
private:
  FrameCopy _jpg;             ///< V: this viewer's copy of the frame being sent
  size_t _sent;               ///< bytes of _jpg already handed to TCP
  int64_t _connected_us;      ///< connection time; the first frame must start after it
  size_t _index;              ///< index of frame
  uint64_t lastAsyncRequest;  ///< last async request
  Camera *camera;             ///< pointer to camera
  Logs *log;                  ///< pointer to logs
  bool _ended;                ///< a capture failed; every later call returns 0 so the response ends

public:
  AsyncJpegStreamResponse(Camera *, Logs *, uint8_t);
  ~AsyncJpegStreamResponse();
  bool _sourceValid() const;
  virtual size_t _fillBuffer(uint8_t *, size_t ) override;
  size_t _content(uint8_t *, size_t , size_t );
};

/* EOF */