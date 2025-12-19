/*****************************************************
 *  FreeRTOS FINAL (5 Tasks) - Frame Pool + Pointer Queue Version
 *
 *  목표:
 *   - 5초마다 자동 캡처 트리거
 *   - UART0(JPEG 수신) → JPEG 디코드 → CNN 추론 → SPI 전송
 *
 *  핵심 변경:
 *   - 전역 버퍼 공유 방식 제거
 *   - frame_pool에 프레임 데이터를 저장하고,
 *     Queue로는 frame_t* (주소)만 전달 (소유권 전달)
 *
 *  Queue 흐름:
 *   freeFrameQ  -> (TaskUartRx) -> rx2decQ
 *   rx2decQ     -> (TaskJpegDecode) -> dec2cnnQ
 *   dec2cnnQ    -> (TaskCnnInference) -> cnn2spiQ
 *   cnn2spiQ    -> (TaskSpiTx) -> freeFrameQ (반환)
 *****************************************************/

#include "xuartps.h"
#include "xparameters.h"
#include "xil_printf.h"
#include <string.h>
#include "tjpgd.h"
#include "xspips.h"
#include "xil_cache.h"
#include "xgpiops.h"
#include <math.h>

/* ================= FreeRTOS ================= */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

/* ================= CNN weights ================= */
#include "conv2d_weights.h"
#include "conv2d_1_weights.h"
#include "conv2d_2_weights.h"
#include "dense_weights.h"
#include "dense_1_weights.h"
#include "xtime_l.h"

volatile u64 g_frame_start = 0;

static inline u64 now_ticks(void)
{
    XTime t;
    XTime_GetTime(&t);
    return (u64)t;
}

static inline double ticks_to_ms(u64 dt)
{
    return (double)dt * 1e3 / (double)COUNTS_PER_SECOND;
}
/* ================================================= */
#define CONV2D_OUT_CH 16
#define CONV2D_1_OUT_CH 32
#define CONV2D_2_OUT_CH 64
#define DENSE_IN_DIM 1024
#define DENSE_OUT_DIM 64
#define DENSE_1_IN_DIM 64
#define DENSE_1_OUT_DIM 3

extern XUartPs_Config XUartPs_ConfigTable[];

/* ---------------- GPIO CS ---------------- */
#define GPIO_DEVICE_ID   XPAR_XGPIOPS_0_DEVICE_ID
#define CS_MIO_PIN       9

/* ---------------- UART instances ---------------- */
XUartPs Uart0; // Arduino (PS UART0)
XUartPs Uart1; // PuTTY   (PS UART1)

/* ---------------- Image config ---------------- */
#define RAW_WIDTH   320
#define RAW_HEIGHT  240
#define ORIG_PATCH_SIZE 80
#define TARGET_SIZE 32
#define NUM_PATCH_X (RAW_WIDTH / ORIG_PATCH_SIZE)  // 4
#define NUM_PATCH_Y (RAW_HEIGHT / ORIG_PATCH_SIZE) // 3

/* ---------------- JPEG buffer size ---------------- */
#define JPEG_MAX_SIZE (512 * 1024)

/* ---------------- TJPGD work buffer ---------------- */
#define WORK_BUF_SIZE 3100
static u8 work_buffer[WORK_BUF_SIZE];

/* =================================================
 * CNN intermediate buffers (단일 CNN Task만 쓰므로 전역 유지 가능)
 * ================================================= */
static float input_fmap[TARGET_SIZE * TARGET_SIZE];
static float conv1_out[32 * 32 * CONV2D_OUT_CH];
static float pool1_out[16 * 16 * CONV2D_OUT_CH];
static float conv2_out[16 * 16 * CONV2D_1_OUT_CH];
static float pool2_out[8 * 8 * CONV2D_1_OUT_CH];
static float conv3_out[8 * 8 * CONV2D_2_OUT_CH];
static float pool3_out[4 * 4 * CONV2D_2_OUT_CH];
static float dense1_out[DENSE_OUT_DIM];
static float final_output[DENSE_1_OUT_DIM];
static int g_first_frame_id = -1;
static int g_prefetch_valid = 0;
static u8  g_prefetch_byte  = 0;

/* =================================================
 * SPI config
 * ================================================= */
#define SPI_DEVICE_ID        XPAR_PS7_SPI_1_DEVICE_ID
#define TRANSFER_LENGTH      8
#define SPI_CLK_PRESCALER    XSPIPS_CLK_PRESCALE_256

