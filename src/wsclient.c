/*
 * wsclient.c
 *
 *  Created on: 2020/11/22
 *      Author: luojianying
 *
 *  This file implements the websocket client which connects to Lorawan server.
 *  It is the interface to send data to and receive data from Lorawan server.
 *
 *  It has a queue that messages can be buffered before being sent over the websocket.
 *  The reason to buffer the messages is that there will be data conflict and data loss
 *  at the air interface if messages were sent too fast.
 *
 *  For message to run command sent from IoT hub, it should be inserted to head of queue to
 *  get highest priority to be sent to the Lora end devices.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <poll.h>

#include "libwebsockets.h"
#include "edgebox.h"
#include "parse_switch.h"
#include "cJSON.h"
#include "logger.h"

#define WS_TX_BUFFER_LEN    4096
#define WS_RX_BUFFER_LEN    4096

static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;


pthread_t ws_thread_id; 
struct lws *edgebox_ws = NULL;

enum protocols
{
    PROTOCOL_EDGEBOX_WS = 0,
    PROTOCOL_COUNT
};

struct eb_ws_msg {
    char *msg;
    int len;
    struct eb_ws_msg *next;
};

static struct eb_ws_msg *p_msg_q_head = NULL;
static struct eb_ws_msg *p_msg_q_tail = NULL;

/*
 * eb_ws_msgEnQ()
 *
 * insert a message to write into the queue
 *
 * @from_head - priority message should be inserted to head
 */

static void eb_ws_msgEnQ(char *msg, int msg_len, int from_head)
{
    struct eb_ws_msg *msg_node_ptr;

    msg_node_ptr = (struct eb_ws_msg *)malloc(sizeof(struct eb_ws_msg));
    if (msg_node_ptr == NULL)
    {
        LOG_ERROR("eb_ws_msgEnQ(): Memory allocation failed.");
        return;
    }

    char *msg_ptr;
    msg_ptr = (char *)malloc(msg_len + 1);
    if (msg_ptr == NULL) {
        LOG_ERROR("eb_ws_msgEnQ(): Memory allocation failed.");
        free(msg_node_ptr);
        return;
    }
    memset(msg_ptr, 0, msg_len + 1);
    memcpy(msg_ptr, msg, msg_len);

    msg_node_ptr->msg  = msg_ptr;
    msg_node_ptr->len  = msg_len;

    /* lock the queue */
    pthread_mutex_lock(&write_mutex);

    if (from_head) {
        msg_node_ptr->next = p_msg_q_head;
        if (p_msg_q_head == NULL) {
            /* first node in q */
            p_msg_q_tail = msg_node_ptr;
        }
        p_msg_q_head = msg_node_ptr;
    } else {
        msg_node_ptr->next = NULL;
        if (p_msg_q_head == NULL)
        {   /* If queue is empty before insert */
            p_msg_q_head = msg_node_ptr;
        }

        if (p_msg_q_tail != NULL)
        {
            p_msg_q_tail->next = msg_node_ptr;
        }

        // tail always points to the new inserted node
        p_msg_q_tail = msg_node_ptr;
    }

    /* unlock the Queue */
    pthread_mutex_unlock(&write_mutex);
}

/*
 *  eb_ws_msgDeQ()
 *
 *  Always remove a message to write from the queue head.
 */
static void eb_ws_msgDeQ(struct eb_ws_msg **node_ptr)
{

    /* lock the queue */
    pthread_mutex_lock(&write_mutex);

    if (p_msg_q_head != NULL)
    {
        *node_ptr = p_msg_q_head;

        if (p_msg_q_head == p_msg_q_tail)
        {
            p_msg_q_head = NULL;
            p_msg_q_tail = NULL;
        }
        else
        {
            p_msg_q_head = p_msg_q_head->next;
        }
    }

    /* unlock the queue */
    pthread_mutex_unlock(&write_mutex);
}


static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
static struct lws_protocols protocols[] =
{
    {
        "edgebox-websocket-protocol",
        ws_callback,
        0,
        WS_RX_BUFFER_LEN,
    },
    {NULL, NULL, 0, 0} /* terminator */
};

void ws_receive_msg(void *in, size_t len)
{
    LOG_DEBUG("ws_receive_msg(), receiving data = %s, size = %d", in, len);

    char *json_text = (char *)in;
    cJSON *root = cJSON_Parse(json_text);
     if (root == NULL) {
         char *error_ptr = cJSON_GetErrorPtr();
         LOG_ERROR("parse websocket message failed, error = %s", error_ptr);
         goto end;
     }

     cJSON *devaddr = cJSON_GetObjectItemCaseSensitive(root, "devaddr");
     if (devaddr == NULL) {
         LOG_ERROR("no devaddr defined.");
         goto end;
     }

     cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
     if (data == NULL) {
         LOG_ERROR("no data defined.");
         goto end;
     }

/* hard code for test */
     sprintf(devaddr->valuestring, "7076e841");
     sprintf(data->valuestring, "01030400075801FFFF");

     if (strcasecmp(weather_lorasn, devaddr->valuestring) == 0) {
         LOG_DEBUG("This is message from weather station = %s \n", weather_lorasn);
         eb_ws_parse_weather_data(data->valuestring, strlen(data->valuestring));
     } else {
         LOG_DEBUG("This is message from non weather lora = %s \n", devaddr->valuestring);
         eb_ws_parse_non_weather_data(devaddr->valuestring, data->valuestring, strlen(data->valuestring));
     }

end:
     cJSON_Delete(root);
}

