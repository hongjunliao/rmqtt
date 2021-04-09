/*!
 *  This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2021/3/18
 *
 * */
#ifndef REDIS_PB_H__
#define REDIS_PB_H__

#include "hp/hp_pub.h"
#include "libim_rcli.h"

#ifdef __cplusplus
extern "C" {
#endif
/////////////////////////////////////////////////////////////////////////////////////////

int redis_pub(redisAsyncContext * c, char const * topic, char const * msg, int len
		, int flags
		, redisCallbackFn done);


redisAsyncContext * redis_subc(redisAsyncContext * c, redisAsyncContext * subc
		, char const * id
		, hp_sub_cb_t cb
		, void * arg
		);
redisAsyncContext * redis_subc_arg(redisAsyncContext * c, redisAsyncContext * subc
		, char const * id
		, hp_sub_cb_t cb
		, hp_sub_arg_t arg
		);
int redis_sub(libim_rcli * client, int n_topic, char * const* topic, uint8_t * qoss);

int redis_sup(redisAsyncContext * c
		, char const * id
		, int flags
		, char const * mid
		, redisCallbackFn done);

int redis_sup_by_topic(redisAsyncContext * c
		, char const * id
		, char const * topic
		, char const * mid
		, redisCallbackFn done);

/**
 * update session: add/remove topic
 */
int redis_sub_sadd(redisAsyncContext * c, char const * id, char const * topicstr);
int redis_sub_sremove(redisAsyncContext * c, char const * id, char const * topicstr);

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

#endif

#ifdef __cplusplus
}
#endif

#endif /* REDIS_PB_H__ */