XSpiPs SpiInstance;
u8 SpiSendBuffer[TRANSFER_LENGTH];
u8 SpiRecvBuffer[TRANSFER_LENGTH];

/* =================================================
 * Frame Pool + Pointer Queues
 * ================================================= */
#define FRAME_POOL_SIZE 2

typedef struct {
   int frame_id;
    u8  jpeg_buf[JPEG_MAX_SIZE];
    int jpeg_len;
    int read_pos;

    u8  raw[RAW_HEIGHT][RAW_WIDTH][3];

    u8  spi_result[TRANSFER_LENGTH];
} frame_t;

static frame_t frame_pool[FRAME_POOL_SIZE];
static int g_frame_counter = 0;

static QueueHandle_t freeFrameQ;
static QueueHandle_t rx2decQ;
static QueueHandle_t dec2cnnQ;
static QueueHandle_t cnn2spiQ;

/* =================================================
 * Semaphore / Mutex
 * ================================================= */
static SemaphoreHandle_t cmdSemaphore; // 5초 트리거 이벤트
static SemaphoreHandle_t spiMutex;

/* (선택) UART1 출력 섞임 방지용 */
#define USE_UART_MUTEX 0
#if USE_UART_MUTEX
static SemaphoreHandle_t uartMutex;
#endif

/* =================================================
 * Task prototypes
 * ================================================= */
static void TaskCmd(void* arg);
static void TaskUartRx(void* arg);
static void TaskJpegDecode(void* arg);
static void TaskCnnInference(void* arg);
static void TaskSpiTx(void* arg);

/* =================================================
 * Function prototypes
 * ================================================= */
void uart0_init(void);
void uart1_init(void);
void uart1_print(const char* msg);

static void CsGpioInit(void);
int  SpiPsMasterInit(void);
int  SpiPsTransfer(u8* tx_buf);

int  uart0_recv_image(u8* buf, int max_len, TickType_t timeout_ticks);

/* 프레임 기반으로 변경된 JPEG decode / patch 처리 */
int  jpeg_decode_to_raw_frame(frame_t* f);
void process_image_patch_frame(frame_t* f, int patch_x_idx, int patch_y_idx, u8* out_patch);

int  cnn_inference(u8* input_patch, int patch_x, int patch_y);
void dense_softmax(const float *input_data, int in_dim,
                   float *output_data, int out_dim,
                   const float *weights, const float *biases);

/* =================================================
 * UART init / print
 * ================================================= */
void uart0_init(void)
{
    XUartPs_Config* Config = &XUartPs_ConfigTable[0];
    XUartPs_CfgInitialize(&Uart0, Config, Config->BaseAddress);
    XUartPs_SetBaudRate(&Uart0, 256000);
}

void uart1_init(void)
{
    XUartPs_Config* Config = &XUartPs_ConfigTable[1];
    XUartPs_CfgInitialize(&Uart1, Config, Config->BaseAddress);
    XUartPs_SetBaudRate(&Uart1, 115200);
}

void uart1_print(const char* msg)
{
#if USE_UART_MUTEX
    xSemaphoreTake(uartMutex, portMAX_DELAY);
#endif
    XUartPs_Send(&Uart1, (u8*)msg, strlen(msg));
    XUartPs_Send(&Uart1, (u8*)"\r\n", 2);
#if USE_UART_MUTEX
    xSemaphoreGive(uartMutex);
#endif
}

/* =================================================
 * main()
 * ================================================= */
