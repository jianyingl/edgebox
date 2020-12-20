/*
 * statusquery.c
 *
 * Created on: 2020/11/21
 *     Author: luojianying
 *
 * This file defines the functions related with MQTT connection initialization 
 * and functions handling MQTT messages from/to IoT hub.
 */
#include <stdio.h>
#include <memory.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>

#include "mqtt/MQTTClient.h"
#include "mqtt.h"
#include "parse_switch.h"

#include "edgebox.h"
#include "cJSON.h"
#include "logger.h"
#include "crc.h"

MQTTClient mclient;
Network mnet;

const char *eb_product_key;    /* edgebox product key at aliyun IoT hub */
const char *eb_device_name;    /* edgebox device name */
const char *eb_device_secret;  /* edgebox device secret */

char eb_pub_topic[256];        /* topic used for publish, post edgebox event to IoT hub */
char eb_sub_topic[256];        /* topic used for subscribe, receive event from IoT hub */


int msgid_in_payload = 0;

/* declare the external function aiotMqttSign() */
extern int aiotMqttSign(const char *productKey, const char *deviceName, const char *deviceSecret, 
                         char clientId[150], char username[65], char password[65]);


typedef struct iothubmsg_node {
    char *msg;
    struct iothubmsg_node *next;
}iothubmsg_node;

iothubmsg_node *p_hubmsg_q_head = NULL;
iothubmsg_node *p_hubmsg_q_tail = NULL;

/* mutex to operate the iot hub msg queue */
pthread_mutex_t hubmsg_mutex = PTHREAD_MUTEX_INITIALIZER;


/*
 * iothubMsgEnQ()
 *
 * Insert a new incoming iot hub message at the queue tail.
 */

void iothubMsgEnQ(char *msg, int msg_len)
{
    iothubmsg_node *msg_node_ptr;

    msg_node_ptr = (iothubmsg_node *) malloc(sizeof(iothubmsg_node));
    if (msg_node_ptr == NULL)
    {
        LOG_ERROR("iothubMsgEnQ(): Memory allocation failed.");
        return;
    }
    
    char *msg_ptr;
    msg_ptr = (char *)malloc(msg_len + 1);
    if (msg_ptr == NULL) {
        LOG_ERROR("iothubMsgEnQ(): Memory allocation failed.");
        free(msg_node_ptr);
        return;
    }
    memset(msg_ptr, 0, msg_len + 1);
    memcpy(msg_ptr, msg, msg_len);

    msg_node_ptr->msg  = msg_ptr;
    msg_node_ptr->next = NULL;

    /* lock the queue */
    pthread_mutex_lock(&hubmsg_mutex);

    /* If queue is empty before insert */
    if (p_hubmsg_q_head == NULL)
    {
        p_hubmsg_q_head = msg_node_ptr;
        
        /* notify the message handling thread */
        //sem_post(&sem_empty_q);
    }

    if (p_hubmsg_q_tail != NULL)
    {
        p_hubmsg_q_tail->next = msg_node_ptr;
    }

    // tail always points to the new inserted node 
    p_hubmsg_q_tail = msg_node_ptr;

    /* unlock the Queue */
    pthread_mutex_unlock(&hubmsg_mutex);

    LOG_DEBUG("iothubMsgEnQ(): msg_node_ptr = %x, msg = %s.", msg_node_ptr, msg_node_ptr->msg);
}

/*
 * iothubMsgDeQ()
 *
 * Remove a iot hub message from the queue head.
 */
void iothubMsgDeQ(iothubmsg_node **node_ptr)
{

    /* lock the queue */
    pthread_mutex_lock(&hubmsg_mutex);

    if (p_hubmsg_q_head != NULL)
    {
        *node_ptr = p_hubmsg_q_head;

        if (p_hubmsg_q_head == p_hubmsg_q_tail)
        {
            p_hubmsg_q_head = NULL;
            p_hubmsg_q_tail = NULL;
        }
        else
        {
            p_hubmsg_q_head = p_hubmsg_q_head->next;
        }
    }

    /* unlock the queue */
    pthread_mutex_unlock(&hubmsg_mutex);

}

/*
 * iot_msg_handler_thread()
 *
 * the thread to handle messages received from IoT hub and insert them into the the message queue.
 */
