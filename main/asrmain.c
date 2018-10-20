#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2s.h"
#include "../components/ac101_driver/AC101.h"
#include "../components/ac101_driver/recoder.h"
#include "../components/ac101_driver/uart.h"

#include "nvs_flash.h"
#include "sdkconfig.h"

#include <curl/curl.h>
#include "string.h"
#include "common.h"
#include "token.h"
#include "asrmain.h"

#include "simple_wifi.h"

#define CONFIG_AC101_I2S_DATA_IN_PIN 35

#define XF_I2S_MAX_SIZE 2048
#define XF_I2S_SAMPLE 70

const char ASR_SCOPE[] = "audio_voice_assistant_get";
const char API_ASR_URL[] = "http://vop.baidu.com/server_api"; // 可改为https

/**
 * @brief
 * @param config
 */
static RETURN_CODE fill_config(struct asr_config *config) {
    // 填写网页上申请的appkey 如 g_api_key="g8eBUMSokVB1BHGmgxxxxxx"
    char api_key[] = "4E1BG9lTnlSeIf1NQFlrSq6h";
    // 填写网页上申请的APP SECRET 如 $secretKey="94dc99566550d87f8fa8ece112xxxxx"
    char secret_key[] = "544ca4657ba8002e3dea3ac2f5fdd241";
    // 需要识别的文件
//    char *filename = "16k_test.pcm";
//    FILE *fp = fopen(filename, "r");
//    if (fp == NULL) {
//        //文件不存在
//        snprintf(g_demo_error_msg, BUFFER_ERROR_SIZE,
//                 "current running directory does not contain file %s", filename);
//        return ERROR_ASR_FILE_NOT_EXIST;
//    }
    // 文件后缀 pcm/wav/amr ,不支持其它格式
    char format[] = "pcm";

    //  1537 表示识别普通话，使用输入法模型。1536表示识别普通话，使用搜索模型 其它语种参见文档
    int dev_pid = 1537;

    // 将上述参数填入config中
    snprintf(config->api_key, sizeof(config->api_key), "%s", api_key);
    snprintf(config->secret_key, sizeof(config->secret_key), "%s", secret_key);
//    config->file = fp;
    config->file = NULL;
    snprintf(config->format, sizeof(config->format), "%s", format);
    config->rate = 16000; // 采样率固定值
    config->dev_pid = dev_pid;
    snprintf(config->cuid, sizeof(config->cuid), "1234567C");

    return RETURN_OK;
}

// 获取token 并调用识别接口
RETURN_CODE run() {
    struct asr_config config;
    char token[MAX_TOKEN_SIZE];

    RETURN_CODE res = fill_config(&config);
    if (res == RETURN_OK) {
        // 获取token
        res = speech_get_token(config.api_key, config.secret_key, ASR_SCOPE, token);
        if (res == RETURN_OK) {
            // 调用识别接口
            run_asr(&config, token);
        }
    }
    if (config.file != NULL) {
        fclose(config.file);
    }
    return res;
}

