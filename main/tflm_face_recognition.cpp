#include "tflm_face_recognition.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "face_auth_adapter.h"
#include "face_model_data.h"
#include "app_config.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

static const char *TAG = "TFLM_FACE";

/*
 * Tensor Arena 大小需要根据实际模型调整。
 * 如果 AllocateTensors failed，可以增大该值。
 */
#define TENSOR_ARENA_SIZE           (180 * 1024)

#define MODEL_INPUT_WIDTH           96
#define MODEL_INPUT_HEIGHT          96
#define MODEL_INPUT_CHANNELS        1

static uint8_t *s_tensor_arena = nullptr;

static const tflite::Model *s_model = nullptr;
static tflite::MicroInterpreter *s_interpreter = nullptr;
static TfLiteTensor *s_input = nullptr;
static TfLiteTensor *s_output = nullptr;


/*
 *  图像预处理
 *
 * 当前假设摄像头输出为灰度图。
 * 摄像头配置：
 *      PIXFORMAT_GRAYSCALE
 *      FRAMESIZE_QQVGA
 *
 * 输入模型：
 *      96×96×1 int8
 */
static esp_err_t preprocess_frame_to_input(camera_fb_t *fb)
{
    if (fb == nullptr || s_input == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_input->type != kTfLiteInt8) {
        ESP_LOGE(TAG, "Only int8 input model is supported");
        return ESP_FAIL;
    }

    int src_w = fb->width;
    int src_h = fb->height;

    if (src_w <= 0 || src_h <= 0) {
        return ESP_FAIL;
    }

    int8_t *input_data = s_input->data.int8;

    /*
     * 最近邻缩放：
     * 从摄像头图像缩放到 96×96。
     */
    for (int y = 0; y < MODEL_INPUT_HEIGHT; y++) {
        int src_y = y * src_h / MODEL_INPUT_HEIGHT;

        for (int x = 0; x < MODEL_INPUT_WIDTH; x++) {
            int src_x = x * src_w / MODEL_INPUT_WIDTH;

            uint8_t pixel = fb->buf[src_y * src_w + src_x];

            /*
             * 根据模型输入张量的量化参数转换为 int8。
             */
            float normalized = pixel / 255.0f;

            int32_t quantized =
                (int32_t)(normalized / s_input->params.scale) +
                s_input->params.zero_point;

            if (quantized > 127) {
                quantized = 127;
            }

            if (quantized < -128) {
                quantized = -128;
            }

            input_data[y * MODEL_INPUT_WIDTH + x] = (int8_t)quantized;
        }
    }

    return ESP_OK;
}


/*
 *  解析模型输出
 *
 * 约定：
 *      class 0 = unknown
 *      class 1 = user001 / face_id 1
 *      class 2 = user002 / face_id 2
 *
 * 如果置信度低于阈值，也按 unknown 处理。
 */
static void handle_model_output(void)
{
    if (s_output == nullptr) {
        return;
    }

    if (s_output->type != kTfLiteInt8) {
        ESP_LOGW(TAG, "Unsupported output tensor type");
        return;
    }

    int class_count = 1;

    for (int i = 0; i < s_output->dims->size; i++) {
        class_count *= s_output->dims->data[i];
    }

    int best_index = -1;
    int best_score = -128;

    for (int i = 0; i < class_count; i++) {
        int score = s_output->data.int8[i];

        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }

    float real_score =
        (best_score - s_output->params.zero_point) *
        s_output->params.scale;

    int confidence = (int)(real_score * 100);

    ESP_LOGI(TAG,
             "CNN result: class=%d, confidence=%d",
             best_index,
             confidence);

    if (best_index <= 0 || confidence < FACE_CONFIDENCE_THRESHOLD) {
        face_auth_on_unknown();
        return;
    }

    /*
     * 当前直接将 class_id 映射为 face_id。
     */
    uint16_t face_id = (uint16_t)best_index;

    face_auth_on_recognized(face_id, confidence);
}


/*
 *  初始化 TFLM
 */
esp_err_t tflm_face_recognition_init(void)
{
    s_model = tflite::GetModel(g_face_model_data);

    if (s_model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema version mismatch");
        return ESP_FAIL;
    }

    /*
     * 注册模型需要的算子。
     * 如果你的模型用了其他算子，需要继续添加。
     */
    static tflite::MicroMutableOpResolver<10> resolver;

    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddFullyConnected();
    resolver.AddMaxPool2D();
    resolver.AddAveragePool2D();
    resolver.AddReshape();
    resolver.AddSoftmax();
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddRelu();

    s_tensor_arena = (uint8_t *)heap_caps_malloc(
        TENSOR_ARENA_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (s_tensor_arena == nullptr) {
        /*
         * 如果没有 PSRAM，尝试使用内部 RAM。
         * 但较大模型可能会失败。
         */
        s_tensor_arena = (uint8_t *)heap_caps_malloc(
            TENSOR_ARENA_SIZE,
            MALLOC_CAP_8BIT
        );
    }

    if (s_tensor_arena == nullptr) {
        ESP_LOGE(TAG, "Tensor arena malloc failed");
        return ESP_ERR_NO_MEM;
    }

    static tflite::MicroInterpreter static_interpreter(
        s_model,
        resolver,
        s_tensor_arena,
        TENSOR_ARENA_SIZE
    );

    s_interpreter = &static_interpreter;

    TfLiteStatus allocate_status = s_interpreter->AllocateTensors();

    if (allocate_status != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed");
        return ESP_FAIL;
    }

    s_input = s_interpreter->input(0);
    s_output = s_interpreter->output(0);

    ESP_LOGI(TAG,
             "TFLM initialized, input type=%d, output type=%d",
             s_input->type,
             s_output->type);

    return ESP_OK;
}


/*
 *  执行一次人脸识别
 */
esp_err_t tflm_face_recognition_run_once(void)
{
    if (s_interpreter == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    camera_fb_t *fb = esp_camera_fb_get();

    if (fb == nullptr) {
        ESP_LOGW(TAG, "Camera frame get failed");
        return ESP_FAIL;
    }

    esp_err_t ret = preprocess_frame_to_input(fb);

    esp_camera_fb_return(fb);

    if (ret != ESP_OK) {
        return ret;
    }

    TfLiteStatus invoke_status = s_interpreter->Invoke();

    if (invoke_status != kTfLiteOk) {
        ESP_LOGE(TAG, "TFLM Invoke failed");
        return ESP_FAIL;
    }

    handle_model_output();

    return ESP_OK;
}


/*
 *  人脸识别任务
 */
static void tflm_face_task(void *arg)
{
    while (1) {
        esp_err_t ret = tflm_face_recognition_run_once();

        if (ret != ESP_OK) {
            ESP_LOGW(TAG,
                     "Face recognition run failed: %s",
                     esp_err_to_name(ret));
        }

        /*
         * 不需要每帧都识别，降低功耗和计算负担。
         */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}


esp_err_t tflm_face_recognition_start(void)
{
    BaseType_t ret = xTaskCreate(
        tflm_face_task,
        "tflm_face_task",
        8192,
        NULL,
        3,
        NULL
    );

    if (ret != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TFLM face task started");

    return ESP_OK;
}