
 /* This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/3/23
 *
 * */

#ifndef RMQTT_SREVER_H
#define RMQTT_SREVER_H

#include "hp/sdsinc.h"
#include "hp/hp_sock_t.h"  /* hp_sock_t */
#include "rmqtt_io_t.h"    /**/
#include "protocol.h"
#ifndef _MSC_VER
#include "mongoose/mongoose.h"
#endif /* _MSC_VER */
///////////////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
extern "C" {
#endif

/* message from Redis */
typedef struct rmqtt_rmsg_t {
	sds payload;
	sds topic; /* which topic is belongs to;  */
	sds mid;    /* message ID */
} rmqtt_rmsg_t;

union hp_iohdr {
	r_mqtt_message mqtt;
};

size_t rmqtt_parse(char * buf, size_t * nbuf, int flags
		, hp_iohdr_t ** iohdrp, char ** bodyp);
int rmqtt_dispatch(rmqtt_io_t * ioctx, hp_iohdr_t * iohdr, char * body);
///////////////////////////////////////////////////////////////////////////////////////

/* Keys hashing / comparison functions for dict.c hash tables. */
uint64_t dictSdsHash(const void *key);
int dictSdsKeyCompare(void *privdata, const void *key1, const void *key2);
void dictSdsDestructor(void *privdata, void *val);

/////////////////////////////////////////////////////////////////////////////////////////

/* callbacks for rmqtt clients */
hp_io_t *  rmqttc_on_new(hp_io_ctx * ioctx, hp_sock_t fd);
int rmqttc_on_parse(hp_io_t * io, char * buf, size_t * len, int flags
		, hp_iohdr_t ** hdrp, char ** bodyp);
int rmqttc_on_dispatch(hp_io_t * io, hp_iohdr_t * imhdr, char * body);
int rmqttc_on_loop(hp_io_t * io);
void rmqttc_on_delete(hp_io_t * io);
/////////////////////////////////////////////////////////////////////////////////////////
int mg_init(struct mg_mgr * mgr, struct mg_timer * t1, struct mg_timer * t2);


#ifndef _MSC_VER
#define rev_init(rev) do { if(uv_loop_init(rev) != 0) { rev = 0; } } while(0)
#define rev_run(rev) do { uv_run(rev, UV_RUN_NOWAIT); } while(0)
#define rev_close(rev) do { uv_loop_close(rev); } while(0)
#else
#define rev_init(rev) do { rev = aeCreateEventLoop(1024 * 10);assert(rev); } while(0)
#define rev_run(rev) do { aeProcessEvents((rev), AE_ALL_EVENTS); } while(0)
#define rev_close(rev) do { aeDeleteEventLoop(rev); } while(0)
#endif /* _MSC_VER */
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
int test_redis_pub_main(int argc, char ** argv);
#endif //NDEBUG


#ifdef __cplusplus
}
#endif

#endif /* RMQTT_SREVER_H */
