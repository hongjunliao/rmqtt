
 /* This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/1/9
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "Win32_Interop.h"
#include "redis/src/adlist.h" /* list */
#ifndef _MSC_VER
#include <sys/wait.h>
#endif /* _MSC_VER */

#ifdef LIBHP_WITH_WIN32_INTERROP
#include "redis/src/Win32_Interop/Win32_QFork.h"
#endif /* LIBHP_WITH_WIN32_INTERROP */

#include "hp/sdsinc.h"        /* sds */
#include <unistd.h>        /* _SC_IOV_MAX */
#include <locale.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>	       /* malloc */
#include <errno.h>         /* errno */
#include <string.h>	       /* strncmp */
#include <ctype.h>	       /* isblank */
#include <assert.h>        /* define NDEBUG to disable assertion */
#include <stdint.h>
#include <getopt.h>		/* getopt_long */
#include "hiredis/hiredis.h"
#include <hiredis/adapters/libuv.h>
#include "zlog.h"			/* zlog */
#include "c-vector/cvector.h"	/**/
#include "inih/ini.h"
#include "hp/str_dump.h"   /* dumpstr */
#include "hp/string_util.h"
#include "hp/hp_io_t.h"
#include "hp/hp_log.h"
#include "hp/hp_net.h"
#include "hp/hp_sig.h"
#include "hp/hp_expire.h"  /* hp_expire */
#include "hp/hp_redis.h"	/* hp_redis_init */
#include "hp/hp_config.h"	/* hp_config_t */
#include "hp/hp_test.h"    /* hp_test */
#include "mongoose/mongoose.h"
#include "rmqtt_io_t.h"
#include "gen/git_commit_id.h"	/* GIT_BRANCH_NAME, GIT_COMMIT_ID */

/////////////////////////////////////////////////////////////////////////////////////////

/* listen fd */
static int s_listenfd = 0;
/* event loop */
static hp_redis_ev_t s_evobj, * s_ev = &s_evobj;

#ifndef _MSC_VER
/* for IPC */
static int n_chlds = 0, is_master = 1;
static int n_workers = 16;
static pid_t chlds[128] = { 0 };
/* for signal handle */
static int sigchld = 0;
static hp_sig ghp_sigobj = { 0 }, *s_sig = &ghp_sigobj;

#endif /* _MSC_VER */

/* rmqtt server */
static hp_io_ctx s_ioctxobj, * s_ioctx = &s_ioctxobj;
static rmqtt_io_ctx s_rmqttobj = { 0 }, * s_rmqtt = &s_rmqttobj;

/* HTTP  */
#if (!defined _MSC_VER) || (!defined LIBHP_WITH_WIN32_INTERROP)
struct mg_mgr mgrobj, *mgr = &mgrobj;
struct mg_timer t1obj, t2obj, *t1 = &t1obj, *t2 = &t2obj;
#endif

/* global for Redis */
redisAsyncContext * g_redis = 0;

/* config table. char * => char * */
static dictType configTableDictType = {
	r_dictSdsHash,            /* hash function */
    NULL,                   /* key dup */
    NULL,                   /* val dup */
	r_dictSdsKeyCompare,      /* key compare */
    r_dictSdsDestructor,      /* key destructor */
	r_dictSdsDestructor       /* val destructor */
};
static dict * config = 0;
#define cfgi(key) atoi(cfg(key))
static char const * cfg(char const * id) {
	sds key = sdsnew(id);
	void * v = dictFetchValue(config, key);
	sdsfree(key);
	return v? (char *)v : "";
}
hp_config_t g_rmqtt_conf = cfg;
static int s_quit = 0;

/* the default configure file */
#ifndef _MSC_VER
#define  RMQTT_CONF "/etc/rmqtt.conf"
#define ZLOG_CONF "/etc/zlog.conf"
#else
#define  RMQTT_CONF "rmqtt.conf"
#define ZLOG_CONF "zlog.conf"
#endif /* _MSC_VER */

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef _MSC_VER
void on_sigchld(void * arg)
{
	++sigchld;
}