// 调用识别接口
RETURN_CODE run_asr(struct asr_config *config, const char *token) {
    char url[300];
    CURL *curl = curl_easy_init(); // 需要释放
    char *cuid = curl_easy_escape(curl, config->cuid, strlen(config->cuid)); // 需要释放

    snprintf(url, sizeof(url), "%s?cuid=%s&token=%s&dev_pid=%d",
             API_ASR_URL, cuid, token, config->dev_pid);
    free(cuid);
    printf("request url :%s\n", url);

    struct curl_slist *headerlist = NULL;
    char header[50];
    snprintf(header, sizeof(header), "Content-Type: audio/%s; rate=%d", config->format,
             config->rate);
    headerlist = curl_slist_append(headerlist, header); // 需要释放

    int content_len = 0;
    char *result = NULL;
//    char *audio_data = read_file_data(config->file, &content_len); // 读取文件， 需要释放

    char *buff = (char *) malloc(sizeof(char) * XF_I2S_MAX_SIZE * XF_I2S_SAMPLE); // 记得释放
	char *pbuff = buff;
	i2s_start(I2S_NUM_0);
	for (int i = 0; i < XF_I2S_SAMPLE; i++) // 3s
	{
		i2s_read_bytes(I2S_NUM_0, pbuff, XF_I2S_MAX_SIZE, portMAX_DELAY);
		i2s_write_bytes(I2S_NUM_0, pbuff, XF_I2S_MAX_SIZE, portMAX_DELAY);
		vTaskDelay(50 / portTICK_PERIOD_MS);
		printf("i2s_read i=%d\n", i);
		printf("%p\n", pbuff);
		pbuff = pbuff + XF_I2S_MAX_SIZE;
	}
	i2s_stop(I2S_NUM_0);
	pbuff = NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5); // 连接5s超时
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60); // 整体请求60s超时
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist); // 添加http header Content-Type
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buff); // 音频数据
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, sizeof(char) * XF_I2S_MAX_SIZE * XF_I2S_SAMPLE); // 音频数据长度
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);  // 需要释放

    CURLcode res_curl = curl_easy_perform(curl);
    RETURN_CODE res = RETURN_OK;
    if (res_curl != CURLE_OK) {
        // curl 失败
        snprintf(g_demo_error_msg, BUFFER_ERROR_SIZE, "perform curl error:%d, %s.\n", res,
                 curl_easy_strerror(res_curl));
        res = ERROR_ASR_CURL;
    } else {
        printf("YOUR FINAL RESULT: %s\n", result);
    }

    curl_slist_free_all(headerlist);
//    free(audio_data);
    free(result);
    free(buff);
    curl_easy_cleanup(curl);
    return res;
}

static void audio_recorder_AC101_init()
{
	AC101_init();

	i2s_config_t i2s_config = {
	        .mode = I2S_MODE_MASTER |I2S_MODE_RX | I2S_MODE_TX,
	        .sample_rate = 16000,
	        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
	        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,                           //1-channels
	        .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
	        .dma_buf_count = 32,
	        .dma_buf_len = 32 *2,
	        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1                                //Interrupt level 1
	    };

	i2s_pin_config_t pin_config_rx = {
	        .bck_io_num = CONFIG_AC101_I2S_BCK_PIN,
	        .ws_io_num = CONFIG_AC101_I2S_LRCK_PIN,
	        .data_out_num = CONFIG_AC101_I2S_DATA_PIN,
	        .data_in_num = CONFIG_AC101_I2S_DATA_IN_PIN
	    };

	int reg_val = REG_READ(PIN_CTRL);
	REG_WRITE(PIN_CTRL, 0xFFFFFFF0);
	reg_val = REG_READ(PIN_CTRL);
	PIN_FUNC_SELECT(GPIO_PIN_REG_0, 1); //GPIO0 as CLK_OUT1

	/* 注册i2s设备驱动 */
	i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
	/* 设置i2s引脚 */
	i2s_set_pin(I2S_NUM_0, &pin_config_rx);
	/* 停止i2s设备 */
	i2s_stop(I2S_NUM_0);
}

static void asr_task(void *pvParameters) {
	curl_global_init(CURL_GLOBAL_ALL);
	RETURN_CODE rescode = run();
	curl_global_cleanup();
	if (rescode != RETURN_OK) {
		fprintf(stderr, "ERROR: %s, %d", g_demo_error_msg, rescode);
	}

//	while(1);
//	printf("tts_task over!\n");
	vTaskDelete(NULL);
}

void app_main() {
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES
			|| ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

#if EXAMPLE_ESP_WIFI_MODE_AP
	printf("ESP_WIFI_MODE_AP\n");
	wifi_init_softap();
#else
	printf("ESP_WIFI_MODE_STA\n");
	wifi_init_sta();
#endif /*EXAMPLE_ESP_WIFI_MODE_AP*/

	audio_recorder_AC101_init();
//	xTaskCreatePinnedToCore(&alexa__AC101_task, "alexa__AC101_task", 8096, NULL,
//			2, NULL, 1);
	xTaskCreatePinnedToCore(&asr_task, "asr_task", 8096 * 2, NULL, 2, NULL, 1);
//	xTaskCreatePinnedToCore(&tts_play, "tts_play", 8096, NULL, 2, NULL, 1);

}