void *iot_msg_handler_thread(void *arg_ptr)
{
    iothubmsg_node *node_ptr;

    while (1)
    {
        /* check if message queue is empty */
        if (p_hubmsg_q_head == NULL)
        {
        /* wait if the message queue is empty */
            //sem_wait(&sem_empty_q);
            poll(0, 0, 20);
            continue;
        }

        /* message queue is not empty. get a message from the head. */
        iothubMsgDeQ(&node_ptr);
        LOG_DEBUG("iot_msg_handler_thread(): node_ptr = %x, msg_ptr = %x .", node_ptr, node_ptr->msg);

        /* now process this msg */
        LOG_DEBUG("raw json text from IoT hub: %s", node_ptr->msg);
        process_single_iot_hub_msg(node_ptr->msg);

        /* free the allocated memory afer the msg is processed */
        free(node_ptr->msg);
        free(node_ptr);
    }
}

/* 
 * process_single_iot_hub_msg()
 * 
 * the function to process a single iot hub message.
 */
void process_single_iot_hub_msg(char *json_text)
{

    if (json_text == NULL) {
        LOG_DEBUG("receive null pointer json_text");
        return;
    }

    cJSON *root = cJSON_Parse(json_text);
    if (root == NULL) {
        char *error_ptr = cJSON_GetErrorPtr();
        LOG_ERROR("Failed to parse json text, error =%s, json_text = %s", error_ptr, json_text);
        goto end;
    }

    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (params == NULL) {
        LOG_ERROR("Failed to parse json text, params undefined, json_text = %s", json_text);
        goto end;
    }

    char loranodesn[32] = {0};
    struct switch_node *list_head = NULL;
    cJSON *current_element = NULL;
    char *current_key = NULL;
    cJSON_ArrayForEach(current_element, params)
    {
        current_key = current_element->string;
        if (current_key != NULL)
        {

            if (strcmp(current_key, "loraNodeSerialNo") == 0) {
                strcpy(loranodesn, current_element->valuestring);
                LOG_DEBUG2("loranodesn =  %s", loranodesn);
            } else {
                struct switch_node *p = NULL;
                LOG_DEBUG2("parameter =  %s", current_element->string);
                p = get_switch_node(current_element->string);
                if (p != NULL) {
                    struct switch_node *q = malloc(sizeof(struct switch_node));
                    memcpy(q, p, sizeof(struct switch_node));
                    LOG_DEBUG2("key = %s, type = %d", current_key, current_element->type);
                    if (current_element->type == cJSON_Number) {

                        /* special case for some float value */
                        if (strcmp(current_element->string, "waterFertilizerPressure") == 0) {
                            q->v.intv = current_element->valuedouble * 10;
                        } else {
                            q->v.intv = current_element->valueint;
                        }
                        LOG_DEBUG2("value = %d, value float = %f", q->v.intv, current_element->valuedouble);
                    }
                    add_switch_to_list(&list_head, q);
                    LOG_DEBUG2("add switch name %s", current_element->string);
                }
            }
        }
    }

    /* execute the control commands on the Lora node in this message */
    run_command_on_lora_node(loranodesn, list_head);

    /* free the switch node list */
    free_switch_list(&list_head);

end:
    cJSON_Delete(root);
}

/*
 * handleMsgFromIoThub()
 *
 * function registered to handle messages from IoT hub
 */
void handleMsgFromIoThub(MessageData* md)
{
    MQTTMessage* message = md->message;

    LOG_DEBUG("Received topic from IoT hub: len = %d, topic = %s", 
              md->topicName->lenstring.len, md->topicName->lenstring.data);
    LOG_DEBUG("Received json text from IoT hub: len = %d, payload = %s", 
             (int)message->payloadlen, (char*)message->payload);

    iothubMsgEnQ(message->payload, message->payloadlen);

}


/*
 * mqtt_conn_init()
 *
 * Create the MQTT connection to IoT hub.
 * 
 * NOTE: This function must be re-enterable so that once the connection is
 *       broken, it can be re-called to initialized again.
 */

