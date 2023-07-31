
/*!
 *  This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2021/3/18
 *
 * 1.about topic_id/TID, e.g.: "rmqtt:1:865452044887154"
 *
 * |rmqtt        :1           : 865452044887154         |
 * |topic_prefix | topic_type | topic_postfix(client_id/CID)|
 *
 * 2.all clients both subscribe 1 basic topic:
 *
 * ${topic_prefix}:1:${client_id}  # for a specific client
 *
 * 3.about session_id/SID, e.g.: "rmqtt:s:865452044887154"
 *
 * ${topic_prefix}:s:${client_id}
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

//#include "Win32_Interop.h"
#include "redis/src/adlist.h" /* list */

#include <time.h>
#ifndef _MSC_VER
#include <sys/time.h>
#endif

#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "hp/sdsinc.h"
#include "hp/string_util.h" /* hp_vercmp */
//#include "uuid/uuid.h"
#include "c-vector/cvector.h"
#include "hp/hp_pub.h"
#include "hp/hp_config.h"	/* hp_config_t */
#include "hp/hp_log.h"
#include "redis_pub.h"
#include "redis/src/version.h" /*REDIS_VERSION*/

extern hp_config_t g_rmqtt_conf;

static uint8_t s_qoss[3] = {0, 1, 2};
/////////////////////////////////////////////////////////////////////////////////////////

static sds internal_TID_gen(char const * topic_prefix, char const * id, int flags)
{
	assert(topic_prefix && id);

	char const * s = "1";
	if(flags & 8) s = "group";
	else               s = "1";

	return sdscatfmt(sdsempty(), "%s:%s:%s", topic_prefix, s, id);
}

char const * internal_do_get_postfix_from_TID(char const * topic_prefix, char const * topic, int * flags)
{
	assert(topic_prefix && topic);

	int i;
	int f = 0;
	if(!flags)
		flags = &f;

	char const * id = topic;

	char const * patterns[] = { "%s:1:", "%s:group:" };
	int fs[] = { 0, 8 };

	for(i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i){

		sds s = sdscatfmt(sdsempty(), patterns[i], topic_prefix);

		if(strncmp(s, topic, sdslen(s)) == 0){
			*flags = fs[i];
			id = topic + sdslen(s);

			sdsfree(s);
			break;
		}
		sdsfree(s);
	}

	return id;
}

static char const * internal_get_postfix_from_TID(char const * id, int * flags)
{
	return id? internal_do_get_postfix_from_TID(g_rmqtt_conf("redis.topic"), id, flags) : id;
}

static char const * internal_get_CID_from_SID(char const * sid)
{
	if(!sid)
		return sid;

	char const * id = sid;

	sds pattern = sdscatfmt(sdsempty(), "%s:s:", g_rmqtt_conf("redis.topic"));
	if(strncmp(pattern, sid, sdslen(pattern)) == 0)
		id = sid + sdslen(pattern);

	sdsfree(pattern);
	return id;
}

/**
 * pub
 * */

