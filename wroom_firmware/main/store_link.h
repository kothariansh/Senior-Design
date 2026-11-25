// Single owner: WROOM UART receiver. Each slot contains a checksum and generation.
#include <inttypes.h>
#include <stdlib.h>
#define SNAP_MAX 4096
typedef struct {uint32_t magic,rev,count,hash; char data[SNAP_MAX];} slot_t;
static slot_t staging,previous,probe;
static uint32_t digest(const char *p,size_t n){uint32_t h=2166136261u;while(n--)h=(h^(uint8_t)*p++)*16777619u;return h;}
static int latest(void){
 int best=-1;previous.rev=0;
 for(int i=0;i<2;i++){size_t n=0;if(sd_read_file(i?"GAME1.BIN":"GAME0.BIN",&probe,sizeof probe,&n)==ESP_OK &&
 n>=16 && probe.magic==0x43415431 && probe.count<=SNAP_MAX && n==16+probe.count &&
 probe.hash==digest(probe.data,probe.count) && (best<0||probe.rev>previous.rev)){previous=probe;best=i;}}
 return best;
}
static unsigned expected;static uint32_t wanted;
static void store_line(char* l){
 if(sscanf(l,"BEGIN:%u:%" SCNx32,&expected,&wanted)==2){staging.count=0;return;}
 if(!strncmp(l,"DATA:",5)){size_t n=strlen(l+5);if(staging.count+n<=SNAP_MAX){memcpy(staging.data+staging.count,l+5,n);staging.count+=n;}else expected=0;return;}
 if(!strcmp(l,"END")){bool ok=false;if(expected && staging.count==expected && digest(staging.data,expected)==wanted && sd_is_ready()){
 int last=latest();staging.magic=0x43415431;staging.rev=previous.rev+1;staging.hash=wanted;
 const char* path=last==0?"GAME1.BIN":"GAME0.BIN";size_t bytes=0;
 ok=sd_write_file(path,&staging,16+expected)==ESP_OK && sd_read_file(path,&probe,sizeof probe,&bytes)==ESP_OK && bytes==16+expected && memcmp(&probe,&staging,bytes)==0;}
 char reply[48];snprintf(reply,sizeof reply,"SAVE:%s:%08" PRIx32 "\n",ok?"OK":"ERR",wanted);uart_link_send(reply);expected=0;return;}
 if(!strcmp(l,"LOAD")){if(latest()<0){uart_link_send("LOAD:EMPTY\n");return;}
 char msg[120];snprintf(msg,sizeof msg,"LOAD:BEGIN:%08" PRIx32 "\n",previous.hash);uart_link_send(msg);
 for(unsigned i=0;i<previous.count;i+=80){int n=previous.count-i;if(n>80)n=80;snprintf(msg,sizeof msg,"LOAD:DATA:%.*s\n",n,previous.data+i);uart_link_send(msg);}
 uart_link_send("LOAD:END\n");}
}