int main(void)
{
    uart0_init();
    uart1_init();

    CsGpioInit();
    if (SpiPsMasterInit() != XST_SUCCESS) {
        uart1_print("FATAL: SPI INIT FAILED");
        while (1);
    }

    uart1_print("=== FreeRTOS FINAL (Frame Pool + Pointer Queue) START ===");

    /* cmdSemaphore: 주기 이벤트 (단순 binary로 유지) */
    cmdSemaphore = xSemaphoreCreateBinary();
    if (cmdSemaphore == NULL) {
        uart1_print("cmdSemaphore create failed");
        while (1);
    }

    spiMutex = xSemaphoreCreateMutex();
    if (spiMutex == NULL) {
        uart1_print("spiMutex create failed");
        while (1);
    }

#if USE_UART_MUTEX
    uartMutex = xSemaphoreCreateMutex();
    if (uartMutex == NULL) {
        uart1_print("uartMutex create failed");
        while (1);
    }
#endif

    /* Pointer Queues */
    freeFrameQ = xQueueCreate(FRAME_POOL_SIZE, sizeof(frame_t*));
    rx2decQ    = xQueueCreate(FRAME_POOL_SIZE, sizeof(frame_t*));
    dec2cnnQ   = xQueueCreate(FRAME_POOL_SIZE, sizeof(frame_t*));
    cnn2spiQ   = xQueueCreate(FRAME_POOL_SIZE, sizeof(frame_t*));

    if (!freeFrameQ || !rx2decQ || !dec2cnnQ || !cnn2spiQ) {
        uart1_print("Queue create failed");
        while (1);
    }

    /* frame_pool을 freeFrameQ에 채워 넣기 */
    for (int i = 0; i < FRAME_POOL_SIZE; i++) {
        frame_t* f = &frame_pool[i];
        xQueueSend(freeFrameQ, &f, 0);
    }

    /* Task 생성 (5 Tasks 유지) */
    xTaskCreate(TaskCmd,         "TaskCmd", 1024, NULL, 5, NULL);
    xTaskCreate(TaskUartRx,      "TaskUartRx", 4096, NULL, 4, NULL);
    xTaskCreate(TaskJpegDecode,  "TaskJpegDecode", 4096, NULL, 3, NULL);
    xTaskCreate(TaskSpiTx,       "TaskSpiTx", 2048, NULL, 3, NULL);
    xTaskCreate(TaskCnnInference,"TaskCNN", 4096, NULL, 2, NULL);

    vTaskStartScheduler();
    while (1);
}

/* =================================================
 * TaskCmd: 5초마다 캡처 이벤트 발생
 * ================================================= */
static void TaskCmd(void* arg)
{
    TickType_t last = xTaskGetTickCount();
    while (1)
    {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
//        uart1_print("[TaskCmd] Trigger");
        xSemaphoreGive(cmdSemaphore);
    }
}

/* =================================================
 * TaskUartRx: freeFrameQ에서 frame을 받아 JPEG 수신 후 rx2decQ로 전달
 * ================================================= */
static void TaskUartRx(void* arg)
{
    while (1) {
        xSemaphoreTake(cmdSemaphore, portMAX_DELAY);
        frame_t* f = NULL;
        xQueueReceive(freeFrameQ, &f, portMAX_DELAY);
        f->frame_id = g_frame_counter++;

        // 아두이노 캡처 명령 전송
        u8 capture_cmd = 0x10;
        XUartPs_Send(&Uart0, &capture_cmd, 1);

        u8 first_byte = 0;
        while (XUartPs_Recv(&Uart0, &first_byte, 1) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1)); // 데이터 올 때까지 대기
        }

        if (first_byte == 0x53) { // 'S' (SKIP) 신호를 받았다면
            uart1_print("[TaskUartRx] SKIP - No change detected");
            xQueueSend(freeFrameQ, &f, portMAX_DELAY); // 프레임 반환
            continue;
        }

        g_prefetch_valid = 1;
        g_prefetch_byte = first_byte;

        int len = uart0_recv_image(f->jpeg_buf, JPEG_MAX_SIZE, pdMS_TO_TICKS(3000));
        if (len <= 0) {
            xQueueSend(freeFrameQ, &f, portMAX_DELAY);
            continue;
        }

        f->jpeg_len = len;
        f->read_pos = 0;
        xQueueSend(rx2decQ, &f, portMAX_DELAY);
    }
}

/* =================================================
 * TaskJpegDecode: rx2decQ에서 frame 받아 디코딩 후 dec2cnnQ로 전달
 * ================================================= */
static void TaskJpegDecode(void* arg)
{
    while (1)
    {
        frame_t* f = NULL;
        xQueueReceive(rx2decQ, &f, portMAX_DELAY);

        char buf[64];
        snprintf(buf, sizeof(buf),
                 "[TaskJpegDecode] frame=%d",
                 f->frame_id);
        uart1_print(buf);

        if (jpeg_decode_to_raw_frame(f) != JDR_OK) {
            uart1_print("[TaskJpegDecode] Decode failed -> drop frame");
            /* 실패 시 프레임 반환 */
            xQueueSend(freeFrameQ, &f, portMAX_DELAY);
            continue;
        }

//        uart1_print("[TaskJpegDecode] Decode OK -> dec2cnnQ");
        xQueueSend(dec2cnnQ, &f, portMAX_DELAY);
    }
}

/* =================================================
 * TaskCnnInference: dec2cnnQ에서 frame 받아 CNN 수행 후 cnn2spiQ로 전달
 * ================================================= */
