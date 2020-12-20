/*
 * parse_switch.c
 *
 *  Created on: 2020/11/24
 *      Author: luojianying
 *
 *  The file implements the switches.json parsing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>


#include "cJSON.h"
#include "logger.h"
#include "parse_switch.h"

/*
 * switch_list_head is used to get the PLC/register address when
 * parse the IoT command message.
 */
struct switch_node *switch_list_head = NULL;


/*
 * plc_list_head is used to get to know the switch name when
 * a reply message is received via websocket.
 */
struct plc_node *plc_list_head = NULL;



void add_switch_to_list(struct switch_node **head, struct switch_node *p)
{
    p->next = (struct switch_node *)*head;
    *head = p;
}


struct switch_node *get_switch_node(char *name)
{
    struct switch_node *p = switch_list_head;
    while (p != NULL) {
        if (strcmp(p->name, name) == 0) {
            return p;
        }
        p = p->next;
    }

    return NULL;
}

void free_switch_list(struct switch_node **head)
{
    while (*head != NULL) {
        struct switch_node *next = (*head)->next;
        free(*head);
        *head = next;
    }
}

void add_plc_to_list(struct plc_node **head, struct plc_node *p)
{
    p->next = (struct plc_node *)*head;
    *head = p;
}

struct plc_node *get_plc_node(int addr_485)
{
    struct plc_node *p = plc_list_head;
    while (p != NULL) {
        if (p->addr_485 == addr_485) {
            return p;
        }
        p = p->next;
    }

    return NULL;
}




void readSwitches(char *switches_json_file)
{
    struct stat st;
    int rc;
    rc = stat(switches_json_file, &st);
    if (rc < 0) {
        LOG_ERROR("failed to get file %s", switches_json_file);
        exit(-1);
    }

    int fd = open(switches_json_file, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("failed to open file %s", switches_json_file);
        exit(-1);
    }

    char *buf = NULL;
    buf = malloc(st.st_size + 1);
    if (buf == NULL) {
        LOG_ERROR("failed to allocate memory");
        exit(-1);
    }

    memset(buf, 0, st.st_size + 1);
    read(fd, buf, st.st_size);
    LOG_DEBUG2("the switch json file content: %s", buf);
    close(fd);

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        char *error_ptr = cJSON_GetErrorPtr();
        LOG_ERROR("Failed to parse the json file, error =%s", error_ptr);
    }

    cJSON *switches = cJSON_GetObjectItemCaseSensitive(root, "switches");
    if (switches == NULL) {
        LOG_ERROR("no switches defined.");
        return;
    }


    /*
     * loop each switch defined in the json file. it creates a switch list which is used
     * to search the PLC 485 address and the register address of a switch.
     *
     * a plc_node list will be created too. so when a reply message is received over the websocket,
     * the PLC 485 addr/register addr will be mapped to switch name. the plc_node also contains a
     * bitmap of registers. so it can send status query command for a batch of registers in one message
     * to lora device.
     */

    cJSON *sw = NULL;
    cJSON_ArrayForEach(sw, switches)
    {
        cJSON *name           = cJSON_GetObjectItemCaseSensitive(sw, "name");
        cJSON *plc            = cJSON_GetObjectItemCaseSensitive(sw, "plc");
        cJSON *plc_channel    = cJSON_GetObjectItemCaseSensitive(sw, "plc_channel");
        cJSON *plc_channel_ro = cJSON_GetObjectItemCaseSensitive(sw, "plc_channel_ro");
        cJSON *reg_len        = cJSON_GetObjectItemCaseSensitive(sw, "reg_len");

        int plcnum = (int)strtol(plc->valuestring, NULL, 16);
        int channel = (int)strtol(plc_channel->valuestring, NULL, 16);
        int ro_channel = (plc_channel_ro == NULL) ? -1 : strtol(plc_channel_ro->valuestring, NULL, 16);
        int len = (int)strtol(reg_len->valuestring, NULL, 16);

        LOG_DEBUG2("sw: %s, %s, %s, %d, %d, %d, %d", name->valuestring, plc->valuestring, plc_channel->valuestring, plcnum, channel, ro_channel, len);

        /* create a switch_node node for global switch list */
        struct switch_node *s = NULL;
        s = malloc(sizeof(struct switch_node));
        if (s == NULL) {
            LOG_ERROR("failed to allocate memory for switch_node");
            exit(-1);
        }

        strcpy(s->name, name->valuestring);
        s->plc             = plcnum;
        s->plc_channel     = channel;
        s->plc_channel_ro  = ro_channel;
        s->reg_len         = len;
        s->next            = NULL;

        add_switch_to_list(&switch_list_head, s);

        struct plc_node *p;
        p = get_plc_node(plcnum);
        if (p == NULL) {
            struct plc_node *np = malloc(sizeof(struct plc_node));
            if (np == NULL) {
                LOG_ERROR("failed to allocate memory for plc_node");
                exit(-1);
            }
            memset(np, 0, sizeof(struct plc_node));
            add_plc_to_list(&plc_list_head, np);
            p = np;
            p->addr_485 = plcnum;
            p->first_1B = MAX_REG_ADDR + 1;
            p->last_1B  = -1;
            p->first_2B = MAX_REG_ADDR + 1;
            p->last_2B  = -1;
        }



        /* if readonly channel is defined for a switch, use it */
        if (ro_channel >= 0) {
            channel = ro_channel;
        }

        int sw_name_len = strlen(name->valuestring);

        char *sw_name = malloc(sw_name_len);
        if (sw_name == NULL) {
            LOG_ERROR("failed to allocate memory for plc_node");
            exit(-1);
        }

        strcpy(sw_name, name->valuestring);

        p->reg2name[channel] = sw_name;

        if (len == 1) {
            if (channel <= p->first_1B) {
                p->first_1B = channel;
            }
            if (channel >= p->last_1B) {
                p->last_1B = channel;
            }

            p->channel_1B[channel] = 1;
        }

        if (len == 2) {
            if (channel <= p->first_2B) {
                p->first_2B = channel;
            }
            if (channel >= p->last_2B) {
                p->last_2B = channel;
            }

            p->channel_2B[channel] = 1;
        }

    } /* each sw */


    struct plc_node *p = plc_list_head;
    while (p != NULL) {
        LOG_DEBUG("485 addr = %d, first1B = %d, last1B = %d, first2B = %d, last2B = %d", p->addr_485, p->first_1B, p->last_1B, p->first_2B, p->last_2B);
        p = p->next;
    }

    LOG_INFO("Edgebox parsed the switch json file %s", switches_json_file);

end:
    cJSON_Delete(root);
}
