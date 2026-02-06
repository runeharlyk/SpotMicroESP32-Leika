#include <peripherals/camera_service.h>
#include <communication/webserver.h>
#include <esp_heap_caps.h>

namespace Camera {

static const char *const TAG = "CameraService";

#if USE_DVP_CAMERA || USE_CSI_CAMERA
static constexpr const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static constexpr const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static constexpr const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
#endif

#if USE_DVP_CAMERA

SemaphoreHandle_t cameraMutex = xSemaphoreCreateMutex();

camera_fb_t *safe_camera_fb_get() {
    camera_fb_t *fb = NULL;
    if (xSemaphoreTakeRecursive(cameraMutex, portMAX_DELAY) == pdTRUE) {
        fb = esp_camera_fb_get();
        xSemaphoreGiveRecursive(cameraMutex);
    }
    return fb;
}

sensor_t *safe_sensor_get() {
    sensor_t *s = NULL;
    if (xSemaphoreTakeRecursive(cameraMutex, portMAX_DELAY) == pdTRUE) {
        s = esp_camera_sensor_get();
    }
    return s;
}

void safe_sensor_return() { xSemaphoreGiveRecursive(cameraMutex); }

CameraService::CameraService()
    : protoEndpoint(CameraSettings_read, CameraSettings_update, this,
                    API_REQUEST_EXTRACTOR(camera_settings, api_CameraSettings),
                    API_RESPONSE_ASSIGNER(camera_settings, api_CameraSettings)),
      _persistence(CameraSettings_read, CameraSettings_update, this, CAMERA_SETTINGS_FILE, api_CameraSettings_fields,
                   api_CameraSettings_size, CameraSettings_defaults()) {
    addUpdateHandler([&](const std::string &originId) { updateCamera(); }, false);
}

esp_err_t CameraService::begin() {
    _persistence.readFromFS();
    camera_config_t camera_config;
    camera_config.ledc_channel = LEDC_CHANNEL_0;
    camera_config.ledc_timer = LEDC_TIMER_0;
    camera_config.pin_d0 = Y2_GPIO_NUM;
    camera_config.pin_d1 = Y3_GPIO_NUM;
    camera_config.pin_d2 = Y4_GPIO_NUM;
    camera_config.pin_d3 = Y5_GPIO_NUM;
    camera_config.pin_d4 = Y6_GPIO_NUM;
    camera_config.pin_d5 = Y7_GPIO_NUM;
    camera_config.pin_d6 = Y8_GPIO_NUM;
    camera_config.pin_d7 = Y9_GPIO_NUM;
    camera_config.pin_xclk = XCLK_GPIO_NUM;
    camera_config.pin_pclk = PCLK_GPIO_NUM;
    camera_config.pin_vsync = VSYNC_GPIO_NUM;
    camera_config.pin_href = HREF_GPIO_NUM;
    camera_config.pin_sccb_sda = SIOD_GPIO_NUM;
    camera_config.pin_sccb_scl = SIOC_GPIO_NUM;
    camera_config.pin_pwdn = PWDN_GPIO_NUM;
    camera_config.pin_reset = RESET_GPIO_NUM;
    camera_config.xclk_freq_hz = 20000000;
    camera_config.pixel_format = PIXFORMAT_JPEG;

    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
        camera_config.frame_size = FRAMESIZE_SVGA;
        camera_config.jpeg_quality = 10;
        camera_config.fb_location = CAMERA_FB_IN_PSRAM;
        camera_config.fb_count = 2;
        camera_config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        camera_config.frame_size = FRAMESIZE_SVGA;
        camera_config.jpeg_quality = 12;
        camera_config.fb_count = 1;
    }

    ESP_LOGI(TAG, "Initializing camera");
    esp_err_t err = esp_camera_init(&camera_config);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "Camera probe successful");
    else
        ESP_LOGE(TAG, "Camera probe failed with error 0x%x", err);

    return err;
}

