
 /* This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/1/9
 *
 * */

#include <unistd.h>        /* _SC_IOV_MAX */
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>	       /* malloc */
#include <errno.h>         /* errno */
#include <string.h>	       /* strncmp */
#include <ctype.h>	       /* isblank */
#include <assert.h>        /* define NDEBUG to disable assertion */
#include <stdint.h>
#include <getopt.h>		/* getopt_long */
#include <sys/timerfd.h> /* timerfd_create */
#include "hiredis/hiredis.h"
#include <hiredis/adapters/libuv.h>
#include "zlog.h"			/* zlog */
#include "sds/sds.h"        /* sds */
#include "sds/sds.h"        /* sds */
#include "cJSON/cJSON.h"	/* cJSON */
#include "hp/hp_cjson.h"
#include "c-vector/cvector.h"	/**/
#include "inih/ini.h"
#include "hp/str_dump.h"   /* dumpstr */
#include "hp/string_util.h"
#include "hp/hp_epoll.h"
#include "hp/hp_log.h"
#include "hp/hp_net.h"
#include "hp/hp_sig.h"
#include "hp/hp_expire.h"  /* hp_expire */
#include "hp/hp_redis.h"	/* hp_redis_init */
#include "hp/hp_config.h"	/* hp_config_t */
#include "hp/hp_test.h"    /* hp_test */
#include "mongoose/mongoose.h"
#include "libim_rcli.h"
#include "gen/git_commit_id.h"	/* GIT_BRANCH_NAME, GIT_COMMIT_ID */
#include "server.h"
/////////////////////////////////////////////////////////////////////////////////////////
/* for master/slave */
static int libim_fd = 0;
static int n_chlds = 0, is_master = 1;
static hp_epoll efdsobj = { 0 }, * efds = &efdsobj;
static uv_loop_t uvloopobj = { 0 }, * uvloop = &uvloopobj;

static pid_t chlds[128] = { 0 };
static hp_sig ghp_sigobj = { 0 }, * ghp_sig = &ghp_sigobj;
static int sigchld = 0;

static int n_workers = 16;
static libim_rctx imclictxobj = { 0 }, * imclictx = &imclictxobj;

/* HTTP  */
struct mg_mgr mgrobj, * mgr = &mgrobj;
struct mg_timer t1obj, t2obj, * t1 = &t1obj, * t2 = &t2obj;
/* for Redis Pub */
redisAsyncContext * g_redis = 0;

int gloglevel = 9;
#define Mq XHMDM_MQ_PKG
#define Cjson HP_CJSON_PKG

/* config table. char * => char * */
static dictType configTableDictType = {
	dictSdsHash,            /* hash function */
    NULL,                   /* key dup */
    NULL,                   /* val dup */
	dictSdsKeyCompare,      /* key compare */
    dictSdsDestructor,      /* key destructor */
	dictSdsDestructor       /* val destructor */
};
static dict * config = 0;
#define cfgi(key) atoi(cfg(key))
static char const * cfg(char const * id) {
	sds key = sdsnew(id);
	void * v = dictFetchValue(config, key);
	sdsfree(key);
	return v? (char *)v : "";
}
hp_config_t g_conf = cfg;

