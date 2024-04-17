#include "who_camera.h"
#include "who_motion_detection.hpp"
#include "app_wifi.h"
#include "app_httpd.hpp"
#include "app_mdns.h"

#include "esp_log.h"
#include "dl_image.hpp"

#undef EPS
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/core/types_c.h"
#define EPS 192

static const char *TAG = "openCV";

static QueueHandle_t xQueueCameraFrame = NULL;
static QueueHandle_t xQueueOpenCVFrame = NULL;

static QueueHandle_t xQueueFrameI = NULL;
static QueueHandle_t xQueueEvent = NULL;
static QueueHandle_t xQueueFrameO = NULL;
static QueueHandle_t xQueueResult = NULL;

static bool gEvent = true;

using namespace cv;
using namespace std;

long sysconf(int name) {
    ESP_LOGI(TAG, "sysconf name: %d", name);
    return 0;
}

static void task_process_handler(void *arg)
{
    camera_fb_t *frame = NULL;

    while (true)
    {
        if (gEvent)
        {
            if (xQueueReceive(xQueueFrameI, &(frame), portMAX_DELAY))
            {
                Mat inputImage = Mat(frame->height, frame->width, CV_8UC1, frame->buf);
                ESP_LOGI(TAG, "format: %d, : len: %d, w*h: %d ", frame->format, frame->len, frame->height * frame->width);

                // blur image before trashing to get better results
                Mat greyImage = Mat::zeros( inputImage.size(), CV_8UC1);
                blur(inputImage, greyImage, Size(3, 3));

                // tresh image to black-white 
                adaptiveThreshold(greyImage, greyImage, 255, ADAPTIVE_THRESH_MEAN_C, THRESH_BINARY_INV, 5, 5);

                // find contours in black-white image
                vector<vector<Point> > contours;
                findContours(greyImage, contours, RETR_EXTERNAL, CHAIN_APPROX_NONE);
                
                //draw all countours - thickness = 1
                //drawContours(inputImage, contours, -1, Scalar(255,255,255));

                for (int i = 0; i < contours.size(); i++)
                {
                    double area = contourArea(contours[i]);
                    double area_min = 300;
                    //draw  contours bigger than area_min on original frame
                    if (area > area_min)
                    {
                        drawContours(inputImage, contours, i, Scalar(255,255,255), 5);
                    }
                } 
            }
            if (xQueueFrameO)
                xQueueSend(xQueueFrameO, &frame, portMAX_DELAY);
        }
    }
}

static void task_event_handler(void *arg)
{
    while (true)
    {
        xQueueReceive(xQueueEvent, &(gEvent), portMAX_DELAY);
    }
}

void register_opencv(QueueHandle_t frame_i, QueueHandle_t event,
                     QueueHandle_t result, QueueHandle_t frame_o)
{
    xQueueFrameI = frame_i;
    xQueueFrameO = frame_o;
    xQueueEvent = event;
    xQueueResult = result;
    xTaskCreatePinnedToCore(task_process_handler, TAG, 4 * 1024, NULL, 5, NULL, 1);
    if (xQueueEvent)
        xTaskCreatePinnedToCore(task_event_handler, TAG, 4 * 1024, NULL, 5, NULL, 1);
}

extern "C" void app_main()
{
    app_wifi_main();

    xQueueCameraFrame = xQueueCreate(2, sizeof(camera_fb_t *));
    xQueueOpenCVFrame = xQueueCreate(2, sizeof(camera_fb_t *));

    register_camera(PIXFORMAT_GRAYSCALE, FRAMESIZE_VGA, 1, xQueueCameraFrame);
    register_opencv(xQueueCameraFrame, NULL, NULL, xQueueOpenCVFrame);
    register_httpd(xQueueOpenCVFrame, NULL, true);
}