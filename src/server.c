
 /* This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/1/9
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef _MSC_VER
#include <sys/wait.h>
#else
#include "redis/src/Win32_Interop/Win32_Portability.h"
#include "redis/src/Win32_Interop/win32_types.h"
#include "redis/src/Win32_Interop/Win32_FDAPI.h"
#endif /* _MSC_VER */

#include <stdio.h>
#include <stdlib.h>	       /* malloc */
#include <errno.h>         /* errno */
#include <string.h>	       /* strncmp */
#ifdef _MSC_VER
#define strcasecmp  _stricmp
#endif /* _MSC_VER */

#include <ctype.h>	       /* isblank */
#include <assert.h>        /* define NDEBUG to disable assertion */
#include <stdint.h>
#include <getopt.h>		/* getopt_long */
#include "hiredis/hiredis.h"
#include <hiredis/adapters/libuv.h>
#include "zlog.h"			/* zlog */
#include "c-vector/cvector.h"	/**/
#include "inih/ini.h"
#include "hp/sdsinc.h"        /* sds */
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
#include "hp/hp_libim.h"
#ifndef _MSC_VER
#include "mongoose/mongoose.h"
#endif /* _MSC_VER */

#include "rmqtt_io_t.h"
#include "gen/git_commit_id.h"	/* GIT_BRANCH_NAME, GIT_COMMIT_ID */
#include "server.h"
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

static void _redisAssert(char *estr, char *file, int line) {
	assert(0 && "_redisAssert");
}

/////////////////////////////////////////////////////////////////////////////////////////

hp_io_t *  rmqttc_on_new(hp_io_ctx * ioctx, hp_sock_t fd)
{
	if(!ioctx) { return 0; }
	rmqtt_io_t * io = calloc(1, sizeof(rmqtt_io_t));
	int rc = rmqtt_io_t_init(io, (rmqtt_io_ctx *)ioctx);
	assert(rc == 0);

	return (hp_io_t *)io;
}
int rmqttc_on_parse(hp_io_t * io, char * buf, size_t * len, int flags
	, hp_iohdr_t ** hdrp, char ** bodyp)
{
	return rmqtt_parse(buf, len, flags, hdrp, bodyp);
}
int rmqttc_on_dispatch(hp_io_t * io, hp_iohdr_t * imhdr, char * body)
{
	return rmqtt_dispatch((rmqtt_io_t *)io, imhdr, body);
}

int rmqttc_on_loop(hp_io_t * io)
{
	return rmqtt_io_t_loop((rmqtt_io_t *)io);
}

void rmqttc_on_delete(hp_io_t * io)
{
	if(!io) { return; }
	rmqtt_io_t_uninit((rmqtt_io_t *)io);
	free(io);
}

/////////////////////////////////////////////////////////////////////////////////////////
