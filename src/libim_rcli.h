/*!
 * This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/3/8
 *
 * MQTT client for libim
 * */

#ifndef RMQTT_RCLI_LIBIM_H
#define RMQTT_RCLI_LIBIM_H

#ifndef _MSC_VER

#include "sds/sds.h"    /* sds */
#include <hiredis/async.h>
#include "hp/hp_libim.h"   /*  */
#include "redis/src/dict.h"			  /* dict */
#include "redis/src/adlist.h" /* list */

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////

/* message from Redis */
typedef struct libim_msg_t {
	sds payload;
	sds topic; /* which topic is belongs to;  */
	sds mid;    /* message ID */
} libim_msg_t;

/* for MQTT client */
typedef struct libim_rcli {
	libim_cli base;
	/* for sub/unsub */
	redisAsyncContext * subc;
	time_t rping;		/* redis ping-pong */
	dict * qos;			/* topic=>QOS */
	list * outlist;     /* pendding out messages */
	listNode * l_msg; 	/* last message sent, from outlist */
	time_t l_time;      /* last send time */
	int l_mid;          /* MQTT msgid */
} libim_rcli;
/////////////////////////////////////////////////////////////////////////////////////////

typedef struct libim_rctx {
	libim_ctx base;
	redisAsyncContext * c;
	redisAsyncContext * (* redis)();
	int rping_interval; /* redis ping-pong interval */
	uint16_t mqid;		/* mqtt message_id */
} libim_rctx;

int libim_rctx_init(libim_rctx * ctx, hp_epoll * efds
		, int fd, int tcp_keepalive
		, redisAsyncContext * c, redisAsyncContext * (* redis)()
		, int ping_interval);
int libim_rcli_append(libim_rcli * client, char const * topic
		, char const * mid, sds message, int flags);

void libim_rctx_uninit(libim_rctx * ctx);

int libim_mqtt_send_header(libim_rcli * client, uint8_t cmd,
        uint8_t flags, size_t len);

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
#endif /* _MSC_VER */
#endif /* RMQTT_RCLI_LIBIM_H */
