#if !defined(__PARSE_SWITCH_H__)
#define __PARSE_SWITCH_H__


struct switch_node {
    struct switch_node *next;
    char name[64];
    int plc;
    int plc_channel;
    int plc_channel_ro;
    int reg_len;
    int type;
    union {
        int intv;
        float floatv;
    }v;
};

#define  MAX_REG_ADDR 1024
#define  MAX_1BYTE_LEN_REG 511

struct plc_node {
    int addr_485;

    unsigned short channel_1B[MAX_REG_ADDR+1];
    int first_1B;
    int last_1B;

    unsigned short channel_2B[MAX_REG_ADDR+1];
    int first_2B;
    int last_2B;

    /* reg number to switch name mapping */

    char * reg2name[MAX_REG_ADDR + 1];

    struct plc_node *next;
};

extern struct switch_node *switch_list_head;
extern struct plc_node *plc_list_head;

struct switch_node *get_switch_node(char *name);


#endif
