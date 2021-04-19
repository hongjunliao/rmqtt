/*!
* This file is PART of rmqtt project
* @date 2018/4/8
*
* MQTT protocol
* */

#ifndef RMQTT_PROTOCOL_H
#define RMQTT_PROTOCOL_H

#include <stddef.h>
#include <string.h>
#include "hp/hp_libc.h"
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

#ifndef uint8_t
typedef unsigned char uint8_t;
#endif

#ifndef uint8_t
typedef unsigned short uint16_t;
#endif

/* Describes chunk of memory */
struct rmqtt_str {
  const char *p; /* Memory chunk pointer */
  size_t len;    /* Memory chunk length */
};

typedef struct r_mqtt_message {
  int cmd;
  int qos;
  int len; /* message length in the IO buffer */
  struct rmqtt_str topic;
  struct rmqtt_str payload;

  uint8_t connack_ret_code; /* connack */
  uint16_t message_id;      /* puback */

  /* connect */
  uint8_t protocol_version;
  uint8_t connect_flags;
  uint16_t keep_alive_timer;
  struct rmqtt_str protocol_name;
  struct rmqtt_str client_id;
  struct rmqtt_str will_topic;
  struct rmqtt_str will_message;
  struct rmqtt_str user_name;
  struct rmqtt_str password;
} r_mqtt_message;

/////////////////////////////////////////////////////////////////////////////////////////
// for MQTT protocol

/* Message types */
#define MG_MQTT_CMD_CONNECT 1
#define MG_MQTT_CMD_CONNACK 2
#define MG_MQTT_CMD_PUBLISH 3
#define MG_MQTT_CMD_PUBACK 4
#define MG_MQTT_CMD_PUBREC 5
#define MG_MQTT_CMD_PUBREL 6
#define MG_MQTT_CMD_PUBCOMP 7
#define MG_MQTT_CMD_SUBSCRIBE 8
#define MG_MQTT_CMD_SUBACK 9
#define MG_MQTT_CMD_UNSUBSCRIBE 10
#define MG_MQTT_CMD_UNSUBACK 11
#define MG_MQTT_CMD_PINGREQ 12
#define MG_MQTT_CMD_PINGRESP 13
#define MG_MQTT_CMD_DISCONNECT 14

/* MQTT event types */
#define MG_MQTT_EVENT_BASE 200
#define MG_EV_MQTT_CONNECT (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_CONNECT)
#define MG_EV_MQTT_CONNACK (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_CONNACK)
#define MG_EV_MQTT_PUBLISH (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_PUBLISH)
#define MG_EV_MQTT_PUBACK (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_PUBACK)
#define MG_EV_MQTT_PUBREC (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_PUBREC)
#define MG_EV_MQTT_PUBREL (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_PUBREL)
#define MG_EV_MQTT_PUBCOMP (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_PUBCOMP)
#define MG_EV_MQTT_SUBSCRIBE (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_SUBSCRIBE)
#define MG_EV_MQTT_SUBACK (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_SUBACK)
#define MG_EV_MQTT_UNSUBSCRIBE (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_UNSUBSCRIBE)
#define MG_EV_MQTT_UNSUBACK (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_UNSUBACK)
#define MG_EV_MQTT_PINGREQ (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_PINGREQ)
#define MG_EV_MQTT_PINGRESP (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_PINGRESP)
#define MG_EV_MQTT_DISCONNECT (MG_MQTT_EVENT_BASE + MG_MQTT_CMD_DISCONNECT)

/* Message flags */
#define MG_MQTT_RETAIN 0x1
#define MG_MQTT_QOS(qos) ((qos) << 1)
#define MG_MQTT_GET_QOS(flags) (((flags) &0x6) >> 1)
#define MG_MQTT_SET_QOS(flags, qos) (flags) = ((flags) & ~0x6) | ((qos) << 1)
#define MG_MQTT_DUP 0x8

/* Connection flags */
#define MG_MQTT_CLEAN_SESSION 0x02
#define MG_MQTT_HAS_WILL 0x04
#define MG_MQTT_WILL_RETAIN 0x20
#define MG_MQTT_HAS_PASSWORD 0x40
#define MG_MQTT_HAS_USER_NAME 0x80
#define MG_MQTT_GET_WILL_QOS(flags) (((flags) &0x18) >> 3)
#define MG_MQTT_SET_WILL_QOS(flags, qos) \
  (flags) = ((flags) & ~0x18) | ((qos) << 3)

/* CONNACK return codes */
#define MG_EV_MQTT_CONNACK_ACCEPTED 0
#define MG_EV_MQTT_CONNACK_UNACCEPTABLE_VERSION 1
#define MG_EV_MQTT_CONNACK_IDENTIFIER_REJECTED 2
#define MG_EV_MQTT_CONNACK_SERVER_UNAVAILABLE 3
#define MG_EV_MQTT_CONNACK_BAD_AUTH 4
#define MG_EV_MQTT_CONNACK_NOT_AUTHORIZED 5

#ifdef __cplusplus
}
#endif


#endif /* RMQTT_PROTOCOL_H */
