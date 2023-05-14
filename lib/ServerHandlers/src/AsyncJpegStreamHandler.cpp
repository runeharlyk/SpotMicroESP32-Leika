#include <AsyncJpegStreamHandler.h>

static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: %s\r\nContent-Length: %u\r\n\r\n";

static const char * JPG_CONTENT_TYPE = "image/jpeg";

AsyncJpegStreamResponse::AsyncJpegStreamResponse(){
    _callback = nullptr;
    _code = 200;
    _contentLength = 0;
    _contentType = STREAM_CONTENT_TYPE;
    _sendContentLength = false;
    _chunked = true;
    _index = 0;
    _jpg_buf_len = 0;
    _jpg_buf = NULL;
    lastAsyncRequest = 0;
    memset(&_frame, 0, sizeof(camera_frame_t));
}

AsyncJpegStreamResponse::~AsyncJpegStreamResponse(){
    if(_frame.fb){
        if(_frame.fb->format != PIXFORMAT_JPEG){
            free(_jpg_buf);
        }
        esp_camera_fb_return(_frame.fb);
    }
}

/*bool AsyncJpegStreamResponse::_sourceValid() {
    return true;
}*/

size_t AsyncJpegStreamResponse::_fillBuffer(uint8_t *buf, size_t maxLen) {
    size_t ret = _content(buf, maxLen, _index);
    if(ret != RESPONSE_TRY_AGAIN){
        _index += ret;
    }
    return ret;
}

size_t AsyncJpegStreamResponse::_content(uint8_t *buffer, size_t maxLen, size_t index){
    if(!_frame.fb || _frame.index == _jpg_buf_len) {
        if(index && _frame.fb){
            uint64_t end = (uint64_t)micros();
            int fp = (end - lastAsyncRequest) / 1000;
            log_printf("Size: %uKB, Time: %ums (%.1ffps)\n", _jpg_buf_len/1024, fp);
            lastAsyncRequest = end;
            if(_frame.fb->format != PIXFORMAT_JPEG){
                free(_jpg_buf);
            }
            esp_camera_fb_return(_frame.fb);
            _frame.fb = NULL;
            _jpg_buf_len = 0;
            _jpg_buf = NULL;
        }
        if(maxLen < (strlen(STREAM_BOUNDARY) + strlen(STREAM_PART) + strlen(JPG_CONTENT_TYPE) + 8)){
            //log_w("Not enough space for headers");
            return RESPONSE_TRY_AGAIN;
        }
        //get frame
        _frame.index = 0;

        _frame.fb = esp_camera_fb_get();
        if (_frame.fb == NULL) {
            log_e("Camera frame failed");
            return 0;
        }

        if(_frame.fb->format != PIXFORMAT_JPEG){
            unsigned long st = millis();
            bool jpeg_converted = frame2jpg(_frame.fb, 80, &_jpg_buf, &_jpg_buf_len);
            if(!jpeg_converted){
                log_e("JPEG compression failed");
                esp_camera_fb_return(_frame.fb);
                _frame.fb = NULL;
                _jpg_buf_len = 0;
                _jpg_buf = NULL;
                return 0;
            }
            log_i("JPEG: %lums, %uB", millis() - st, _jpg_buf_len);
        } else {
            _jpg_buf_len = _frame.fb->len;
            _jpg_buf = _frame.fb->buf;
        }

        //send boundary
        size_t blen = 0;
        if(index){
            blen = strlen(STREAM_BOUNDARY);
            memcpy(buffer, STREAM_BOUNDARY, blen);
            buffer += blen;
        }
        //send header
        size_t hlen = sprintf((char *)buffer, STREAM_PART, JPG_CONTENT_TYPE, _jpg_buf_len);
        buffer += hlen;
        //send frame
        hlen = maxLen - hlen - blen;
        if(hlen > _jpg_buf_len){
            maxLen -= hlen - _jpg_buf_len;
            hlen = _jpg_buf_len;
        }
        memcpy(buffer, _jpg_buf, hlen);
        _frame.index += hlen;
        return maxLen;
    }

    size_t available = _jpg_buf_len - _frame.index;
    if(maxLen > available){
        maxLen = available;
    }
    memcpy(buffer, _jpg_buf+_frame.index, maxLen);
    _frame.index += maxLen;

    return maxLen;
}


void streamJpg(AsyncWebServerRequest *request){
    AsyncJpegStreamResponse *response = new AsyncJpegStreamResponse();
    if(!response){
        request->send(501);
        return;
    }
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}