int redis_pub(redisAsyncContext * c, char const * id, char const * msg, int len
		, int flags
		, redisCallbackFn done)
{
	if(!(c && id && msg))
		return -1;

	int rc;

	sds topic = internal_TID_gen(g_rmqtt_conf("redis.topic"), id, flags);
	rc = hp_pub(c, topic, msg, len, done);
	sdsfree(topic);

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 * sub
 * */

redisAsyncContext * redis_subc_arg(redisAsyncContext * c, redisAsyncContext * subc
		, char const * id
		, hp_sub_cb_t cb
		, hp_sub_arg_t arg
		)
{
	if(!(c && subc && id))
		return 0;

	int rc;

	sds sid = sdscatfmt(sdsempty(), "%s:s:%s", g_rmqtt_conf("redis.topic"), id);

	/* make sure session exits for this id */
	sds idtopic = internal_TID_gen(g_rmqtt_conf("redis.topic"), id, 0);
	rc = redisAsyncCommand(c, 0, 0/* privdata */, "hsetnx %s %s %s", sid, idtopic, "0");

	subc = hp_subc_arg(c, subc, g_rmqtt_conf("redis.shasub"), g_rmqtt_conf("redis.shasup"), sid, cb, arg);

	sdsfree(idtopic);
	sdsfree(sid);

	HP_UNUSED(rc);
	return subc;
}

redisAsyncContext * redis_subc(redisAsyncContext * c, redisAsyncContext * subc
		, char const * id
		, hp_sub_cb_t cb
		, void * arg
		)
{
	hp_sub_arg_t a = { arg, 0 };
	return redis_subc_arg(c, subc, id, cb, a);
}

int redis_sub(rmqtt_io_t * client, int n_topic, char * const* topic, uint8_t * qoss)
{
	if(!(client && client->subc && client->subc->data))
		return -1;

	int i, rc;

	sds * topics = 0;
	cvector_init(topics, 1);

	if (topic && n_topic > 0) {
		sds t = internal_TID_gen(g_rmqtt_conf("redis.topic"), client->sid, 0);
		cvector_push_back(topics, t);
	}
	/* others */
	for(i = 0; i < n_topic; ++i){
		char * key = topic[i];
		if(!(key && strlen(key) > 0))
			continue;

		sds t = internal_TID_gen(g_rmqtt_conf("redis.topic"), key, 8);
		cvector_push_back(topics, t);
		/* save QOS */
		dictEntry * ent, *existing = 0;
		ent = dictAddRaw(client->qos, t, &existing);
		if (!ent) {
			ent = existing;
			sdsfree(t);
		}
		if (ent) { dictSetUnsignedIntegerVal(ent, (qoss ? qoss[i] : s_qoss[2])); }

		/* NOTE: NOT freed here */
		/*sdsfree(t);*/
	}

	rc = hp_sub(client->subc, cvector_size(topics), topics);

	cvector_free(topics);
	return rc;
}

static void redis_sup_cb(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r;

	if(!(reply && reply->type != REDIS_REPLY_ERROR)){

		hp_log(stderr, "%s: redis HSET failed, err/errstr=%d/'%s'/'%s'\n"
				, __FUNCTION__, c->err, (reply? reply->str : ""), c->errstr);;
	}
}

int redis_sup_by_topic(redisAsyncContext * c
		, char const * id
		, char const * topic
		, char const * mid
		, redisCallbackFn done)
{
	if(!(c && id && mid))
		return -1;

	sds sid = sdscatfmt(sdsempty(), "%s:s:%s", g_rmqtt_conf("redis.topic"), id);
	int rc = redisAsyncCommand(c, (done? done : redis_sup_cb), 0/* privdata */, "hset %s %s %s", sid, topic, mid);

	sdsfree(sid);
	return rc;
}


int redis_sup(redisAsyncContext * c
		, char const * id
		, int flags
		, char const * mid
		, redisCallbackFn done)
{
	if(!(c && id && mid))
		return -1;

	sds topic = internal_TID_gen(g_rmqtt_conf("redis.topic"), id, flags);
	sds sid = sdscatfmt(sdsempty(), "%s:s:%s", g_rmqtt_conf("redis.topic"), id);
	int rc = redisAsyncCommand(c, (done ? done : redis_sup_cb), 0/* privdata */, "hset %s %s %s", sid, topic, mid);
	assert(rc == 0);

	sdsfree(sid);
	sdsfree(topic);

	return rc;
}

static void def_cb(redisAsyncContext *c, void *r, void *privdata)
{
	redisReply * reply = (redisReply *)r;

	if(!(reply && reply->type != REDIS_REPLY_ERROR)){

		hp_log(stderr, "%s: redis %s failed, err/errstr=%d/'%s'/'%s'\n"
				, __FUNCTION__, (char *)privdata, c->err, (reply? reply->str : ""), c->errstr);;
	}
}

int redis_sub_sadd(redisAsyncContext * c, char const * id, char const * topicstr)
{
	if(!(c && id && topicstr))
		return -1;

	int rc;
	sds sid = sdscatfmt(sdsempty(), "%s:s:%s", g_rmqtt_conf("redis.topic"), id);
	sds topic = internal_TID_gen(g_rmqtt_conf("redis.topic"), topicstr, 8);

	const char *argv[8] = {"evalsha", g_rmqtt_conf("redis.shasadd"), "1", topic, sid};

	int argc = 5;
	rc = redisAsyncCommandArgv(c, def_cb, (void *)argv[0]/* privdata */, argc, argv, 0);

	sdsfree(sid);
	sdsfree(topic);
	return rc;
}

int redis_sub_sremove(redisAsyncContext * c, char const * id, char const * topicstr)
{
	if(!(c && id && topicstr))
		return -1;

	sds sid = sdscatfmt(sdsempty(), "%s:s:%s", g_rmqtt_conf("redis.topic"), id);
	sds topic = internal_TID_gen(g_rmqtt_conf("redis.topic"), topicstr, 8);

	int rc = redisAsyncCommand(c, def_cb, "HDEL"/* privdata */, "HDEL %s %s", sid, topic);

	sdsfree(sid);
	sdsfree(topic);

	return rc;
}

char const * redis_cli_topic(char const * topic)
{
	return internal_get_postfix_from_TID(topic, 0);
}
/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#include <uv.h>
#include "hp/hp_cjson.h"
#include "hp/string_util.h"
#include "hp/hp_redis.h"
#include "hp/hp_config.h"

static redisAsyncContext * pubc = 0;
static int done = 0;
static char const * id = 0;
static sds msgs[128] = { 0 };

static void test_cb_1(hp_sub_t * s, char const * topic, sds id, sds msg)
{
	assert(pubc && id);

	assert(s && msg && s->arg._1);
	assert(*(int *)s->arg._1 == 10);

	if(!topic){
		fprintf(stderr, "%s: erorr %s\n", __FUNCTION__, msg);
		return;
	}
	else if(topic[0] == '\0') { 
		fprintf(stdout, "%s: unsubscribed\n", __FUNCTION__);
		done = -1;
		return;
	}

	if(msgs[0]){
		printf("%s: '%s'\n", __FUNCTION__, msg);
		if(strcmp(msg, msgs[0]) == 0){
//			assert(strcmp(topic, cli->topic) == 0);
			++done;
		}
	}
}

static void test_sub_only(hp_sub_t * s, char const * topic, sds id, sds msg)
{
	assert(pubc && id);

	assert(s && msg && s->arg._1);
	assert(*(int *)s->arg._1 == 10);

	if(!topic){
		fprintf(stderr, "%s: erorr %s\n", __FUNCTION__, msg);
		return;
	}
	else if(topic[0] == '\0') {
		fprintf(stdout, "%s: unsubscribed\n", __FUNCTION__);
		done = -1;
		return;
	}
	++done;
}

static void is_done_1(redisAsyncContext *c, void *r, void *privdata) {
	redisReply * reply = (redisReply *)r;
	assert(reply && reply->type != REDIS_REPLY_ERROR);
	done = 1;
}
static void is_done_2(redisAsyncContext *c, void *r, void *privdata) {
	redisReply * reply = (redisReply *)r;
	assert(reply && reply->type != REDIS_REPLY_ERROR);
	done = 1;
}
static void is_done_3(redisAsyncContext *c, void *r, void *privdata) {
	redisReply * reply = (redisReply *)r;
	assert(reply && reply->type != REDIS_REPLY_ERROR);
	++done;
}

int test_redis_pub_main(int argc, char ** argv)
{
	assert(g_rmqtt_conf);
	hp_config_t cfg = g_rmqtt_conf;

	int i, r, rc;
	{
		int flags = 0;
		char * topics[] = { "rmqtt:1:865452044887154", "rmqtt:group:all"};
		assert(strcmp(internal_get_postfix_from_TID(topics[0], &flags), "865452044887154") == 0); assert(flags == 0);
		assert(strcmp(internal_get_postfix_from_TID(topics[1], &flags), "all") == 0); assert(flags == 8);

		assert(strcmp(internal_get_CID_from_SID("rmqtt:s:865452044887154"), "865452044887154") == 0);
	}

	int arg = 10;

	id = "868783048901857";

	/* QOS table. sds string -> QOS int */
	static dictType qosTableDictType = {
		r_dictSdsHash,            /* hash function */
	    NULL,                   /* key dup */
	    NULL,                   /* val dup */
		r_dictSdsKeyCompare,      /* key compare */
	    r_dictSdsDestructor,      /* key destructor */
		NULL                    /* val destructor */
	};
	rmqtt_io_t clientobj = { 0 }, * client = &clientobj;
	client->qos = dictCreate(&qosTableDictType);
	hp_redis_ev_t s_evobj, *rev = &s_evobj;
	rev_init(rev); assert(rev);

	r = hp_redis_init(&pubc, rev, cfg("redis"), cfg("redis.password"), 0);
//	pubc->dataCleanup = 0;
	assert(r == 0 && pubc);
	r = hp_redis_init(&client->subc, rev, cfg("redis"), cfg("redis.password"), 0);
	assert(r == 0 && client->subc);

	/* failed: invalid arg */
	{
		assert(redis_pub(0, 0, 0, 0, 0, 0) != 0);
		assert(redis_pub(pubc, 0, 0, 0, 0, 0) != 0);

		assert(!redis_subc(0, 0, 0, 0, 0));
		assert(!redis_subc(pubc, 0, 0, 0, 0));
		assert(!redis_subc(pubc, client->subc, 0, 0, 0));

		assert(redis_sub(0, 0, 0, 0) != 0);

		assert(redis_sup(0, 0, 0, 0, 0) != 0);
		assert(redis_sup(pubc, 0, 0, 0, 0) != 0);
		assert(redis_sup(pubc, 0, 0, 0, 0) != 0);
		assert(redis_sup(pubc, id, 0, 0, 0) != 0);

		assert(redis_sub_sadd(0, 0, 0) != 0);
		assert(redis_sub_sadd(0, 0, 0) != 0);
		assert(redis_sub_sremove(0, 0, 0) != 0);
		assert(redis_sub_sremove(0, 0, 0) != 0);
	}
	/* sadd */
	{
		rc = redis_sub_sadd(pubc, "865452044887154", "group1");
		assert(rc == 0);
		rc = redis_sub_sremove(pubc, "865452044887154", "group1");
		assert(rc == 0);

		done = 10;
		for(; done > 0;--done) rev_run(rev);
		done = 0;
	}
	/* sup: OK */
	{
		rc = redis_sup(pubc, id, 0, "0", is_done_1);
		assert(rc == 0);

		for(; !done;) rev_run(rev);
		done = 0;
	}
	/* OK: pub only, pub 1 */
	{
		r = redis_pub(pubc, id, "hello", 5, 0, is_done_2);
		assert(r == 0);

		for(; !done;) rev_run(rev);

		done = 0;
	}
	/* OK: pub only, pub 2 */
	{
		r = redis_pub(pubc, id, "hello", 5, 0, is_done_3);
		assert(r == 0);

		r = redis_pub(pubc, id, "hello", 5, 0, is_done_3);
		assert(r == 0);

		for(; done != 2;) rev_run(rev);

		done = 0;
	}
	/* OK: sub only */
	{
		client->subc = redis_subc(pubc, client->subc, id, test_sub_only, &arg);
		r = redis_sub(client, 0, 0, 0);
		assert(r == 0);

		int unsub = 0;
		for(i = 0; ;) {
			rev_run(rev);

			if(!unsub && done > 0){
				r = hp_unsub(client->subc);
				assert(r == 0);

				unsub = 1;
				continue;
			}

			if(unsub  && done == -1)
				break;
		}

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));

	}

	/* OK: pub 1 */
	{
		client->subc = redis_subc(pubc, client->subc, id, test_cb_1, &arg);
		r = redis_sub(client, 0, 0, 0);
		assert(r == 0);

		int unsub = 0;
		for(i = 0; ;) {
			rev_run(rev);

			if(i < 1){
				char uuidstr[64] = "";
				struct timeval tv;
				gettimeofday(&tv, NULL);
				snprintf(uuidstr, sizeof(uuidstr), "%.0f", tv.tv_sec * 1000.0 + tv.tv_usec / 1000);

				msgs[i] =  sdsnew(uuidstr);

				r = redis_pub(pubc, id, uuidstr, strlen(uuidstr), 0, 0);
				assert(r == 0);

				++i;
				continue;
			}

			if(!unsub && done > 0){
				r = hp_unsub(client->subc);
				assert(r == 0);

				unsub = 1;
				continue;
			}

			if(unsub && done == -1)
				break;
		}

		done = 0;
		for(i = 0; msgs[i]; ++i) sdsfree(msgs[i]);
		memset(msgs, 0, sizeof(msgs));

	}

	hp_redis_uninit(pubc);
	hp_redis_uninit(client->subc);
	rev_close(rev);

	dictRelease(client->qos);

	return 0;
}

#endif /* NDEBUG */
