#include "camera_http_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CAM_HTTP";
static httpd_handle_t s_server;
// MIME 多部分边界字符串（MJPEG 流中分隔每帧图片）
#define PART_BOUNDARY "123456789000000000000987654321"

/**
 *  首页处理函数（GET /）
 *  返回一个简单的 HTML 页面，内嵌视频流和抓图链接
 */
static esp_err_t index_handler(httpd_req_t *req)
{
    const char *html =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<title>ESP32-S3 OV2640</title>"
        "<style>body{font-family:Arial;text-align:center;background:#f5f5f5;}img{max-width:96%;border:1px solid #ccc;}a{margin:10px;display:inline-block;}</style>"
        "</head><body><h2>ESP32-S3 OV2640 Camera</h2>"
        "<p><a href='/capture.jpg'>capture.jpg</a><a href='/stream'>stream</a></p>"
        "<img src='/stream'></body></html>";
    httpd_resp_set_type(req, "text/html");
    // 发送 HTML，HTTPD_RESP_USE_STRLEN 表示根据字符串长度自动计算 Content-Length
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

/**
 *  单张抓图处理函数（GET /capture.jpg）
 *  从摄像头获取一帧 JPEG 数据，直接返回给客户端
 */

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();// 获取一帧图像
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    // 确保是 JPEG 格式，否则无法直接返回
    if (fb->format != PIXFORMAT_JPEG) {
        esp_camera_fb_return(fb);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t ret = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);// 归还帧缓冲，防止内存泄漏
    return ret;
}
/**
 * MJPEG 视频流处理函数（GET /stream）
 * 使用 multipart/x-mixed-replace 协议连续发送 JPEG 帧，
 * 浏览器会不断替换画面，实现“伪视频”效果
 */
static esp_err_t stream_handler(httpd_req_t *req)
{
    char part_buf[128]; // 用于存放每帧的 MIME 头部
    esp_err_t ret = ESP_OK;

    // 设置响应类型为 multipart，并指定边界字符串
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" PART_BOUNDARY);

    while (1) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ret = ESP_FAIL; break;
        }
        if (fb->format != PIXFORMAT_JPEG) {
            esp_camera_fb_return(fb);
            ret = ESP_FAIL;
            break;
        }

        // 构造 multipart 头部，格式：
        // --BOUNDARY\r\nContent-Type: image/jpeg\r\nContent-Length: xxx\r\n\r\n

        size_t hlen = snprintf(part_buf, sizeof(part_buf),
                               "\r\n--" PART_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                               (unsigned)fb->len);
        // 分块发送：先发送头部，再发送 JPEG 数据
        ret = httpd_resp_send_chunk(req, part_buf, hlen);
        if (ret == ESP_OK) {
            ret = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        esp_camera_fb_return(fb); // 务必归还帧缓冲

        if (ret != ESP_OK) break;// 发送失败（如客户端断开）则退出循环
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return ret;
}

/**
 * 启动相机 HTTP 服务器
 * 注册三个 URI 处理函数，监听 80 端口
 */

esp_err_t camera_http_server_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;// HTTP 标准端口
    config.stack_size = 8192;// 增大任务栈（处理视频流需要更多栈空间）
    config.ctrl_port = 32768; // 控制端口（内部使用，用于安全停止等）

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) return ret;

    // 注册三个路由
    httpd_uri_t u0 = {.uri="/", .method=HTTP_GET, .handler=index_handler};
    httpd_uri_t u1 = {.uri="/capture.jpg", .method=HTTP_GET, .handler=capture_handler};
    httpd_uri_t u2 = {.uri="/stream", .method=HTTP_GET, .handler=stream_handler};
    httpd_register_uri_handler(s_server, &u0);
    httpd_register_uri_handler(s_server, &u1);
    httpd_register_uri_handler(s_server, &u2);
    ESP_LOGI(TAG, "Camera HTTP server started: / /capture.jpg /stream");
    return ESP_OK;
}
