#if !defined(__EDGEBOX_H__)
#define __EDGEBOX_H__
#include <stdio.h>



struct lora_node {
    char name[64];
    struct lora_node *next;
    struct switch_node *list_head;
};




extern char *ws_url;
extern char *weather_lorasn;
extern int temp_humi_485_addr;
extern int wind_dir_485_addr;
extern int wind_speed_485_addr;

struct lora_node *lora_list_head;
#endif