int mqtt_conn_init()
{
    int rc = 0;

    /* setup the buffer, it must big enough for aliyun IoT platform */
    unsigned char buf[1024*4];
    unsigned char readbuf[1024*4];

    char hostname[128];
    short port = 443;

    sprintf(hostname, "%s.iot-as-mqtt.cn-shanghai.aliyuncs.com", eb_product_key);


    /* invoke aiotMqttSign to generate mqtt connect parameters */
    char clientId[150] = {0};
    char username[65] = {0};
    char password[65] = {0};

    if ((rc = aiotMqttSign(eb_product_key, eb_device_name, eb_device_secret, 
                           clientId, username, password) < 0)) {
        LOG_ERROR("aiotMqttSign -%0x4x\n", -rc);
        return -1;
    }

    LOG_INFO("Connecting IoT Hub ... ");
    LOG_INFO("    clientid: %s", clientId);
    LOG_INFO("    username: %s", username);
    LOG_INFO("    password: %s", password);



    /* network init and establish network to aliyun IoT platform */
    NetworkInit(&mnet);
    rc = NetworkConnect(&mnet, hostname, port);
    if (rc < 0) {
        LOG_ERROR("NetworkConnect %d\n", rc);
        return -1;
    }

    /* init mqtt client */
    MQTTClientInit(&mclient, &mnet, 5000, buf, sizeof(buf), readbuf, sizeof(readbuf));
 
    /* set the default message handler */
    mclient.defaultMessageHandler = handleMsgFromIoThub;

    /* set mqtt connect parameter */
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
    data.willFlag = 0;
    data.MQTTVersion = 3;
    data.clientID.cstring = clientId;
    data.username.cstring = username;
    data.password.cstring = password;
    data.keepAliveInterval = 600;
    data.cleansession = 1;
    printf("Connecting to %s %d\n", hostname, port);

    if (MQTTConnect(&mclient, &data) == 0) {
        LOG_INFO("Connect aliyun IoT Cloud Success!", rc);
    } else {
        LOG_ERROR("Failed to connect aliyun IoT Cloud, rc = %d", rc);
        return -1;
    }
    
    
    sprintf(eb_pub_topic, "/sys/%s/%s/thing/event/property/post", eb_product_key, eb_device_name);
    sprintf(eb_sub_topic, "/sys/%s/%s/thing/service/property/set", eb_product_key, eb_device_name);

    rc = MQTTSubscribe(&mclient, eb_sub_topic, 1, handleMsgFromIoThub);
    if (rc == 0) {
        LOG_INFO("MQTTSubscribe success, topic = %s", eb_sub_topic);
    } else {
        LOG_ERROR("MQTTSubscribe failed, rc = %d", rc);
        return -1;
    }

/*
    int cnt = 0;
    unsigned int msgid = 0;
    while (!toStop)
    {
        MQTTYield(&mclient, 1000);    

        if (++cnt % 5 == 0) {
            MQTTMessage msg = {
                QOS1, 
                0,
                0,
                0,
                "Hello world",
                strlen("Hello world"),
            };
            msg.id = ++msgid;
            rc = MQTTPublish(&mclient, pubTopic, &msg);
            printf("MQTTPublish %d, msgid %d\n", rc, msgid);
        }
    }

    printf("Stopping\n");

    MQTTDisconnect(&mclient);
    NetworkDisconnect(&mnet);

*/
    return 0;
}

void eb_subscribe(char *topic, void *msghandler)
{
}

/*
 * eb_publish() - Edgebox publish a message to IoT hub 
 */
void eb_publish(char *topic, char *content, int size)
{
    int rc;
    static int msgid = 0;
    MQTTMessage msg = {QOS0, 0, 0, 0, "", 0};

    msg.id         = msgid++;
    msg.payload    = content;
    msg.payloadlen = size;

    rc = MQTTPublish(&mclient, topic, &msg);
    if (rc == 0) {
        LOG_DEBUG1("MQTTPublish success, msgid = %d, topic = %s, message = %s", msgid, topic, content);
    } else {
        LOG_ERROR("MQTTPublish failed rc = %d, msgid = %d, payload = %s, payloadlen = %d", rc, msgid, msg.payload, msg.payloadlen);
    }
}

/*
 * eb_report_temp_hum()
 *
 * the function to report weather data. we just print the data into a 
 * string rather than calling the cJSON functions to construct the 
 * json object. this is much quick and simple.
 */
void eb_report_temp_hum(short rawtemp, unsigned short rawhum)
{
    char weather_json[1024];
    float temperature, humidity;

    LOG_DEBUG2("raw temp = %d, raw hum = %x", rawtemp, rawhum);

    temperature = (float)rawtemp / 10.0;
    humidity = (float)rawhum /10.0;
    char weather_format[]="{\"id\":%d,\"params\":{\"loraNodeSerialNo\":\"%s\",\"temperature\":%.1f,\"humidity\":%.1f},\"method\":\"thing.event.property.post\"}";
    sprintf(weather_json, weather_format, msgid_in_payload++, "00000000", temperature, humidity);

    eb_publish(eb_pub_topic, weather_json, strlen(weather_json));
}

void eb_report_wind_dir(int rawvalue)
{
    char weather_json[1024];

    char *dirs[8] = {"北风", "东北风", "东风", "东南风", "南风", "西南风", "西风", "西北风"};
    LOG_DEBUG2("raw value of wind dir = %d", rawvalue);

    char weather_format[]="{\"id\":%d,\"params\":{\"loraNodeSerialNo\":\"%s\",\"windDirection\":\"%s\"},\"method\":\"thing.event.property.post\"}";
    sprintf(weather_json, weather_format, msgid_in_payload++, "00000000", dirs[rawvalue]);

    eb_publish(eb_pub_topic, weather_json, strlen(weather_json));
}

