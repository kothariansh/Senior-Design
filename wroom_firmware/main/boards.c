#include "boards.h"
#include "sd_card.h"
#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
// nibble high = terrain (brick,lumber,wool,grain,ore,desert), low = token.
static bool valid(const uint8_t*b){int deserts=0;for(int i=0;i<19;i++){int t=b[i]>>4,n=b[i]&15;
 if(t==5 && n==0){deserts++;continue;}if(t>=5||n<2||n>12||n==7)return false;}return deserts==1;}
bool board_put(int slot,const uint8_t*b){if(slot<0||slot>2||!valid(b))return false;
 char name[]="BOARD0.BIN";name[5]+=(char)slot;return sd_write_file(name,b,19)==ESP_OK;}
bool board_get(int slot,uint8_t*b){if(slot<0||slot>2)return false;char name[]="BOARD0.BIN";name[5]+=(char)slot;size_t n;
 esp_err_t err=sd_read_file(name,b,19,&n);if(err==ESP_OK)return n==19&&valid(b);if(err!=ESP_ERR_NOT_FOUND)return false;
 static const uint8_t base[]={0x15,0x22,0x36,0x03,0x48,0x1a,0x29,0x3c,0x0b,0x50,0x44,0x18,0x2a,0x39,0x04,0x35,0x46,0x13,0x2b};
 for(int i=0;i<19;i++){b[i]=base[(i+slot*4)%19];}return board_put(slot,b);}
static ble_uuid128_t service=BLE_UUID128_INIT(1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xca);
static ble_uuid128_t upload=BLE_UUID128_INIT(2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xca);
static int write_board(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt*c,void*arg){
 uint8_t data[20];if(OS_MBUF_PKTLEN(c->om)!=20||os_mbuf_copydata(c->om,0,20,data))return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
 return board_put(data[0],data+1)?0:BLE_ATT_ERR_UNLIKELY;}
static const struct ble_gatt_chr_def chars[]={ {.uuid=&upload.u,.access_cb=write_board,.flags=BLE_GATT_CHR_F_WRITE},{0}};
static const struct ble_gatt_svc_def services[]={ {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service.u,.characteristics=chars},{0}};
static uint8_t addr;
static void advertise(void);
static int event(struct ble_gap_event*e,void*a){if(e->type==BLE_GAP_EVENT_DISCONNECT||(e->type==BLE_GAP_EVENT_CONNECT&&e->connect.status))advertise();return 0;}
static void advertise(void){struct ble_hs_adv_fields f={0};f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
 f.uuids128=&service;f.num_uuids128=1;f.uuids128_is_complete=1;ble_gap_adv_set_fields(&f);
 struct ble_gap_adv_params p={0};p.conn_mode=BLE_GAP_CONN_MODE_UND;p.disc_mode=BLE_GAP_DISC_MODE_GEN;ble_gap_adv_start(addr,NULL,BLE_HS_FOREVER,&p,event,NULL);}
static void sync_host(void){if(ble_hs_id_infer_auto(0,&addr)==0)advertise();}
static void host(void*a){nimble_port_run();nimble_port_freertos_deinit();}
void board_ble_start(void){if(nvs_flash_init()!=ESP_OK||nimble_port_init()!=ESP_OK)return;
 ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set("CATAN Board");ble_hs_cfg.sync_cb=sync_host;
 if(ble_gatts_count_cfg(services)||ble_gatts_add_svcs(services))return;
 nimble_port_freertos_init(host);}