static void TaskCnnInference(void* arg)
{
    while (1)
    {
        frame_t* f = NULL;
        xQueueReceive(dec2cnnQ, &f, portMAX_DELAY);

        char buf[64];
        snprintf(buf, sizeof(buf),
                 "[TaskCNN] frame=%d",
                 f->frame_id);
//        uart1_print(buf);

        u8 patch[TARGET_SIZE * TARGET_SIZE];
        int patch_count = 0;

        for (int py = 0; py < NUM_PATCH_Y; py++)
        {
            if (py == 1) continue; // 중앙 행 스킵

            for (int px = 0; px < NUM_PATCH_X; px++)
            {
                process_image_patch_frame(f, px, py, patch);
                f->spi_result[patch_count++] = (u8)cnn_inference(patch, px, py);
            }
        }

//        uart1_print("[TaskCNN] Result queued -> cnn2spiQ");
        xQueueSend(cnn2spiQ, &f, portMAX_DELAY);
    }
}

/* =================================================
 * TaskSpiTx: cnn2spiQ에서 frame 받아 SPI 전송 후 frame 반환
 * ================================================= */
static void TaskSpiTx(void* arg)
{
    while (1)
    {
        frame_t* f = NULL;
        xQueueReceive(cnn2spiQ, &f, portMAX_DELAY);

        char buf[64];
        snprintf(buf, sizeof(buf),
                 "[TaskSpiTx] frame=%d",
                 f->frame_id);
//        uart1_print(buf);

        xSemaphoreTake(spiMutex, portMAX_DELAY);
        SpiPsTransfer(f->spi_result);
        xSemaphoreGive(spiMutex);

        /* ★ END TIME */
        u64 t_end = now_ticks();
        double ms = ticks_to_ms(t_end - g_frame_start);
        double aver=ms/(f->frame_id+1);
        char buf2[64];
        snprintf(buf2, sizeof(buf2), "Average TIME: %.3f ms\r\n", aver);
        uart1_print(buf2);

//        uart1_print("[TaskSpiTx] SPI transmit done -> freeFrameQ");

        xQueueSend(freeFrameQ, &f, portMAX_DELAY);
    }
}

/* ============================================================
 * GPIO CS
 * ============================================================ */
static XGpioPs Gpio;

static void CsGpioInit(void)
{
    XGpioPs_Config* Cfg = XGpioPs_LookupConfig(GPIO_DEVICE_ID);
    if (!Cfg) {
        xil_printf("ERROR: XGpioPs_LookupConfig failed\r\n");
        return;
    }

    int Status = XGpioPs_CfgInitialize(&Gpio, Cfg, Cfg->BaseAddr);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: XGpioPs_CfgInitialize failed: %d\r\n", Status);
        return;
    }

    XGpioPs_SetDirectionPin(&Gpio, CS_MIO_PIN, 1);
    XGpioPs_SetOutputEnablePin(&Gpio, CS_MIO_PIN, 1);
    XGpioPs_WritePin(&Gpio, CS_MIO_PIN, 1); // idle HIGH
}

#define CS_LOW()   XGpioPs_WritePin(&Gpio, CS_MIO_PIN, 0)
#define CS_HIGH()  XGpioPs_WritePin(&Gpio, CS_MIO_PIN, 1)

/* ============================================================
 * UART0 JPEG 수신 (원본 유지)
 * ============================================================ */
// 함수 상단에 전역 변수 추가

