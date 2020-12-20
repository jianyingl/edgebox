/*
 * statusquery.c
 *
 * Created on: 2020/11/21
 *     Author: luojianying
 *
 * This file implements the status query thread. It loops to send the weather station data request
 * and all state request of various switches. The websocket thread will receive the reply, parse
 * and then publish to IoT hub.
 *
 */

#include "edgebox.h"
#include "crc.h"
#include "cJSON.h"
#include "logger.h"
#include "parse_switch.h"
#include "statusquery.h"
#include "mqtt.h"


/*
 *
 */
void * status_query_thread(void *arg)
{
    while (1) {

        //loop_each_lora_each_plc_each_switch();
        poll(0,0,6000);

        //send_weather_info_request(temp_humi_485_addr);
        poll(0,0,6000);

        send_weather_info_request(wind_dir_485_addr);
        poll(0,0,6000);

    }
}

void loop_each_lora_each_plc_each_switch()
{

    struct lora_node *p_lora = lora_list_head;
    while (p_lora != NULL) {
        struct plc_node *p_plc = plc_list_head;
        while (p_plc != NULL) { /* for each plc */

            /* handle 1byte-long reg */
            if (p_plc->first_1B < (MAX_REG_ADDR + 1)) {
                int start_reg = p_plc->first_1B;
                int end_reg = p_plc->last_1B;

                while (start_reg < (MAX_REG_ADDR + 1)) {
                    int i;
                    int reg_continuous = 0;
                    for (i = start_reg; reg_continuous < MAX_REG_IN_ONE_READ && i<=MAX_REG_ADDR; i++) {
                        if (p_plc->channel_1B[i] == 1) {
                            reg_continuous++;
                        } else {
                            break;
                        }
                    }
                    /* start_reg --- i-1, continuous */
                    /* send a query request */
                    LOG_DEBUG("status query %s, plc addr = %d, from reg %d to reg = %d",
                                   p_lora->name, p_plc->addr_485, start_reg, i-1);

                    run_single_switch_query_command(p_lora->name, p_plc->addr_485, start_reg, i - start_reg);
                    poll(0, 0, 4000);

                    /* look for start of next continuous reg block */
                    int next_reg;
                    for (next_reg = i; next_reg <= MAX_REG_ADDR; next_reg++) {
                        if (p_plc->channel_1B[next_reg] == 1) {
                            break;
                        }
                    }
                    start_reg = next_reg;

                }
            }

            /* handle 2byte-long reg */
            if (p_plc->first_2B < (MAX_REG_ADDR + 1)) {
                int start_reg = p_plc->first_2B;
                int end_reg = p_plc->last_2B;

                while (start_reg < (MAX_REG_ADDR + 1)) {
                    int i;
                    int reg_continuous = 0;
                    for (i = start_reg; reg_continuous < MAX_REG_IN_ONE_READ && i<=MAX_REG_ADDR; i++) {
                        if (p_plc->channel_2B[i] == 1) {
                            reg_continuous++;
                        } else {
                            break;
                        }
                    }
                    /* start_reg --- i-1, continuous */
                    /* send a query request */
                    LOG_DEBUG("status query %s, plc addr = %d, from reg %d to reg = %d",
                                   p_lora->name, p_plc->addr_485, start_reg, i-1);

                    run_single_switch_query_command(p_lora->name, p_plc->addr_485, start_reg, i - start_reg);
                    poll(0, 0, 4000);

                    /* look for start of next continuous reg block */
                    int next_reg;
                    for (next_reg = i; next_reg <= MAX_REG_ADDR; next_reg++) {
                        if (p_plc->channel_2B[next_reg] == 1) {
                            break;
                        }
                    }
                    start_reg = next_reg;

                }
            }

            p_plc = p_plc->next;
        }

        p_lora = p_lora->next;
    }
}

void run_single_switch_query_command(char* lorasn, int plc, int start_reg, int num_reg)
{
    char modbus_read[8];

    modbus_read[0] = plc;
    modbus_read[1] = FC_READ;
    modbus_read[2] = (start_reg & 0xFF00)>>8;
    modbus_read[3] = (start_reg) & 0xFF;
    modbus_read[4] = (num_reg & 0xFF00)>>8;
    modbus_read[5] = num_reg & 0xFF;

    unsigned short crc16 = modbus_CRC16(modbus_read, 6);

    modbus_read[6] = CRC16_LOW_BYTE(crc16);
    modbus_read[7] = CRC16_HIGH_BYTE(crc16);


    char ascii_data[17];
    hex2ascii(modbus_read, ascii_data, 8);

    /* construct the json text */
    char json_text[1024];
    construct_json_text2lora(ascii_data, 2, lorasn, json_text);

    /* send it via the websocket to the target lora node */
    eb_ws_write(json_text, strlen(json_text), 0);
}