esp_err_t CameraService::cameraStill(httpd_req_t *request) {
    camera_fb_t *fb = safe_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return WebServer::sendError(request, 500, "Camera capture failed");
    }

    httpd_resp_set_type(request, "image/jpeg");
    httpd_resp_set_hdr(request, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t res = httpd_resp_send(request, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

esp_err_t CameraService::cameraStream(httpd_req_t *request) {
    httpd_resp_set_type(request, _STREAM_CONTENT_TYPE);

    camera_fb_t *fb = NULL;
    char part_buf[64];
    uint8_t *buf = NULL;
    size_t buf_len = 0;
    esp_err_t res = ESP_OK;

    while (res == ESP_OK) {
        fb = safe_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            break;
        }
        if (fb->format != PIXFORMAT_JPEG) {
            if (!frame2jpg(fb, 80, &buf, &buf_len)) {
                esp_camera_fb_return(fb);
                break;
            }
        } else {
            buf_len = fb->len;
            buf = fb->buf;
        }

        size_t hlen = snprintf(part_buf, 64, _STREAM_PART, buf_len);

        res = httpd_resp_send_chunk(request, part_buf, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(request, (const char *)buf, buf_len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(request, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

        esp_camera_fb_return(fb);
        safe_sensor_return();
        buf = NULL;

        vTaskDelay(pdMS_TO_TICKS(30));
    }

    ESP_LOGI(TAG, "Stream ended");
    httpd_resp_send_chunk(request, NULL, 0);
    return ESP_OK;
}

void CameraService::updateCamera() {
    ESP_LOGI("CameraSettings", "Updating camera settings");
    sensor_t *s = safe_sensor_get();
    if (!s) {
        ESP_LOGE("CameraSettings", "Failed to update camera settings");
        safe_sensor_return();
        return;
    }
    s->set_pixformat(s, static_cast<pixformat_t>(state().pixformat));
    s->set_framesize(s, static_cast<framesize_t>(state().framesize));
    s->set_brightness(s, state().brightness);
    s->set_contrast(s, state().contrast);
    s->set_saturation(s, state().saturation);
    s->set_sharpness(s, state().sharpness);
    s->set_denoise(s, state().denoise);
    s->set_gainceiling(s, static_cast<gainceiling_t>(state().gainceiling));
    s->set_quality(s, state().quality);
    s->set_colorbar(s, state().colorbar);
    s->set_awb_gain(s, state().awb_gain);
    s->set_wb_mode(s, state().wb_mode);
    s->set_aec2(s, state().aec2);
    s->set_ae_level(s, state().ae_level);
    s->set_aec_value(s, state().aec_value);
    s->set_agc_gain(s, state().agc_gain);
    s->set_bpc(s, state().bpc);
    s->set_wpc(s, state().wpc);
    s->set_special_effect(s, state().special_effect);
    s->set_raw_gma(s, state().raw_gma);
    s->set_lenc(s, state().lenc);
    s->set_hmirror(s, state().hmirror);
    s->set_vflip(s, state().vflip);
    s->set_dcw(s, state().dcw);
    safe_sensor_return();
}

#elif USE_CSI_CAMERA

}

extern "C" {
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr_types.h"
#include "driver/isp.h"
#include "driver/jpeg_encode.h"
#include "esp_sccb_intf.h"
#include "esp_sccb_i2c.h"
#include "esp_cam_sensor.h"
#include "ov5647.h"
#include "esp_ldo_regulator.h"
}

#include <peripherals/i2c_bus.h>

namespace Camera {

#ifndef MIPI_CSI_HRES
#define MIPI_CSI_HRES 640
#endif
#ifndef MIPI_CSI_VRES
#define MIPI_CSI_VRES 480
#endif
#ifndef MIPI_CSI_LANE_BITRATE_MBPS
#define MIPI_CSI_LANE_BITRATE_MBPS 200
#endif
#ifndef MIPI_CSI_DATA_LANES
#define MIPI_CSI_DATA_LANES 2
#endif
#ifndef CAM_SCCB_FREQ_HZ
#define CAM_SCCB_FREQ_HZ 100000
#endif
#ifndef CAM_SENSOR_ADDR
#define CAM_SENSOR_ADDR 0x36
#endif
#ifndef CAM_XCLK_PIN
#define CAM_XCLK_PIN -1
#endif
#ifndef CAM_XCLK_FREQ_HZ
#define CAM_XCLK_FREQ_HZ 25000000
#endif
#ifndef CAM_RESET_PIN
#define CAM_RESET_PIN -1
#endif
#ifndef CAM_PWDN_PIN
#define CAM_PWDN_PIN -1
#endif
#ifndef CSI_JPEG_QUALITY
#define CSI_JPEG_QUALITY 80
#endif

static SemaphoreHandle_t s_cam_mutex = NULL;
static SemaphoreHandle_t s_frame_done = NULL;
static esp_cam_ctlr_handle_t s_cam_handle = NULL;
static isp_proc_handle_t s_isp_proc = NULL;
static jpeg_encoder_handle_t s_jpeg_enc = NULL;

static uint8_t *s_frame_buf = NULL;
static size_t s_frame_buf_size = 0;
static uint8_t *s_jpeg_buf = NULL;
static size_t s_jpeg_buf_size = 0;
static bool s_cam_initialized = false;

static bool on_trans_finished(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data) {
    if (trans->buffer != s_frame_buf) return false;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_frame_done, &woken);
    return (woken == pdTRUE);
}

static bool capture_and_encode(uint8_t **jpeg_out, size_t *jpeg_len) {
    if (!s_cam_initialized) return false;

    esp_cam_ctlr_trans_t trans = {};
    trans.buffer = s_frame_buf;
    trans.buflen = s_frame_buf_size;

    esp_err_t err = esp_cam_ctlr_receive(s_cam_handle, &trans, 2000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Frame capture failed: %s", esp_err_to_name(err));
        return false;
    }

    if (xSemaphoreTake(s_frame_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "Frame receive timed out");
        return false;
    }

    jpeg_encode_cfg_t enc_cfg = {};
    enc_cfg.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
    enc_cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
    enc_cfg.image_quality = CSI_JPEG_QUALITY;
    enc_cfg.width = MIPI_CSI_HRES;
    enc_cfg.height = MIPI_CSI_VRES;

    uint32_t out_size = 0;
    err = jpeg_encoder_process(s_jpeg_enc, &enc_cfg,
                               s_frame_buf, trans.received_size,
                               s_jpeg_buf, s_jpeg_buf_size, &out_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "JPEG encode failed: %s", esp_err_to_name(err));
        return false;
    }

    *jpeg_out = s_jpeg_buf;
    *jpeg_len = out_size;
    return true;
}

CameraService::CameraService() {
    s_cam_mutex = xSemaphoreCreateMutex();
    s_frame_done = xSemaphoreCreateBinary();
}

esp_err_t CameraService::begin() {
    ESP_LOGI(TAG, "Initializing MIPI-CSI camera for ESP32-P4");

    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id = 3;
    ldo_cfg.voltage_mv = 2500;
    esp_err_t ldo_err = esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy);
    if (ldo_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire MIPI PHY LDO: %s", esp_err_to_name(ldo_err));
        return ldo_err;
    }

    i2c_master_bus_handle_t i2c_bus = I2CBus::instance().busHandle();
    if (!i2c_bus) {
        ESP_LOGE(TAG, "I2C bus not initialized, cannot init camera SCCB");
        return ESP_ERR_INVALID_STATE;
    }

    esp_sccb_io_handle_t sccb_io = NULL;
    sccb_i2c_config_t sccb_cfg = {};
    sccb_cfg.scl_speed_hz = CAM_SCCB_FREQ_HZ;
    sccb_cfg.device_address = CAM_SENSOR_ADDR;
    sccb_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;

    esp_err_t err = sccb_new_i2c_io(i2c_bus, &sccb_cfg, &sccb_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SCCB I/O handle: %s", esp_err_to_name(err));
        return err;
    }

    esp_cam_sensor_config_t cam_sensor_cfg = {};
    cam_sensor_cfg.sccb_handle = sccb_io;
    cam_sensor_cfg.reset_pin = static_cast<gpio_num_t>(CAM_RESET_PIN);
    cam_sensor_cfg.pwdn_pin = static_cast<gpio_num_t>(CAM_PWDN_PIN);
    cam_sensor_cfg.xclk_pin = static_cast<gpio_num_t>(CAM_XCLK_PIN);
    cam_sensor_cfg.xclk_freq_hz = CAM_XCLK_FREQ_HZ;
    cam_sensor_cfg.sensor_port = ESP_CAM_SENSOR_MIPI_CSI;

    esp_cam_sensor_device_t *cam_sensor = ov5647_detect(&cam_sensor_cfg);
    if (!cam_sensor) {
        ESP_LOGE(TAG, "OV5647 detection failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OV5647 camera sensor detected");

    esp_cam_sensor_format_array_t fmt_array = {};
    err = esp_cam_sensor_query_format(cam_sensor, &fmt_array);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to query sensor formats: %s", esp_err_to_name(err));
        return err;
    }

    bool format_set = false;
    for (uint32_t i = 0; i < fmt_array.count; i++) {
        if (fmt_array.format_array[i].width == MIPI_CSI_HRES &&
            fmt_array.format_array[i].height == MIPI_CSI_VRES) {
            err = esp_cam_sensor_set_format(cam_sensor, &fmt_array.format_array[i]);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Sensor format set: %dx%d",
                         MIPI_CSI_HRES, MIPI_CSI_VRES);
                format_set = true;
                break;
            }
        }
    }
    if (!format_set && fmt_array.count > 0) {
        err = esp_cam_sensor_set_format(cam_sensor, &fmt_array.format_array[0]);
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "Using fallback sensor format: %dx%d",
                     fmt_array.format_array[0].width, fmt_array.format_array[0].height);
        }
    }

    int stream_on = 1;
    err = esp_cam_sensor_ioctl(cam_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_on);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sensor stream: %s", esp_err_to_name(err));
        return err;
    }

    esp_isp_processor_cfg_t isp_cfg = {};
    isp_cfg.clk_src = static_cast<isp_clk_src_t>(0);
    isp_cfg.clk_hz = 80 * 1000 * 1000;
    isp_cfg.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
    isp_cfg.input_data_color_type = ISP_COLOR_RAW8;
    isp_cfg.output_data_color_type = ISP_COLOR_RGB565;
    isp_cfg.has_line_start_packet = false;
    isp_cfg.has_line_end_packet = false;
    isp_cfg.h_res = MIPI_CSI_HRES;
    isp_cfg.v_res = MIPI_CSI_VRES;
    isp_cfg.bayer_order = COLOR_RAW_ELEMENT_ORDER_BGGR;

    err = esp_isp_new_processor(&isp_cfg, &s_isp_proc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ISP processor init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_isp_enable(s_isp_proc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ISP enable failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_cam_ctlr_csi_config_t csi_cfg = {};
    csi_cfg.ctlr_id = 0;
    csi_cfg.h_res = MIPI_CSI_HRES;
    csi_cfg.v_res = MIPI_CSI_VRES;
    csi_cfg.lane_bit_rate_mbps = MIPI_CSI_LANE_BITRATE_MBPS;
    csi_cfg.input_data_color_type = CAM_CTLR_COLOR_RAW8;
    csi_cfg.output_data_color_type = CAM_CTLR_COLOR_RGB565;
    csi_cfg.data_lane_num = MIPI_CSI_DATA_LANES;
    csi_cfg.byte_swap_en = false;
    csi_cfg.queue_items = 1;

    err = esp_cam_new_csi_ctlr(&csi_cfg, &s_cam_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI controller init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_cam_ctlr_evt_cbs_t cbs = {};
    cbs.on_trans_finished = on_trans_finished;
    err = esp_cam_ctlr_register_event_callbacks(s_cam_handle, &cbs, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI register callbacks failed: %s", esp_err_to_name(err));
        return err;
    }

    static constexpr size_t CACHE_LINE_SIZE = 64;
    s_frame_buf_size = MIPI_CSI_HRES * MIPI_CSI_VRES * 2;
    s_frame_buf_size = (s_frame_buf_size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
    s_frame_buf = (uint8_t *)heap_caps_aligned_alloc(CACHE_LINE_SIZE, s_frame_buf_size, MALLOC_CAP_SPIRAM);
    if (!s_frame_buf) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer (%d bytes)", (int)s_frame_buf_size);
        return ESP_ERR_NO_MEM;
    }

    jpeg_encode_memory_alloc_cfg_t jpeg_mem_cfg = {};
    jpeg_mem_cfg.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER;
    s_jpeg_buf = (uint8_t *)jpeg_alloc_encoder_mem(
        MIPI_CSI_HRES * MIPI_CSI_VRES, &jpeg_mem_cfg, &s_jpeg_buf_size);
    if (!s_jpeg_buf) {
        ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
        return ESP_ERR_NO_MEM;
    }

    jpeg_encode_engine_cfg_t enc_eng_cfg = {};
    enc_eng_cfg.timeout_ms = 40;

    err = jpeg_new_encoder_engine(&enc_eng_cfg, &s_jpeg_enc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "JPEG encoder init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_cam_ctlr_enable(s_cam_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI controller enable failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_cam_ctlr_start(s_cam_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI controller start failed: %s", esp_err_to_name(err));
        return err;
    }

    s_cam_initialized = true;
    ESP_LOGI(TAG, "MIPI-CSI camera initialized (%dx%d, %d-lane, %d Mbps)",
             MIPI_CSI_HRES, MIPI_CSI_VRES, MIPI_CSI_DATA_LANES, MIPI_CSI_LANE_BITRATE_MBPS);
    return ESP_OK;
}

esp_err_t CameraService::cameraStill(httpd_req_t *request) {
    if (!s_cam_initialized) {
        return WebServer::sendError(request, 503, "Camera not initialized");
    }

    xSemaphoreTake(s_cam_mutex, portMAX_DELAY);

    uint8_t *jpeg_buf = NULL;
    size_t jpeg_len = 0;

    if (!capture_and_encode(&jpeg_buf, &jpeg_len)) {
        xSemaphoreGive(s_cam_mutex);
        return WebServer::sendError(request, 500, "Camera capture failed");
    }

    httpd_resp_set_type(request, "image/jpeg");
    httpd_resp_set_hdr(request, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t res = httpd_resp_send(request, (const char *)jpeg_buf, jpeg_len);

    xSemaphoreGive(s_cam_mutex);
    return res;
}

esp_err_t CameraService::cameraStream(httpd_req_t *request) {
    if (!s_cam_initialized) {
        return WebServer::sendError(request, 503, "Camera not initialized");
    }

    httpd_resp_set_type(request, _STREAM_CONTENT_TYPE);

    char part_buf[64];
    esp_err_t res = ESP_OK;

    while (res == ESP_OK) {
        xSemaphoreTake(s_cam_mutex, portMAX_DELAY);

        uint8_t *jpeg_buf = NULL;
        size_t jpeg_len = 0;

        if (!capture_and_encode(&jpeg_buf, &jpeg_len)) {
            xSemaphoreGive(s_cam_mutex);
            break;
        }

        size_t hlen = snprintf(part_buf, 64, _STREAM_PART, (unsigned int)jpeg_len);
        res = httpd_resp_send_chunk(request, part_buf, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(request, (const char *)jpeg_buf, jpeg_len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(request, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

        xSemaphoreGive(s_cam_mutex);

        vTaskDelay(pdMS_TO_TICKS(30));
    }

    ESP_LOGI(TAG, "Stream ended");
    httpd_resp_send_chunk(request, NULL, 0);
    return ESP_OK;
}

#else

CameraService::CameraService() {}
esp_err_t CameraService::begin() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t CameraService::cameraStill(httpd_req_t *request) {
    return WebServer::sendError(request, 501, "Camera not supported on this platform");
}
esp_err_t CameraService::cameraStream(httpd_req_t *request) {
    return WebServer::sendError(request, 501, "Camera not supported on this platform");
}

#endif

} // namespace Camera
