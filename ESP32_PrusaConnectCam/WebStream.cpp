/**
   @file stream.cpp

   @brief stream library.

   Inspiration is from git repository https://gist.github.com/me-no-dev/d34fba51a8f059ac559bf62002e61aa3

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/
#include "WebStream.h"

#define PART_BOUNDARY "123456789000000000000987654321"                                          ///< Must be unique for each stream
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;   ///< content type for stream
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";                             ///< boundary for stream
static const char *STREAM_PART = "Content-Type: %s\r\nContent-Length: %u\r\n\r\n";              ///< part for stream
static const char *JPG_CONTENT_TYPE = "image/jpeg";                                           ///< content type for jpg
static const char *CAPTURE_BODY = "Take Photo";                                                 ///< body of the /action_capture answer

/**
 * @brief Construct a new Async Buffer Response:: Async Buffer Response object
 * 
 * @param buf 
 * @param len 
 * @param contentType 
 */
AsyncBufferResponse::AsyncBufferResponse(uint8_t *buf, size_t len, const char *contentType) {
  _buf = buf;
  _len = len;
  _callback = nullptr;
  _code = 200;
  _contentLength = _len;
  _contentType = contentType;
  _index = 0;
}

/**
 * @brief Destroy the Async Buffer Response:: Async Buffer Response object
 * 
 */
AsyncBufferResponse::~AsyncBufferResponse() {
  if (_buf != nullptr) {
    free(_buf);
  }
}

/**
 * @brief Check if source is valid
 * 
 * @return bool
 */
bool AsyncBufferResponse::_sourceValid() const {
  return _buf != nullptr;
}

/**
 * @brief Fill buffer
 * 
 * @param buf 
 * @param maxLen 
 * @return size_t 
 */
size_t AsyncBufferResponse::_fillBuffer(uint8_t *buf, size_t maxLen) {
  size_t ret = _content(buf, maxLen, _index);
  if (ret != RESPONSE_TRY_AGAIN) {
    _index += ret;
  }
  return ret;
}

/**
 * @brief Content
 * 
 * @param buffer 
 * @param maxLen 
 * @param index 
 * @return size_t 
 */
size_t AsyncBufferResponse::_content(uint8_t *buffer, size_t maxLen, size_t index) {
  memcpy(buffer, _buf + index, maxLen);
  if ((index + maxLen) == _len) {
    free(_buf);
    _buf = nullptr;
  }
  return maxLen;
}

/**
 * @brief Construct a new Async Frame Response:: Async Frame Response object
 * 
 * @param frame 
 * @param contentType 
 */
AsyncFrameResponse::AsyncFrameResponse(camera_fb_t *frame, const char *contentType) {
  _callback = nullptr;
  _code = 200;
  _contentLength = frame->len;
  _contentType = contentType;
  _index = 0;
  fb = frame;
}

/**
 * @brief Destroy the Async Frame Response:: Async Frame Response object
 * 
 */
AsyncFrameResponse::~AsyncFrameResponse() {
  if (fb != nullptr) {
    esp_camera_fb_return(fb);
  }
}

/**
 * @brief Check if source is valid
 * 
 * @return bool
 */
bool AsyncFrameResponse::_sourceValid() const {
  return fb != nullptr;
}

/**
 * @brief Fill buffer
 * 
 * @param buf 
 * @param maxLen 
 * @return size_t 
 */
size_t AsyncFrameResponse::_fillBuffer(uint8_t *buf, size_t maxLen) {
  size_t ret = _content(buf, maxLen, _index);
  if (ret != RESPONSE_TRY_AGAIN) {
    _index += ret;
  }
  return ret;
}

/**
 * @brief Content
 * 
 * @param buffer 
 * @param maxLen 
 * @param index 
 * @return size_t 
 */
size_t AsyncFrameResponse::_content(uint8_t *buffer, size_t maxLen, size_t index) {
  memcpy(buffer, fb->buf + index, maxLen);
  if ((index + maxLen) == fb->len) {
    esp_camera_fb_return(fb);
    fb = nullptr;
  }
  return maxLen;
}

/**
 * @brief Construct the /action_capture answer: it waits, without blocking the web server, for the photo task to serve the ticket
 *
 * @param i_requests - ticket pair
 * @param i_ticket - this request's ticket
 */
AsyncCaptureResponse::AsyncCaptureResponse(PhotoRequests *i_requests, uint32_t i_ticket) {
  _callback = nullptr;
  _code = 200;
  _contentType = "text/plain";
  _contentLength = strlen(CAPTURE_BODY);
  _sendContentLength = true;
  _chunked = false;
  requests = i_requests;
  ticket = i_ticket;
  start_ms = millis();
  done = false;
  _index = 0;
}

/**
 * @brief Check if source is valid
 *
 * @return bool
 */
bool AsyncCaptureResponse::_sourceValid() const {
  return true;
}

/**
 * @brief Fill buffer: "try again" until the photo is done or PHOTO_REQUEST_TIMEOUT passed; the library retries on its 500 ms poll
 *
 * @param buf
 * @param maxLen
 * @return size_t
 */