/*
 * construct_json_text2lora()
 *
 * Construct the json text that is used to send data to lorawan server via websocket.
 * The Lorawan server expects to receive json format data like below:
 *
 *     {"data":"020300000002C438","devaddr":"7076e841","port":2,"time":"immediately"}
 *
 *     The 'data' field is the real data that will be transmitted over the air to the end device.
 */
void construct_json_text2lora(char *ascii_data, int port_num, char *lorasn, char *json_output)
{
    /* construct the json text */
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        LOG_ERROR("failed to create root");
        goto end;
    }

    cJSON *data = NULL;
    data = cJSON_CreateString(ascii_data);
    if (data == NULL) {
        LOG_ERROR("failed to create data");
        goto end;
    }
    cJSON_AddItemToObject(root, "data", data);

    cJSON *devaddr = NULL;
    devaddr = cJSON_CreateString(lorasn);
    if (devaddr == NULL) {
        LOG_ERROR("failed to create devaddr");
        goto end;
    }
    cJSON_AddItemToObject(root, "devaddr", devaddr);

    cJSON *port = NULL;
    port = cJSON_CreateNumber(port_num);
    if (port == NULL) {
        LOG_ERROR("failed to create port");
        goto end;
    }
    cJSON_AddItemToObject(root, "port", port);

    cJSON *time = NULL;
    time = cJSON_CreateString("immediately");
    if (time == NULL) {
        LOG_ERROR("failed to create time");
        goto end;
    }
    cJSON_AddItemToObject(root, "time", time);

    /* send it via the websocket to the target lora node */
    char *json_text = cJSON_Print(root);
    strcpy(json_output, json_text);

end:
    cJSON_Delete(root);
}

/*
 * send_weather_info_request()
 *
 * send the request to read information from weather station.
 *
 * target_addr  -  the 485 address for temperature/humidity, wind censors
 */
void send_weather_info_request(int target_addr)
{
    if (weather_lorasn == NULL) return;

    char modbus_read[8];

    modbus_read[0] = target_addr;
    modbus_read[1] = FC_READ;
    modbus_read[2] = 0x00;
    modbus_read[3] = 0x00;
    modbus_read[4] = 0x00;
    modbus_read[5] = 0x02;

    unsigned short crc16 = modbus_CRC16(modbus_read, 6);

    modbus_read[6] = CRC16_LOW_BYTE(crc16);
    modbus_read[7] = CRC16_HIGH_BYTE(crc16);

    char ascii_data[17];
    hex2ascii(modbus_read, ascii_data, 8);

    /* construct the json text */
    char json_text[1024];
    construct_json_text2lora(ascii_data, 2, weather_lorasn, json_text);

    /* send it via the websocket to the weather station lora node */
    eb_ws_write(json_text, strlen(json_text), 0);

}

/*
 * hex2ascii()
 *
 * Convert the binary data in hex to readable ascii characters.
 * e.g. the data frame to control a device 010600020001E9CA should
 * be converted to a char array "010600020001E9CA" to be transmitted
 * in json text to Lorawan server.
 */
void hex2ascii(char *hex, char *ascii, int hexlen )
{
    int i,j;
    for (i = 0, j = 0; i < hexlen; i++) {
         char hiHalf = hex[i] >> 4;
         char loHalf = hex[i] & 0x0F;
         ascii[j++] = (hiHalf <= 9) ? (hiHalf + '0') : (hiHalf - 10 + 'A');
         ascii[j++] = (loHalf <= 9) ? (loHalf + '0') : (loHalf - 10 + 'A');
    }

    /* the ascii[] must be long enough to hold this \0 terminator */
    ascii[j] = 0;
}

/*
 * ascii2hex()
 *
 * DO the reverse as above
 */

void ascii2hex(char *ascii, char *hex, int asciilen)
{
    int i;

    for (i = 0; i < asciilen;) {
        char tmp[3];
        tmp[0] = ascii[i++];
        tmp[1] = ascii[i++];
        tmp[2] = 0;

        hex[i/2 - 1] = strtol(tmp, NULL, 16);
    }
}
