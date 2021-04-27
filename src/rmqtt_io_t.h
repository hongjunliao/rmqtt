/*!
 * This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/3/8
 *
 * RMQTT client 
 * */

#ifndef RMQTT_IO_T_H
#define RMQTT_IO_T_H

#include "redis/src/adlist.h" /* list */
#include "Win32_Interop.h"
#include "redis/src/dict.h"	  /* dict */
#include "hp/sdsinc.h"    /* sds */
#include <hiredis/async.h>
#include "hp/hp_sock_t.h"   /* hp_sock_t */
#include "hp/hp_io_t.h"   /* hp_io_ctx */

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rmqtt_io_t rmqtt_io_t;
typedef struct rmqtt_io_ctx rmqtt_io_ctx;

/* for MQTT client */
struct rmqtt_io_t {
	hp_io_t base;
	sds sid;
	redisAsyncContext * subc; /* for sub/unsub */
	time_t rping;		/* redis ping-pong */
	dict * qos;			/* topic=>QOS */
	list * outlist;     /* pendding out messages */
	listNode * l_msg; 	/* last message sent, from outlist */
	time_t l_time;      /* last send time */
	int l_mid;          /* MQTT msgid */
	rmqtt_io_ctx * ioctx;
};

struct rmqtt_io_ctx {
	hp_io_ctx base;
	redisAsyncContext * c;
	redisAsyncContext * (* redis)();
	int rping_interval; /* redis ping-pong interval */
	uint16_t mqid;		/* mqtt message_id */
};

/////////////////////////////////////////////////////////////////////////////////////////

int rmqtt_io_t_init(rmqtt_io_t * io, rmqtt_io_ctx * ioctx);
int rmqtt_io_t_loop(rmqtt_io_t * c);
void rmqtt_io_t_uninit(rmqtt_io_t * io);

int rmqtt_io_init(rmqtt_io_ctx * ioctx
		, hp_sock_t fd, int tcp_keepalive
		, redisAsyncContext * c, redisAsyncContext * (* redis)()
		, int ping_interval);
int rmqtt_io_append(rmqtt_io_t * client, char const * topic
		, char const * mid, sds message, int flags);

int rmqtt_io_uninit(rmqtt_io_ctx * ioctx);

int rmqtt_io_send_header(rmqtt_io_t * client, uint8_t cmd,
        uint8_t flags, size_t len);

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
#endif /* RMQTT_IO_T_H */
