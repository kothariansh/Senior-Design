#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "uart_link.h"
#include "audio.h"
#include "driver/gpio.h"
#include "nfc.h"
#include "sd_card.h"

static const char *TAG = "CATAN_WROOM";

/*
 * Embedded sound files.
 * These linker symbols match the existing audio_test implementation.
 */
extern const uint8_t ka_ching_start[]
    asm("_binary_ka_ching_raw_start");

extern const uint8_t ka_ching_end[]
    asm("_binary_ka_ching_raw_end");

extern const uint8_t error_start[]
    asm("_binary_error_raw_start");

extern const uint8_t error_end[]
    asm("_binary_error_raw_end");


#include "freertos/queue.h"
#include "store_link.h"
#include "boards.h"
static QueueHandle_t sounds;
#define CLIP(n) extern const uint8_t n##_s[] asm("_binary_" #n "_raw_start"), n##_e[] asm("_binary_" #n "_raw_end")
CLIP(dice_louder);CLIP(build);CLIP(robber);
static const uint8_t *starts[]={dice_louder_s,build_s,robber_s,ka_ching_start,ka_ching_start,error_start};
static const uint8_t *ends[]={dice_louder_e,build_e,robber_e,ka_ching_end,ka_ching_end,error_end};
// Momentary BOOT press after startup auditions the same dice clip without the S3.
static void speaker_button(void *unused){
 gpio_config_t cfg={.pin_bit_mask=1ULL<<GPIO_NUM_0,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,.intr_type=GPIO_INTR_DISABLE};
 if(gpio_config(&cfg)!=ESP_OK){vTaskDelete(NULL);return;}
 int armed=0,stable=0,prev=-1;
 for(;;){int level=gpio_get_level(GPIO_NUM_0);
  stable=(level==prev)?(stable<4?stable+1:4):0;prev=level;
  if(stable==3){if(level)armed=1;else if(armed){int cue=0;armed=0;ESP_LOGI(TAG,"Local dice test");xQueueSend(sounds,&cue,0);}}
  vTaskDelay(pdMS_TO_TICKS(20));}
}
static void audio_worker(void *arg){int i;for(;;)if(xQueueReceive(sounds,&i,portMAX_DELAY))play_sound(starts[i],ends[i]);}
// 0 = checking, 1 = mounted and read/write verified, -1 = unavailable.
static atomic_int sd_state;
static atomic_bool reader_offline;

#define SD_READY_POLL_MS 750
#define SD_RETRY_MS 1500
#define SD_REMOVAL_CONFIRMATIONS 2

static const char *sd_status_message(void)
{
    int state = atomic_load(&sd_state);
    return state > 0 ? "SD:READY\n" : state < 0 ? "SD:ERROR\n" : "SD:CHECKING\n";
}

static void sd_publish_state(int state)
{
    if (atomic_exchange(&sd_state, state) != state) {
        uart_link_send(sd_status_message());
    }
}