void on_sigexit(void * arg)
{
	++s_quit;
}
#endif /* _MSC_VER */

/////////////////////////////////////////////////////////////////////////////////////////
extern int mg_init(struct mg_mgr * mgr, struct mg_timer * t1, struct mg_timer * t2);

/////////////////////////////////////////////////////////////////////////////////////////

static redisAsyncContext * redis_get()
{
	assert(s_ev);
	redisAsyncContext * c = 0;
	int rc = hp_redis_init(&c, s_ev, cfg("redis"), cfg("redis.password"), 0);
	return (rc == 0? c : 0);
}

/////////////////////////////////////////////////////////////////////////////////////////
/*====================== Hash table type implementation  ==================== */
int r_dictSdsKeyCompare(void *privdata, const void *key1,  const void *key2)
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
static int dictSdsKeyCaseCompare(void *privdata, const void *key1,
        const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcasecmp(key1, key2) == 0;
}

void r_dictSdsDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

uint64_t r_dictSdsHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
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
#ifndef _MSC_VER
	else if (strcmp(name, "workers") == 0)
		n_workers = atoi(value);
#endif /* _MSC_VER */
	else if(strcmp(name, "loglevel") == 0){
		hp_log_level = atoi(value);
	}
	
	dictAdd(cfg, sdsnew(name), sdsnew(value));

	return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef _MSC_VER

static int init_chld()
{
	int rc;
	/* init uv */
	rev_init(s_ev);
	if(!s_ev) { return -10; }
	/* init Redis */
    g_redis = redis_get();
    if(!g_redis){ return -2; }
    rc  = hp_io_init(s_ioctx);
    if(rc != 0){ return -3; }
	rc = rmqtt_io_init(s_rmqtt, s_ioctx, s_listenfd, cfgi("tcp-keepalive")
		, g_redis, redis_get, cfgi("redis.ping"));
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
		rev_close(s_ev);

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

			if(hp_log_level > 0)
				hp_log(stdout, "%s/%d: SIGCHLD on worker pid=%d, n_workers=%d/%d\n"
					, __FUNCTION__, getpid(), pid, n_chlds, n_workers);
		}
	}

	--sigchld;
	return 0;
}

#endif /* _MSC_VER */
/////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char ** argv)
{
	int rc;
	fprintf(stdout, "%s: build at %s %s\n", __FUNCTION__, __DATE__, __TIME__);
#if (defined _MSC_VER) && (!defined LIBHP_WITH_WIN32_INTERROP)
	/* init winsock */
	WSADATA wsd;
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		fprintf(stdout, "%s: error WSAStartup, err=%d\n", __FUNCTION__, WSAGetLastError());
		return 1;
	}
#endif /* LIBHP_WITH_WIN32_INTERROP */				
	setlocale(LC_COLLATE, "");
	srand((unsigned int)time(NULL) ^ getpid());   /* cast (unsigned int) */

	sds conf = sdsnew(RMQTT_CONF);
	sds zconf = sdsnew(ZLOG_CONF);
#ifdef LIBHP_WITH_ZLOG
	/* init log sytstem: zconf */
	if (access(zconf, F_OK) == 0) {
		//if(zlog_init(zconf) != 0) { return -1; }
		if (dzlog_init(zconf, "rmqtt") != 0 ) { return -1; }
	}
	else {
		fprintf(stderr, "%s: unable to init zlog, '%s' can NOT be load\n", __FUNCTION__, zconf);
		return -1;
	}

