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
#include "protocol.h"

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
	hp_io_ctx * ioctx;
	hp_io_t listenio;
	redisAsyncContext * c;
	redisAsyncContext * (* redis)();
	int rping_interval; /* redis ping-pong interval */
	uint16_t mqid;		/* mqtt message_id */
};

/////////////////////////////////////////////////////////////////////////////////////////
/* message from Redis */
typedef struct rmqtt_rmsg_t {
	sds payload;
	sds topic; /* which topic is belongs to;  */
	sds mid;    /* message ID */
} rmqtt_rmsg_t;

union hp_iohdr {
	r_mqtt_message mqtt;
};

/////////////////////////////////////////////////////////////////////////////////////////

int rmqtt_io_init(rmqtt_io_ctx * rmqtt, hp_io_ctx * ioctx
	, hp_sock_t fd, int tcp_keepalive
	, redisAsyncContext * c, redisAsyncContext * (*redis)()
	, int ping_interval);
int rmqtt_io_run(rmqtt_io_ctx * ioctx, int interval);
int rmqtt_io_uninit(rmqtt_io_ctx * ioctx);

/////////////////////////////////////////////////////////////////////////////////////////

int rmqtt_io_send_header(rmqtt_io_t * client, uint8_t cmd,
        uint8_t flags, size_t len);

///////////////////////////////////////////////////////////////////////////////////////

/* Keys hashing / comparison functions for dict.c hash tables. */
uint64_t r_dictSdsHash(const void *key);
int r_dictSdsKeyCompare(void *privdata, const void *key1, const void *key2);
void r_dictSdsDestructor(void *privdata, void *val);
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_redis_pub_main(int argc, char ** argv);
#endif //NDEBUG

/////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
#endif /* RMQTT_IO_T_H */