size_t AsyncCaptureResponse::_fillBuffer(uint8_t *buf, size_t maxLen) {
  if (false == done) {
    if ((false == requests->Done(ticket)) && ((millis() - start_ms) < PHOTO_REQUEST_TIMEOUT)) {
      return RESPONSE_TRY_AGAIN;
    }
    done = true;
  }

  size_t len = _contentLength - _index;
  if (len > maxLen) {
    len = maxLen;
  }
  memcpy(buf, CAPTURE_BODY + _index, len);
  _index += len;
  return len;
}

/**
 * @brief Construct a new Async Jpeg Stream Response:: Async Jpeg Stream Response object
 * 
 * @param i_cam 
 * @param i_log 
 * @param i_http_version - request->version(): 1 for HTTP/1.1, 0 for HTTP/1.0
 */
AsyncJpegStreamResponse::AsyncJpegStreamResponse(Camera *i_cam, Logs *i_log, uint8_t i_http_version) {
  _callback = nullptr;
  _code = 200;
  _contentLength = 0;
  _contentType = STREAM_CONTENT_TYPE;
  _sendContentLength = false;
  /* HTTP/1.0 clients cannot de-chunk; their body ends when the connection closes */
  _chunked = (1 == i_http_version);
  _index = 0;
  lastAsyncRequest = 0;
  _ended = false;
  memset(&_frame, 0, sizeof(camera_frame_t));
  camera = i_cam;
  log = i_log;
  camera->SetStreamStatus(true);
}

/**
 * @brief Destroy the Async Jpeg Stream Response:: Async Jpeg Stream Response object
 * 
 */
AsyncJpegStreamResponse::~AsyncJpegStreamResponse() {
  camera->SetStreamStatus(false);
  camera->CaptureReturnFrameBuffer();
}

/**
 * @brief Check if source is valid
 * 
 * @return bool
 */
bool AsyncJpegStreamResponse::_sourceValid() const {
  return true;
}

/**
 * @brief Fill buffer
 * 
 * @param buf 
 * @param maxLen 
 * @return size_t 
 */
size_t AsyncJpegStreamResponse::_fillBuffer(uint8_t *buf, size_t maxLen) {
  size_t ret = _content(buf, maxLen, _index);
  if (ret != RESPONSE_TRY_AGAIN) {
    _index += ret;
  }
  return ret;
}

/**
 * @brief Content - get picture from camera and send it to client
 * 
 * @param buffer 
 * @param maxLen 
 * @param index 
 * @return size_t 
 */
size_t AsyncJpegStreamResponse::_content(uint8_t *buffer, size_t maxLen, size_t index) {
  /* on HTTP/1.0 the first 0 does not end the response when the head goes out in the same call */
  if (true == _ended) {
    return 0;
  }

  if (!_frame.fb || _frame.index == _frame.fb->len) {
    delay(1);

    if (index && _frame.fb) {
      uint64_t end = (uint64_t)micros();
      int fp = (end - lastAsyncRequest) / 1000;
      float fps = 1000.0 / fp;
      char buf[50] = { '\0' };
      camera->StreamSetFrameSize(_frame.fb->len / 1024);
      camera->StreamSetFrameFps(fps);
      ////sprintf(buf, "Size: %uKB, Time: %ums (%.1f fps)", _frame.fb->len / 1024, fp, fps);
      sprintf(buf, "Size: %uKB, FPS: %.1f", _frame.fb->len / 1024, fps);
      Serial.println(buf);
      lastAsyncRequest = end;
      camera->CaptureReturnFrameBuffer();
      _frame.fb = NULL;
    }

    /* check space for headers */
    if (maxLen < (strlen(STREAM_BOUNDARY) + strlen(STREAM_PART) + strlen(JPG_CONTENT_TYPE) + 8)) {
      log->AddEvent(LogLevel_Error, F("Stream Not space for headers"));
      return RESPONSE_TRY_AGAIN;
    }

    /* get frame */
    _frame.index = 0;
    if (false == camera->CaptureStream(&_dframe)) {
      log->AddEvent(LogLevel_Error, F("Stream capture frame failed"));
      _frame.fb = NULL;
      _ended = true;
      return 0;
    }
    _frame.fb = &_dframe;

    /* send boundary, also before the first part */
    size_t blen = strlen(STREAM_BOUNDARY);
    memcpy(buffer, STREAM_BOUNDARY, blen);
    buffer += blen;

    /* send header */
    size_t hlen = sprintf((char *)buffer, STREAM_PART, JPG_CONTENT_TYPE, _frame.fb->len);
    buffer += hlen;

    /* send frame */
    hlen = maxLen - hlen - blen;
    if (hlen > _frame.fb->len) {
      maxLen -= hlen - _frame.fb->len;
      hlen = _frame.fb->len;
    }

    memcpy(buffer, _frame.fb->buf, hlen);
    _frame.index += hlen;
    delay(1);
    return maxLen;
  }

  /* check next send data */
  size_t available = _frame.fb->len - _frame.index;
  if (maxLen > available) {
    maxLen = available;
  }

  delay(1);
  memcpy(buffer, _frame.fb->buf + _frame.index, maxLen);
  _frame.index += maxLen;
  return maxLen;
}

/* EOF */