#endif /* LIBHP_WITH_ZLOG */
	/* config */
	config = dictCreate(&configTableDictType, 0);

	if(access(conf, F_OK) == 0) {
		if (ini_parse(conf, inih_handler, config) < 0) {
			hp_log(stderr, "%s: ini_parse '%s' \n", __FUNCTION__, conf);
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
			if     (option_index == 0) dictAdd(config, sdsnew("test"), sdsnew(arg));
			break;
		}
		case 'f':
			if (strlen(arg) > 0){
				if(ini_parse(arg, inih_handler, config) < 0) {
					hp_log(stderr, "%s: ini_parse '%s' \n", __FUNCTION__, arg);
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
			hp_log(stdout, "%s-%s, build at %s %s\n"
					, GIT_BRANCH_NAME, GIT_COMMIT_ID
					, __DATE__, __TIME__ );
			return 0;
			break;
		case 'v':
			hp_log_level = atoi(arg);
			break;
		case 'h':
			hp_log(stdout, "rmqtt - a Redis-based MQTT broker\n"
					"Usage: %s -fconf/rmqtt.conf\n"
					, argv[0]);
			return 0;
			break;
		}
	}

#ifndef NDEBUG
	//int test_hp_fs_main(int argc, char ** argv);
	//test_hp_fs_main(argc, argv);
	// test_redis_pub_main(argc, argv);
//	test_hp_io_t_main(argc, argv);
#endif
	/* init HTTP for master */
#if (!defined _MSC_VER) || (!defined LIBHP_WITH_WIN32_INTERROP)
	if (mg_init(mgr, t1, t2) != 0) { return -4; }
#endif /* LIBHP_WITH_WIN32_INTERROP */
	/* init event loop */
	rev_init(s_ev);
	if (!s_ev) { return -3; }
	/* init Redis */
	g_redis = redis_get();
	if (!g_redis) { return -11; }
	/* init listening port */
	s_listenfd = hp_net_listen(cfgi("mqtt.port"));
	if (s_listenfd <= 0) { return -2; }
#ifdef _MSC_VER
	rc = rmqtt_io_init(s_rmqtt, s_listenfd, cfgi("tcp-keepalive")
		, g_redis, redis_get, cfgi("redis.ping"));
	if (rc != 0) { return -3;}
#else
	/* init in child */
#endif /* _MSC_VER */

#ifndef _MSC_VER
	if (hp_sig_init(s_sig, on_sigchld, on_sigexit, 0, 0, 0) != 0) { return -1; }
#endif /* _MSC_VER */

	hp_log(stdout, "%s: listening on port=%d, waiting for connection ...\n", __FUNCTION__
			, cfgi("mqtt.port"));
#ifndef NDEBUG
	char const * test = cfg("test");
	if(strlen(test) > 0){
		rc = hp_test(test, argc, argv, 0, 0);
		return rc;
	}
#endif
	/* run */
	for(;!s_quit;){
#ifndef _MSC_VER
		if (is_master) {
			do_fork();
			handle_signal();
			mg_mgr_poll(mgr, cfgi("hz"));
		}
		else {
			rmqtt_io_run(s_rmqtt, cfgi("hz"));
		}
#else
#if (!defined _MSC_VER) || (!defined LIBHP_WITH_WIN32_INTERROP)
		mg_mgr_poll(mgr, cfgi("hz"));
#endif /* LIBHP_WITH_WIN32_INTERROP */
		hp_io_run((hp_io_ctx *)s_rmqtt, cfgi("hz"), 0);
#endif /* _MSC_VER */
		rev_run(s_ev);
	}

	/* unit */
#ifndef _MSC_VER
	if(!is_master){
		rmqtt_io_uninit(s_rmqtt);
	}
#else
	rmqtt_io_uninit(s_rmqtt);
#endif /* _MSC_VER */
#if (!defined _MSC_VER) || (!defined LIBHP_WITH_WIN32_INTERROP)
	mg_timer_free(t1);
	mg_timer_free(t2);
	mg_mgr_free(mgr);
#endif /* LIBHP_WITH_WIN32_INTERROP */

	hp_redis_uninit(g_redis);
	rev_close(s_ev);

	dictRelease(config);
	sdsfree(conf);
	sdsfree(zconf);

#ifndef _MSC_VER
	fprintf(stdout, "%s: %s %s=%d exited\n", __FUNCTION__, argv[0]
		, (is_master ? "master" : "worker"), getpid());
#endif /* _MSC_VER */
	return 0;
}