static void sd_monitor_task(void *arg)
{
    (void)arg;
    int missing_checks = 0;
    for (;;) {
        if (!sd_is_ready()) {
            // Failed mounts stay at ERROR; do not flicker to CHECKING on every retry.
            esp_err_t err = sd_init();
            if (err == ESP_OK) {
                sd_publish_state(0);
                // Give the S3 one display refresh before the read/write check.
                vTaskDelay(pdMS_TO_TICKS(300));
                err = sd_self_test();
                if (err == ESP_OK) {
                    missing_checks = 0;
                    sd_publish_state(1);
                    vTaskDelay(pdMS_TO_TICKS(SD_READY_POLL_MS));
                    continue;
                }
                esp_err_t cleanup = sd_unmount();
                if (cleanup != ESP_OK) {
                    ESP_LOGW(TAG, "SD cleanup failed: %s", esp_err_to_name(cleanup));
                }
            }
            sd_publish_state(-1);
            ESP_LOGW(TAG, "SD unavailable: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(SD_RETRY_MS));
            continue;
        }

        esp_err_t err = sd_check_media();
        if (err == ESP_OK) {
            missing_checks = 0;
        } else if (++missing_checks >= SD_REMOVAL_CONFIRMATIONS) {
            ESP_LOGW(TAG, "SD media lost: %s", esp_err_to_name(err));
            sd_publish_state(-1);
            esp_err_t cleanup = sd_unmount();
            if (cleanup != ESP_OK) {
                ESP_LOGW(TAG, "SD cleanup failed: %s", esp_err_to_name(cleanup));
            }
            missing_checks = 0;
            vTaskDelay(pdMS_TO_TICKS(SD_RETRY_MS));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(SD_READY_POLL_MS));
    }
}

static void uart_receive_task(void *arg)
{
    (void)arg;
    char line[UART_LINK_LINE_SIZE];
    for (;;) {
        if (uart_link_read_line(line, sizeof(line), 100) <= 0) {
            continue;
        }
        if (strcmp(line, "S3:READY") == 0) {
            uart_link_send("WROOM:READY\n");
            if(atomic_load(&reader_offline))uart_link_send("NFC:OFFLINE\n");
            uart_link_send(sd_status_message());
        } else if(!strncmp(line,"SOUND:",6)){
            static unsigned session,seq;unsigned a,b;char cue[16];if(sscanf(line+6,"%x:%u:%15s",&a,&b,cue)!=3||(a==session&&b<=seq))continue;
            session=a;seq=b;const char* names[]={"DICE","BUILD","ROBBER","SCAN","TRADE","ERROR"};
            for(int i=0;i<6;i++)if(!strcmp(cue,names[i])){ESP_LOGI(TAG,"Cue %s (seq %u)",cue,b);if(!xQueueSend(sounds,&i,0))ESP_LOGW(TAG,"Audio queue full");break;}
        } else if(!strncmp(line,"BOARD:",6)){int slot=atoi(line+6);uint8_t cells[19];if(board_get(slot,cells)){char out[64];int k=snprintf(out,sizeof out,"BOARD:%d:",slot);for(int i=0;i<19;i++)sprintf(out+k+2*i,"%02X",cells[i]);strcat(out,"\n");uart_link_send(out);}else uart_link_send("BOARD:ERROR\n");}else store_line(line);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting CATAN WROOM controller");

    audio_init();

    sounds=xQueueCreate(16,sizeof(int));
    assert(sounds);xTaskCreate(audio_worker,"sound",3072,NULL,4,NULL);
    uart_link_init();
    if (xTaskCreate(uart_receive_task, "uart_receive", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Unable to create UART receive task");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    uart_link_send("WROOM:READY\n");

    // The SD monitor owns mount/retry work; NFC and audio continue independently.
    esp_err_t sd_err = sd_prepare();
    if (sd_err == ESP_OK) {
        if (xTaskCreate(sd_monitor_task, "sd_monitor", 8192, NULL, 4, NULL) != pdPASS) {
            sd_err = ESP_ERR_NO_MEM;
        }
    }
    if (sd_err != ESP_OK) {
        sd_publish_state(-1);
        ESP_LOGE(TAG, "SD monitor unavailable: %s", esp_err_to_name(sd_err));
    }

    board_ble_start();
    ESP_LOGI(TAG,"Audio v3 ready: tap BOOT for local dice test");
    xTaskCreate(speaker_button,"speaker_btn",3072,NULL,3,NULL);
    ESP_LOGI(TAG, "Waiting for NFC fob");

    if(!nfc_init()){atomic_store(&reader_offline,true);uart_link_send("NFC:OFFLINE\n");return;}
    uint8_t uid[7],n;
    bool latched=false,missing=false;TickType_t since=0;
    for(;;){
        if(nfc_read_uid(uid,&n,100)){
            missing=false;
            if(!latched && (n==4||n==7)){char line[32]="UID:";
                for(int i=0;i<n;i++)sprintf(line+4+2*i,"%02X",uid[i]);
                strcat(line,"\n");uart_link_send(line);latched=true;}
        }else if(!missing){missing=true;since=xTaskGetTickCount();}
        else if(xTaskGetTickCount()-since>=pdMS_TO_TICKS(300))latched=false;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
