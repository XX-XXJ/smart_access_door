idf_component_register(
    SRCS
        "main.c"

        "event_log.c"
        "mqtt_client_app.c"
        "local_record_db.c"
        "power_manager.c"

        "wifi_net.c"
        "time_sync.c"

        "ui_oled.c"
        "keypad.c"
        "lock_ctrl.c"
        "alarm.c"
        "auth.c"
        "user_db.c"

        "rfid_mfrc522.c"
        "fingerprint_as608.c"

        "camera_ov2640.c"
        "face_auth_adapter.c"
        "tflm_face_recognition.cpp"
        "face_model_data.cc"

    INCLUDE_DIRS "."
    REQUIRES
        esp_timer
        nvs_flash
        esp_event
        esp_netif
        esp_wifi
        mqtt
        spiffs
        esp32-camera
        esp-tflite-micro
        driver
)