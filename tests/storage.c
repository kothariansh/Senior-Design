#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
static unsigned char files[2][8192];static size_t sizes[2];static bool mounted=true,fail=false;static char logbuf[16384];
static bool sd_is_ready(void){return mounted;}
static int sd_read_file(const char*n,void*p,size_t cap,size_t*l){int i=n[4]-'0';if(!mounted||!sizes[i]||cap<sizes[i])return -1;memcpy(p,files[i],sizes[i]);*l=sizes[i];return 0;}
static int sd_write_file(const char*n,const void*p,size_t s){int i=n[4]-'0';if(!mounted)return -1; sizes[i]=fail?s/2:s;memcpy(files[i],p,sizes[i]);return fail?-1:0;}
static void uart_link_send(const char*s){strcat(logbuf,s);}
#include "../wroom_firmware/main/store_link.h"
static void save(char*s){char l[80];sprintf(l,"BEGIN:%u:%08x",(unsigned)strlen(s),digest(s,strlen(s)));store_line(l);sprintf(l,"DATA:%s",s);store_line(l);store_line("END");}
int main(void){save("first");assert(strstr(logbuf,"SAVE:OK"));save("second");assert(latest()==1);fail=true;save("third");assert(strstr(logbuf,"SAVE:ERR"));logbuf[0]=0;store_line("LOAD");assert(strstr(logbuf,"LOAD:DATA:second"));mounted=false;logbuf[0]=0;save("removed");assert(strstr(logbuf,"SAVE:ERR"));mounted=true;fail=false;save("reinsert");assert(latest()==0);logbuf[0]=0;store_line("BEGIN:3:00000000");store_line("DATA:bad");store_line("END");assert(strstr(logbuf,"SAVE:ERR"));}