/*
 * eb_ws_parse_weather_data()
 *
 * parse the data from weather station censors and call functions defined in mqtt.c
 * to report the weather info to IoT hub.
 */
void eb_ws_parse_weather_data(char *data, int len)
{
    char addr_str[3];
    int addr_485;

    addr_str[0] = data[0];
    addr_str[1] = data[1];
    addr_str[2] = 0;

    addr_485 = strtol(addr_str, NULL, 16);
    LOG_DEBUG1("eb_ws_parse_weather_data(), 485 address in the received data = %d, data = %s", addr_485, data);

    if (addr_485 == temp_humi_485_addr) {
        char hum[5];
        strncpy(hum, &data[6], 4);
        hum[4] = 0;
        unsigned short hum_raw = strtol(hum, NULL, 16);

        char temp[5];
        strncpy(temp, &data[10], 4);
        temp[4] = 0;
        short temp_raw = strtol(temp, NULL, 16);

        eb_report_temp_hum(temp_raw, hum_raw);
    } else if (addr_485 == wind_dir_485_addr) {
        char dir[5];
        strncpy(dir, &data[6], 4);
        dir[5] = 0;
        unsigned short dir_raw = strtol(dir, NULL, 16);

        eb_report_wind_dir(dir_raw);
    } else if (addr_485 == wind_speed_485_addr) {
        char speed[5];
        strncpy(speed, &data[6], 4);
        speed[4] = 0;
        unsigned short speed_raw = strtol(speed, NULL, 16);

        eb_report_wind_speed(speed_raw);
    }
}

void eb_ws_parse_non_weather_data(char *lorasn, char *ascii, int len)
{
    char hex[1024]; /* make it big enough */
    ascii2hex(ascii, hex, len);

    int plc = hex[0];

    int nbytes = hex[2];

    int start_addr = (hex[3] << 8) + hex[4];

    LOG_DEBUG("eb_ws_parse_non_weather_data, hex data = %0x %0x %0x %0x %0x %0x ", hex[0], hex[1], hex[2], hex[3], hex[4], hex[5]);

    int reg_size = (start_addr <= MAX_1BYTE_LEN_REG) ? 1 : 2;
    eb_report_switch_data_for_loranode(lorasn, plc, start_addr, &hex[5], nbytes - 2, reg_size);

}

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    LOG_DEBUG("ws_callback() entered. reason = %d, wsi = %x", reason, wsi);
    switch(reason)
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            /* Handle incoming messages here. */
            ws_receive_msg(in, len);
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:

            if (p_msg_q_head != NULL) {
                struct eb_ws_msg *msg_node;
                char buffer[WS_TX_BUFFER_LEN];
                eb_ws_msgDeQ(&msg_node);
                LOG_DEBUG("msg to write over websocket: %s, len = %d", msg_node->msg, msg_node->len);
                char *p = &buffer[LWS_PRE];
                memcpy(p, msg_node->msg, msg_node->len);
                lws_write(wsi, p, msg_node->len, LWS_WRITE_TEXT);
                free(msg_node->msg);
                free(msg_node);

                /* wait for 3.5s to make sure the air interface can be used again */
                poll(0, 0, 3500);
                lws_callback_on_writable(edgebox_ws);

            }

            break;

        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            break;

        default:
            break;
    }

    return 0;
}

/*
 * eb_ws_write()
 *
 * write a message into the message queue and return immediately.
 * the websocket thread to loop the queue to send the message.
 *
 * @priority - set to 1 for messages that should be sent with priority
 *             it will make the message inserted at the head of message queue
 *             that websocket thread handles.
 */
void eb_ws_write(const char *buf, int len, int priority)
{
    LOG_DEBUG("ws_write() entered.");

    eb_ws_msgEnQ(buf, len, priority);

    lws_callback_on_writable(edgebox_ws);

    LOG_DEBUG("ws_write() exit.");
}


/*
 * ws_evloop_run()
 *
 * Event loop to create and monitor the websocket.
 */
static void *ws_evloop_run(void *arg)
{
    LOG_INFO("ws_evloop_run() entered. ");

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    struct lws_context *context = lws_create_context(&info);

    while(1) {
        /* Connect if we are not connected to the server. */
        if( edgebox_ws == NULL ) {
            LOG_DEBUG("creating edgebox_ws ...");
            struct lws_client_connect_info ccinfo = {0};
            ccinfo.context = context;
            ccinfo.address = "localhost";
            ccinfo.port = 8080;
            ccinfo.path = ws_url; 
            ccinfo.host = lws_canonical_hostname(context);
            ccinfo.origin = "origin";
            ccinfo.protocol = protocols[PROTOCOL_EDGEBOX_WS].name;
            edgebox_ws = lws_client_connect_via_info(&ccinfo);
        }else {
            //LOG_DEBUG2("lws..., edgebox_ws = %x\n", edgebox_ws);
        }

        lws_service(context, 0);
    }

    lws_context_destroy(context);
}

int init_websocket()
{
    int cnt = 0;
    pthread_create(&ws_thread_id, NULL, ws_evloop_run, NULL); 

    while (edgebox_ws == NULL) {
        LOG_DEBUG("edgebox_ws still = NULL, waiting...");
        poll(0, 0, 200);

        cnt++;
        if (cnt > 20) {
            LOG_ERROR("failed to init websocket over 4 seconds.");
            return -1;
        }
    }

    return 0;
}