void eb_report_wind_speed(int rawvalue)
{
    char weather_json[1024];
    float windspeed;

    LOG_DEBUG2("raw value of wind speed = %x", rawvalue);

    windspeed = (float)rawvalue / 10.0;

    char weather_format[]="{\"id\":%d,\"params\":{\"loraNodeSerialNo\":\"%s\",\"windSpeed\":%.1f},\"method\":\"thing.event.property.post\"}";
    sprintf(weather_json, weather_format, msgid_in_payload++, "00000000", windspeed);

    eb_publish(eb_pub_topic, weather_json, strlen(weather_json));
}
 

/*
 * eb_report_switch_data_for_loranode()
 *
 * function to report data for each controlled switch on a single Lora node.
 * 
 */
void eb_report_switch_data_for_loranode(char * lorasn, int plc, int start_addr, char *hexdata, int len, int reg_size)
{

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        LOG_ERROR("failed to create root");
        goto end;
    }

    cJSON *id = NULL;
    id = cJSON_CreateNumber(msgid_in_payload++);
    if (id == NULL) {
        LOG_ERROR("failed to create id");
        goto end;
    }
    cJSON_AddItemToObject(root, "id", id);

    cJSON *method = NULL;
    method = cJSON_CreateString("thing.event.property.post");
    if (method == NULL) {
        LOG_ERROR("failed to create method");
        goto end;
    }
    cJSON_AddItemToObject(root, "method", method);

    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
        LOG_ERROR("failed to create params");
        goto end;
    }
    cJSON_AddItemToObject(root, "params", params);

    cJSON *loranodesn = cJSON_CreateString(lorasn);
    if (loranodesn == NULL) {
        LOG_ERROR("failed to create loranodesn");
        goto end;
    }
    cJSON_AddItemToObject(params, "loraNodeSerialNo", loranodesn);


    struct plc_node * p_plc = get_plc_node(plc);
    if (p_plc == NULL) {
        goto end;
    }

    int i;
    for (i = 0; i<(len/reg_size); i++) {
        int addr = start_addr + i;

        int intvalue = (reg_size == 1) ? hexdata[i] : (hexdata[i*2]<<8 + hexdata[i*2+1]);

        char *switch_name = p_plc->reg2name[addr];

        if (switch_name == NULL) continue;

        LOG_DEBUG("adding sw %s",  switch_name);

        cJSON *s = NULL;
        if (strcmp(switch_name, "waterFertilizerPressure") != 0) {
            s = cJSON_CreateNumber(intvalue);
        } else {
            s = cJSON_CreateNumber((float)intvalue/10.0);
        }

        if (s == NULL) {
              LOG_ERROR("failed to create switch %s", switch_name);
            goto end;
        }
        cJSON_AddItemToObject(params, switch_name, s);
    }

    char * json_text =  cJSON_Print(root);

    LOG_DEBUG("eb_report_switch_data_for_loranode(), json text = %s", json_text);


    eb_publish(eb_pub_topic, json_text, strlen(json_text));


end:
    cJSON_Delete(root);
}


/*
 * run_command_on_lora_node()
 *
 * Compose the Modbus frame to write the control command
 */
void run_command_on_lora_node(char *loranodesn, struct switch_node *list_head)
{
    LOG_INFO("run command on loranodesn = %s", loranodesn);

    struct switch_node *p = list_head;
    while (p != NULL) {
        LOG_INFO("switch %s, plc %d, channel %d, value = %d", p->name, p->plc, p->plc_channel, p->v.intv);

        char modbus_write[8];

        modbus_write[0] = p->plc;
        modbus_write[1] = FC_WRITE;
        modbus_write[2] = (p->plc_channel & 0xFF00)>>8;
        modbus_write[3] = p->plc_channel & 0xFF;
        modbus_write[4] = (p->v.intv & 0xFF00)>>8;
        modbus_write[5] = p->v.intv & 0xFF;

        unsigned short crc16 = modbus_CRC16(modbus_write, 6);

        modbus_write[6] = CRC16_LOW_BYTE(crc16);
        modbus_write[7] = CRC16_HIGH_BYTE(crc16);


        char ascii_data[17];
        hex2ascii(modbus_write, ascii_data, 8);

        /* construct the json text */
        char json_text[1024];
        construct_json_text2lora(ascii_data, 2, loranodesn, json_text);

        /*
         * send it via the websocket to the target lora node
         * NOTE: Command from IoT hub has high priority. So set it to 1.
         */
        eb_ws_write(json_text, strlen(json_text), 1);

        p = p->next;
    }

}


