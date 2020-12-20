#if !defined(__STATUSQUERY_H__)
#define __STATUSQUERY_H__


#define MAX_LORA_DATA_LEN  20
#define MAX_REG_IN_ONE_READ (MAX_LORA_DATA_LEN/2)


void * status_query_thread(void *);
void send_weather_info_request(int target_addr);

#endif