/////////////////////////////////////////////////////////////////////////////////////////
/*====================== Hash table type implementation  ==================== */
int dictSdsKeyCompare(void *privdata, const void *key1,  const void *key2)
{
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

/* A case insensitive version used for the command lookup table and other
 * places where case insensitive non binary-safe comparison is needed. */
int dictSdsKeyCaseCompare(void *privdata, const void *key1,
        const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcasecmp(key1, key2) == 0;
}

void dictSdsDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

uint64_t dictSdsHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

/////////////////////////////////////////////////////////////////////////////////////////

void on_sigchld(void * arg)
{
	++sigchld;
}

/////////////////////////////////////////////////////////////////////////////////////////

static redisAsyncContext * redis_get()
{
	redisAsyncContext * c = 0;
	int rc = hp_redis_init(&c, uvloop, cfg("redis"), cfg("redis.password"), 0);
	return (rc == 0? c : 0);
}

/////////////////////////////////////////////////////////////////////////////////////////

static int inih_handler(void* user, const char* section, const char* name,
                   const char* value)
{
	dict* cfg = (dict*)user;
	assert(cfg);

	if(strcmp(name, "mqtt.addr") == 0){
		/* mqtt.addr=0.0.0.0:7006 */
		char mqtt_bind[128] = "";
		int mqtt_port = 0;

		if(value && strlen(value) > 0){
			int n = sscanf(value, "%[^:]:%d", mqtt_bind, &mqtt_port);
			if(n != 2){
				return 0;
			}
		}
		/*
		 * NOTE:
		 * set mysql=
		 * will clear existing values */
		dictAdd(cfg, sdsnew("mqtt.bind"), sdsnew(mqtt_bind));
		dictAdd(cfg, sdsnew("mqtt.port"), sdsfromlonglong(mqtt_port));
	}
	else if(strcmp(name, "redis") == 0){

		char redis_ip[64] = "";
		int redis_port = 0;

		/* NOTE:
		 * set redis=
		 * will clear existing values */
		if(value && strlen(value) > 0){
			int n = sscanf(value, "%[^:]:%d", redis_ip, &redis_port);
			if(n != 2){
				return 0;
			}
		}

		dictAdd(cfg, sdsnew("redis_ip"), sdsnew(redis_ip));
		dictAdd(cfg, sdsnew("redis_port"), sdsfromlonglong(redis_port));
	}
	else if(strcmp(name, "workers") == 0)
		n_workers = atoi(value);
	else if(strcmp(name, "loglevel") == 0){
		gloglevel = atoi(value);
	}
	
	dictAdd(cfg, sdsnew(name), sdsnew(value));

	return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////

static int init_chld()
{
	int rc;

	/* init epoll */
	if (hp_epoll_init(efds, 65535) != 0)
		return -5;

	/* init uv */
	rc = uv_loop_init(uvloop);
	if(rc != 0) return -10;

	/* init Redis */
    g_redis = redis_get();
    if(!g_redis){
    	return -2;
	}

	/* init imcli */
	rc = libim_rctx_init(imclictx, efds, libim_fd, cfgi("tcp-keepalive")
			, g_redis, redis_get, cfgi("redis.ping"));
	if(rc != 0){
		return -3;
	}

	rc = hp_epoll_add(efds, libim_fd, EPOLLIN, &((libim_ctx *)imclictx)->ed); assert(rc == 0);

	return rc;
}

static int do_fork()
{
	int i;
	int rc;

	if(n_chlds >= n_workers)
		return 0;

	pid_t pid = fork();
	if (pid < 0) {
		hp_log(stderr, "%s: fork failed, errno=%d, error='%s'\n",
				__FUNCTION__, errno, strerror(errno));
		return -2;
	}
	else if(pid == 0){  /* worker process */
		/* reset child's environments */
		is_master = 0;
		n_chlds = 0;
		uv_loop_close(uvloop);

		mg_timer_free(t1);
		mg_timer_free(t2);
		mg_mgr_free(mgr);
		hp_redis_uninit(g_redis); g_redis = 0;

		/* init child */
		rc = init_chld();
		if(rc != 0){
			sleep(1);
			exit(0);
		}
	}
	else {              /* master process */
		is_master = 1;
		++n_chlds;

		for(i = 0; i < sizeof(chlds) / sizeof(chlds[0]); ++i){
			if(chlds[i] == 0){
				chlds[i] = pid;
				break;
			}
		}
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

static int handle_signal()
{
	if(sigchld == 0)
		return 0;

	int i = 0;
	for(i = 0; i < sizeof(chlds) / sizeof(chlds[0]); ++i){

		if(chlds[i] == 0)
			continue;
		pid_t pid = chlds[i];

		if(waitpid(pid, 0, WNOHANG) == pid){

			chlds[i] = 0;
			--n_chlds;

			if(gloglevel > 0)
				hp_log(stdout, "%s/%d: SIGCHLD on worker pid=%d, n_workers=%d/%d\n"
					, __FUNCTION__, getpid(), pid, n_chlds, n_workers);
		}
	}

	--sigchld;
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

static void imcli_try_write()
{
	struct list_head * pos, * next;;
	list_for_each_safe(pos, next, &((libim_ctx *)imclictx)->cli){

		libim_cli* client = (libim_cli *)list_entry(pos, libim_cli, list);
		assert(client);
		hp_eto_try_write(&client->eto, &client->epolld);
		/* DO NOT use client after call of hp_eto_try_write !!!! */
	}

	list_for_each_safe(pos, next, &((libim_ctx *)imclictx)->cli){

		libim_cli* client = (libim_cli *)list_entry(pos, libim_cli, list);
		assert(client);
		assert(client->ctx);

		if(client->ctx->proto.loop)
			client->ctx->proto.loop(client);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char ** argv)
{
	int i, rc;

	sds conf = sdsnew("/etc/rmqtt.conf");
	sds zconfs[] = { sdsnew("/etc/zlog.conf"), sdsnew("/etc/rmqtt.zlog") };

	/* init log sytstem: zconf */
	rc = -1;
	int log_sys = -1;

	for(i = 0; i < 2; ++i){
		if(access(zconfs[i], F_OK) == 0) {
			if(log_sys != 0)
				log_sys = zlog_init(zconfs[i]);
			else   log_sys = zlog_reload(zconfs[i]);

			rc = dzlog_set_category("rmqtt");
			if (!(log_sys == 0 && rc == 0)) {
				fprintf(stderr, "%s: zlog_init('%s') failed\n", __FUNCTION__,
						zconfs[i]);
				return -10;
			}
		}
	}
	if(rc != 0){
		fprintf(stderr, "%s: unable to init zlog, none of '%s','%s' can be load\n", __FUNCTION__
			, zconfs[0], zconfs[1]);
		return -10;
	}
	/* config */
	config = dictCreate(&configTableDictType, 0);

	if(access(conf, F_OK) == 0) {
		if (ini_parse(conf, inih_handler, config) < 0) {
			fprintf(stderr, "%s: ini_parse '%s' \n", __FUNCTION__, conf);
			return -3;
		}
	}
    /* parse argc/argv */
	int c;
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
				{ "test", optional_argument, 0, 0 }
			, 	{ 0, 0, 0, 0 } };

		c = getopt_long(argc, argv, "v:f:sVh", long_options, &option_index);
		if (c == -1)
			break;

		char const * arg = optarg? optarg : "";
		switch (c) {
		case 0:{
			#define is2(s) (strncmp(arg, s, 2) == 0)
			#define is(s) (arg[0] == s)

			if     (option_index == 0) dictAdd(config, sdsnew("test"), sdsnew(arg));
			break;
		}
		case 'f':
			if (strlen(arg) > 0){
				if(ini_parse(arg, inih_handler, config) < 0) {
					fprintf(stderr, "%s: ini_parse '%s' \n", __FUNCTION__, arg);
					return -3;
				}
			}
			break;
		case 's':{
			dictIterator * iter = dictGetIterator(config);
			dictEntry * ent;
			for(ent = 0; (ent = dictNext(iter));){
				printf("'%s'=>'%s'\n", (char *)ent->key, (char *)ent->v.val);
			}
			dictReleaseIterator(iter);
			return 0;
		}
			break;
		case 'V':
			fprintf(stdout, "%s-%s, build at %s %s\n"
					, GIT_BRANCH_NAME, GIT_COMMIT_ID
					, __DATE__, __TIME__ );
			return 0;
			break;
		case 'v':
			gloglevel = atoi(arg);
			break;
		case 'h':
			fprintf(stdout, "rmqtt - a Redis-based MQTT broker\n"
					"Usage: %s -fconf/rmqtt.conf\n"
					, argv[0]);
			return 0;
			break;
		}
	}

	/* init HTTP for master */
	int mg_init(struct mg_mgr * mgr, struct mg_timer * t1, struct mg_timer * t2);
	if(mg_init(mgr, t1, t2) != 0){
		return -4;
	}

	/* init listening port */
	libim_fd = hp_net_listen(cfgi("mqtt.addr"));
	assert(libim_fd > 0);
	if(libim_fd <= 0)
		return -2;

	/* init uv */
	rc = uv_loop_init(uvloop);
	if(rc != 0)
		return -10;

	/* init Redis */
	g_redis = redis_get();

	if(hp_sig_init(ghp_sig, on_sigchld, 0, 0, 0, 0) != 0){
		fprintf(stderr, "%s: hp_sig_init failed\n", __FUNCTION__);
		return -1;
	}

	hp_log(stdout, "%s: listening on port=%d, waiting for connection ...\n", __FUNCTION__
			, cfgi("mqtt.port"));

#ifndef NDEBUG
	char const * test = cfg("test");
	if(strlen(test) > 0){
		rc = hp_test(test, argc, argv, 0, 0);
		/* for redis async */
		uv_run(uvloop, UV_RUN_ONCE);	
		return rc;
	}
#endif

	/* run */
	for(;;){
		if(is_master){
			do_fork();
			handle_signal();

			mg_mgr_poll(mgr, cfgi("hz"));
		}
		else {
			imcli_try_write();
			hp_epoll_run(efds, cfgi("hz"), (void * )-1);
		}
		uv_run(uvloop, UV_RUN_NOWAIT);
	}

	/* unit */
	if(!is_master){
		libim_rctx_uninit(imclictx);
	}
	else{
		mg_timer_free(t1);
		mg_timer_free(t2);
		mg_mgr_free(mgr);
	}

	hp_redis_uninit(g_redis);
	uv_loop_close(uvloop);

	if(!is_master){
		close(libim_fd);
		hp_epoll_uninit(efds);
	}

	dictRelease(config);
	sdsfree(conf);
	for(i = 0; i < 2; ++i)
		sdsfree(zconfs[i]);

	return 0;
}