int uart0_recv_image(u8* buf, int max_len, TickType_t timeout_ticks)
{
    u8 c = 0, prev_c = 0;
    int index = 0;
    int found_header = 0;
    TickType_t start = xTaskGetTickCount();

    while (1) {
        if ((xTaskGetTickCount() - start) > timeout_ticks) return -1;

        // [수정 부분] prefetch가 있으면 그것부터 사용, 없으면 UART에서 읽음
        int has_data = 0;
        if (g_prefetch_valid) {
            c = g_prefetch_byte;
            g_prefetch_valid = 0;
            has_data = 1;
        } else {
            has_data = (XUartPs_Recv(&Uart0, &c, 1) == 1);
        }

        if (has_data) {
            start = xTaskGetTickCount();
            if (!found_header) {
                if (prev_c == 0xFF && c == 0xD8) {
                    found_header = 1;
                    buf[index++] = 0xFF;
                    buf[index++] = 0xD8;
                }
            } else {
                if (index >= max_len) return -1;
                buf[index++] = c;
                if (prev_c == 0xFF && c == 0xD9) return index; // JPEG 끝
            }
            prev_c = c;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/* ============================================================
 * TJPGD handlers (frame_t 기반)
 * ============================================================ */
static size_t tjd_input_handler(JDEC* jd, uint8_t* buff, size_t ndata)
{
    frame_t* f = (frame_t*)jd->device;

    int available = f->jpeg_len - f->read_pos;
    size_t len = (ndata < (size_t)available) ? ndata : (size_t)available;

    if (len > 0) {
        memcpy(buff, f->jpeg_buf + f->read_pos, len);
        f->read_pos += (int)len;
    }
    return len;
}

static int tjd_output_handler(JDEC* jd, void* data, JRECT* rect)
{
    frame_t* f = (frame_t*)jd->device;
    u8* src = (u8*)data;

    for (int y = rect->top; y <= rect->bottom; y++) {
        for (int x = rect->left; x <= rect->right; x++) {
            if (y >= RAW_HEIGHT || x >= RAW_WIDTH) continue;

            f->raw[y][x][0] = *src++;
            f->raw[y][x][1] = *src++;
            f->raw[y][x][2] = *src++;
        }
    }
    return 1;
}

int jpeg_decode_to_raw_frame(frame_t* f)
{
    JDEC jd;
    JRESULT rc;

    /* jd->device = frame_t* 로 설정 */
    rc = jd_prepare(&jd, tjd_input_handler, work_buffer,
                    WORK_BUF_SIZE, (void*)f);
    if (rc != JDR_OK) return rc;

    rc = jd_decomp(&jd, tjd_output_handler, 0);
    return rc;
}

/* ============================================================
 * Patch processing (frame_t 기반)
 * ============================================================ */
void process_image_patch_frame(frame_t* f, int patch_x_idx, int patch_y_idx, u8* out_patch)
{
    int start_x = patch_x_idx * ORIG_PATCH_SIZE;
    int start_y = patch_y_idx * ORIG_PATCH_SIZE;

    float sx = (float)ORIG_PATCH_SIZE / TARGET_SIZE;
    float sy = (float)ORIG_PATCH_SIZE / TARGET_SIZE;

    int idx = 0;
    for (int ty = 0; ty < TARGET_SIZE; ty++) {
        for (int tx = 0; tx < TARGET_SIZE; tx++) {
            int src_x = start_x + (int)((tx + 0.5f) * sx);
            int src_y = start_y + (int)((ty + 0.5f) * sy);

            u8 R = f->raw[src_y][src_x][0];
            u8 G = f->raw[src_y][src_x][1];
            u8 B = f->raw[src_y][src_x][2];

            out_patch[idx++] = (u8)(0.299f * R + 0.587f * G + 0.114f * B);
        }
    }
}

/* ============================================================
 * CNN primitives (원본 유지)
 * ============================================================ */
static inline float relu(float x) { return (x > 0.f) ? x : 0.f; }

void conv2d_relu(const float* input, int h, int w, int ch,
    float* output, int out_ch,
    const float* weights, const float* biases,
    int kh, int kw, int pad)
{
    for (int k = 0; k < out_ch; k++)
        for (int i = 0; i < h; i++)
            for (int j = 0; j < w; j++) {
                float sum = 0.f;
                for (int c = 0; c < ch; c++)
                    for (int y = 0; y < kh; y++)
                        for (int x = 0; x < kw; x++) {
                            int ii = i + y - pad;
                            int jj = j + x - pad;
                            if (ii >= 0 && ii < h && jj >= 0 && jj < w) {
                                int in_idx = ii * w * ch + jj * ch + c;
                                int w_idx =
                                    y * kw * ch * out_ch +
                                    x * ch * out_ch +
                                    c * out_ch + k;
                                sum += input[in_idx] * weights[w_idx];
                            }
                        }
                sum += biases[k];
                output[i * w * out_ch + j * out_ch + k] = relu(sum);
            }
}

void max_pooling(const float* input, int h, int w, int ch, float* output)
{
    int out_h = h / 2, out_w = w / 2;
    int idx = 0;

    for (int c = 0; c < ch; c++)
        for (int i = 0; i < out_h; i++)
            for (int j = 0; j < out_w; j++) {
                float m = -1e30f;
                for (int y = 0; y < 2; y++)
                    for (int x = 0; x < 2; x++) {
                        int in_idx = (i * 2 + y) * w * ch + (j * 2 + x) * ch + c;
                        if (input[in_idx] > m) m = input[in_idx];
                    }
                output[idx++] = m;
            }
}

void dense_relu(const float* in, int in_dim,
    float* out, int out_dim,
    const float* w, const float* b)
{
    for (int j = 0; j < out_dim; j++) {
        float sum = b[j];
        for (int i = 0; i < in_dim; i++)
            sum += in[i] * w[i * out_dim + j];
        out[j] = relu(sum);
    }
}

static void softmax_inplace(float *x, int n)
{
    float maxv = x[0];
    for (int i = 1; i < n; i++) if (x[i] > maxv) maxv = x[i];

    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        x[i] = expf(x[i] - maxv);
        sum += x[i];
    }
    if (sum < 1e-20f) sum = 1e-20f;

    for (int i = 0; i < n; i++) x[i] /= sum;
}

void dense_softmax(const float *input_data, int in_dim,
                   float *output_data, int out_dim,
                   const float *weights, const float *biases)
{
    for (int j = 0; j < out_dim; j++) {
        float sum = 0.0f;
        for (int i = 0; i < in_dim; i++) {
            int weight_idx = i * out_dim + j;
            sum += input_data[i] * weights[weight_idx];
        }
        sum += biases[j];
        output_data[j] = sum;
    }
    softmax_inplace(output_data, out_dim);
}

/* ============================================================
 * CNN inference wrapper (원본 유지)
 * ============================================================ */
int cnn_inference(u8* input_patch, int px, int py)
{
    for (int i = 0; i < TARGET_SIZE * TARGET_SIZE; i++)
        input_fmap[i] = input_patch[i] / 255.f;

    conv2d_relu(input_fmap, 32, 32, 1, conv1_out, CONV2D_OUT_CH,
        CONV2D_WEIGHTS, CONV2D_BIASES, 3, 3, 1);
    max_pooling(conv1_out, 32, 32, CONV2D_OUT_CH, pool1_out);

    conv2d_relu(pool1_out, 16, 16, CONV2D_OUT_CH, conv2_out,
        CONV2D_1_OUT_CH, CONV2D_1_WEIGHTS, CONV2D_1_BIASES, 3, 3, 1);
    max_pooling(conv2_out, 16, 16, CONV2D_1_OUT_CH, pool2_out);

    conv2d_relu(pool2_out, 8, 8, CONV2D_1_OUT_CH, conv3_out,
        CONV2D_2_OUT_CH, CONV2D_2_WEIGHTS, CONV2D_2_BIASES, 3, 3, 1);
    max_pooling(conv3_out, 8, 8, CONV2D_2_OUT_CH, pool3_out);

    dense_relu(pool3_out, DENSE_IN_DIM, dense1_out,
        DENSE_OUT_DIM, DENSE_WEIGHTS, DENSE_BIASES);
    dense_softmax(dense1_out, DENSE_1_IN_DIM, final_output, DENSE_1_OUT_DIM,
                  DENSE_1_WEIGHTS, DENSE_1_BIASES);

    int best = 0;
    float bestp = final_output[0];
    for (int i = 1; i < DENSE_1_OUT_DIM; i++) {
        if (final_output[i] > bestp) {
            bestp = final_output[i];
            best = i;
        }
    }
    return best;
}

/* ============================================================
 * SPI init / transfer (원본 유지)
 * ============================================================ */
int SpiPsMasterInit(void)
{
    XSpiPs_Config* Cfg = XSpiPs_LookupConfig(SPI_DEVICE_ID);
    if (!Cfg) return XST_FAILURE;

    XSpiPs_CfgInitialize(&SpiInstance, Cfg, Cfg->BaseAddress);
    XSpiPs_Reset(&SpiInstance);

    XSpiPs_SetOptions(&SpiInstance,
        XSPIPS_MASTER_OPTION | XSPIPS_FORCE_SSELECT_OPTION);
    XSpiPs_SetClkPrescaler(&SpiInstance, SPI_CLK_PRESCALER);
    XSpiPs_SetSlaveSelect(&SpiInstance, 0);

    return XST_SUCCESS;
}

int SpiPsTransfer(u8* tx)
{
    memcpy(SpiSendBuffer, tx, TRANSFER_LENGTH);
    Xil_DCacheFlushRange((INTPTR)SpiSendBuffer, TRANSFER_LENGTH);

    CS_LOW();
    XSpiPs_PolledTransfer(&SpiInstance,
        SpiSendBuffer, SpiRecvBuffer, TRANSFER_LENGTH);
    CS_HIGH();

    return XST_SUCCESS;
}
