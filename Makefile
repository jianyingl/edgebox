CC=/usr/bin/arm-linux-gnueabihf-gcc
#CC=/usr/bin/arm-linux-gnueabihf-g++-4.7

all: edgebox

OBJS+=
OBJS+=aiot_mqtt_sign.o
OBJS+=cJSON.o
OBJS+=crc.o
OBJS+=dictionary.o
OBJS+=edgebox.o
OBJS+=iniparser.o
OBJS+=logger.o
OBJS+=mqtt.o
OBJS+=parse_switch.o
OBJS+=statusquery.o
OBJS+=wsclient.o


LIBS+=
LIBS+=-lpaho-embed-mqtt3cc
LIBS+=-lpaho-embed-mqtt3c
LIBS+=-lpthread
LIBS+=lib/libwebsockets.a

LIBPATH+=
LIBPATH+=/mnt/hgfs/VMware_share/edgebox_dev/lib

INCLUDES+=
INCLUDES+=-I./include

aiot_mqtt_sign.o: src/aiot_mqtt_sign.c
	${CC} ${INCLUDES}  -DMQTTCLIENT_PLATFORM_HEADER=MQTTLinux.h -c src/aiot_mqtt_sign.c

cJSON.o: src/cJSON.c
	${CC} ${INCLUDES} -c src/cJSON.c

crc.o: src/crc.c
	${CC} ${INCLUDES} -c src/crc.c

edgebox.o: src/edgebox.c
	${CC} ${INCLUDES} -DMQTTCLIENT_PLATFORM_HEADER=MQTTLinux.h -c src/edgebox.c

dictionary.o: src/dictionary.c
	${CC} ${INCLUDES} -c src/dictionary.c

iniparser.o: src/iniparser.c
	${CC} ${INCLUDES} -c src/iniparser.c

logger.o: src/logger.c
	${CC} ${INCLUDES} -c src/logger.c
	
mqtt.o: src/mqtt.c
	${CC} ${INCLUDES} -DMQTTCLIENT_PLATFORM_HEADER=MQTTLinux.h -c src/mqtt.c

parse_switch.o: src/parse_switch.c
	${CC} ${INCLUDES} -c src/parse_switch.c

statusquery.o: src/statusquery.c
	${CC} ${INCLUDES} -DMQTTCLIENT_PLATFORM_HEADER=MQTTLinux.h -c src/statusquery.c

wsclient.o: src/wsclient.c
	${CC} ${INCLUDES} -c src/wsclient.c

edgebox: $(OBJS)
	${CC} -L $(LIBPATH) $(OBJS) $(LIBS) -o edgebox

clean:
	rm -f *.o edgebox
