set(COMPONENT_REQUIRES
    driver
    esp_driver_i2c
    esp_http_server
    nvs_flash
    esp_wifi
    esp_event
    esp_netif
    mdns
    esp_timer
    esp_psram
    spi_flash
    littlefs
    esp-dsp
)

if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND COMPONENT_REQUIRES esp_wifi_remote esp_hosted esp_driver_cam esp_driver_isp esp_driver_jpeg espressif__esp_sccb_intf espressif__esp_cam_sensor)
else()
    list(APPEND COMPONENT_REQUIRES esp32-camera)
endif()

idf_component_register(
    SRC_DIRS 
        "."
        "communication"
        "peripherals"
        "wifi"
        "platform_shared"
        "../../submodules/nanopb"
    INCLUDE_DIRS 
        "../include"
        "../../submodules/nanopb"
    REQUIRES
        ${COMPONENT_REQUIRES}
)
