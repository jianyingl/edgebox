/*
 * edgebox.c
 *
 * Created on: 2020/11/15
 *     Author: luojianying
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pwd.h>


#include "iniparser.h"
#include "mqtt.h"
#include "edgebox.h"
#include "cJSON.h"
#include "wsclient.h"
#include "statusquery.h"
#include "logger.h"

struct lora_node *lora_list_head = NULL;      /* lora node list */



char *ws_url = NULL; /* websocket url */
char *weather_lorasn = NULL;
int temp_humi_485_addr;
int wind_dir_485_addr;
int wind_speed_485_addr;
char *edgebox_username;



/*
 * readConfig()
 *
 * Load the .ini configuration file
 */
void readConfig(char *conf)
{
    dictionary  *ini ;

    ini = iniparser_load(conf);
    if (ini == NULL) {
        fprintf(stderr, "failed to load the edgebox.ini.\n");
        exit(-1);
    }
    eb_product_key   = iniparser_getstring(ini, "mqtt:edgebox_product_key", NULL);
    if (eb_product_key == NULL) {
        fprintf(stderr, "failed to get eb_product_key\n");
        exit(-1);
    }

    eb_device_name   = iniparser_getstring(ini, "mqtt:edgebox_device_name", NULL);
    eb_device_secret = iniparser_getstring(ini, "mqtt:edgebox_device_secret", NULL);

    fprintf(stderr, "tuple = (%s,%s,%s)\n", eb_product_key, eb_device_name, eb_device_secret);

    /* read lora nodes, and add them into a list */
    const char *loranodes = NULL;
    loranodes = iniparser_getstring(ini, "lora:lora_node_list", NULL);

    if (loranodes != NULL) {
        char *p = strtok(loranodes, ",");
        while(p != NULL) {
            fprintf(stderr, "lora node = %s\n",p);
            struct lora_node * lora = NULL;
            lora = (struct lora_node *)malloc(sizeof(struct lora_node));
            if (lora == NULL) {
                fprintf(stderr, "failed to allocate memory for lora_node\n");
                exit(-1);
            }

            strcpy(lora->name, p);
            lora->next = lora_list_head;
            lora_list_head = lora;
            p = strtok(NULL, ",");
        }
    }

    ws_url = iniparser_getstring(ini, "lora:websocket_url", NULL);
    fprintf(stderr, "ws_url = %s\n", ws_url);

    weather_lorasn = iniparser_getstring(ini, "weather_station:lora_devaddr", NULL);
    temp_humi_485_addr = iniparser_getint(ini, "weather_station:temp_humi_addr", -1);
    wind_dir_485_addr = iniparser_getint(ini, "weather_station:wind_dir_addr", -1);
    wind_speed_485_addr = iniparser_getint(ini, "weather_station:wind_speed_addr", -1);

    fprintf(stderr, "weather_lora = %s, temp_humi_addr = %d, wind_dir_addr = %d\n", weather_lorasn, temp_humi_485_addr, wind_dir_485_addr);

    char *logfile = iniparser_getstring(ini, "logging:logfile", NULL);

    char *loglevel = iniparser_getstring(ini, "logging:loglevel", NULL);

    if (logfile != NULL && logfile[0] == '/') {
        strcpy(s_logFile, logfile);
    }

    if (logfile != NULL && logfile[0] != '/') {
        char cwd[256];
        getcwd(cwd, 256);
        sprintf(s_logFile, "%s/%s", cwd, logfile);
    }

    int l;
    for (l = 0; l < LogLevel_MAXLEVEL; l++) {
        if (strcasecmp(loglevel, log_level_string[l]) == 0) {
            break;
        }
    }

    if (l < LogLevel_MAXLEVEL) {
        s_logLevel = l;
    }

    fprintf(stderr, "s_logFile = %s, s_logLevel = %d\n", s_logFile, s_logLevel);


    edgebox_username = iniparser_getstring(ini, "system:user", NULL);

    /* do not call iniparser_freedict() since the data is still referred */

}



void drop_root_priviledge()
{
    if (edgebox_username == NULL) {
        LOG_ERROR("user is not configured, check .ini file.");
        return;
    }

    /* already non root */
    if (getuid() != 0) {
        return;
    }

    struct passwd *p;
    if ((p = getpwnam(edgebox_username)) == NULL) {
        LOG_ERROR("user %s is not found, check /etc/passwd.", edgebox_username);
        return;
    }

    setuid(p->pw_uid);

}

void eb_signal_handler()
{
    MQTTDisconnect(&mclient);
    NetworkDisconnect(&mnet);
    exit(0);
}

int main(int argc, char **argv)
{

    int rc;
    pthread_t iothubmsg_handler_tid;
    pthread_t status_query_tid;

    /* load configuration */
    readConfig("edgebox.ini");

    /* dont run as root */
    drop_root_priviledge();

    /* init the logging */
    logging_init();

    /* read the PLC configuration for all controlled switches */
    readSwitches("switches.json");

    /* init websocket. a separate thread is created there */
    rc = init_websocket();
    if (rc < 0) {
        LOG_ERROR("Edgebox failed to init websocket.");
        return -1;
    }

    /* create a thread to handle the message from IoT hub */
    rc = pthread_create(&iothubmsg_handler_tid, NULL, iot_msg_handler_thread, NULL);
    if (rc != 0) {
        LOG_ERROR("Edgebox failed to create iot_msg_handler_thread thread");
        return -1;
    }

    rc = mqtt_conn_init();
    if (rc < 0)
    {
        LOG_ERROR("Edgebox failed to init MQTT connection");
        return -1;
    }

    /* create thread to do routine work to report status of weather station and all controlled switches */
    rc = pthread_create(&status_query_tid, NULL, status_query_thread, NULL);
    if (rc != 0) {
        LOG_ERROR("Edgebox failed to create status_query_thread thread");
        return -1;
    }

    /* catch SIGTERM to quit gracefully when systemctl stop */
    signal(SIGTERM, eb_signal_handler);

    /* main thread loop. do some monitoring work */
    while(1) {

        MQTTYield(&mclient, 50);

        if (!MQTTIsConnected(&mclient)) {
            LOG_ERROR("mclient.isconnected = %d ", mclient.isconnected);

            MQTTDisconnect(&mclient);
            NetworkDisconnect(&mnet);

            mqtt_conn_init();

        }

        poll(0 ,0, 50);
    }

    MQTTDisconnect(&mclient);
    NetworkDisconnect(&mnet);

    return 0;
}
