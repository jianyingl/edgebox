#if !defined(__MQTT_H__)
#define __MQTT_H__
#include "mqtt/MQTTClient.h"

/* function code to read */
#define FC_READ  0x03
/* function code to write */
#define FC_WRITE 0x06

extern struct MQTTClient mclient;
extern struct Network mnet;

extern const char *eb_product_key;    
extern const char *eb_device_name;    
extern const char *eb_device_secret;

extern char eb_pub_topic[];
extern char eb_sub_topic[];


int mqtt_conn_init();

void eb_publish(char *topic, char *content, int size);

void *iot_msg_handler_thread(void *arg_ptr);

//void eb_report_switch_data_for_loranode(struct lora_node *lora);
#endif
