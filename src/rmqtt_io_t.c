/*!
 * This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/3/8
 *
 * RMQTT client
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "Win32_Interop.h"
#ifndef _MSC_VER
#include <sys/time.h> /*gettimeofday*/
#endif /* _MSC_VER */
#include "rmqtt_io_t.h"
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>     /* assert */
#include "hp/hp_pub.h"
#include "hp/hp_io.h"      /* hp_eti,... */
#include "hp/hp_log.h"
#include "hp/hp_epoll.h"   /* hp_epoll */
#include "hp/hp_net.h"     /* hp_net_connect */
#include "hp/hp_config.h"	/* hp_config_t */
#include "hp/str_dump.h"
#include "protocol.h"
#include "redis_pub.h"

extern hp_config_t g_rmqtt_conf;

extern size_t rmqtt_parse(char const * buf, size_t * nbuf
		, void ** iohdrp, void ** bodyp);
extern int rmqtt_dispatch(rmqtt_io_t * io, void * iohdr, void * body);
/////////////////////////////////////////////////////////////////////////////////////////

/**
*/
//static void * sdslist_dup(void *ptr)
//{
//	libim_msg_t * msg = (libim_msg_t *)ptr;
//	assert(ptr);
//
//	libim_msg_t * ret = calloc(1, sizeof(libim_msg_t));
//	ret->payload = sdsdup(msg->payload);
//	ret->mid = sdsdup(msg->mid);
//	ret->topic = sdsdup(msg->topic);
////	ret->sid = sdsdup(msg->sid);
//
//	return ret;
//}

static void sdslist_free(void *ptr)
{
	rmqtt_rmsg_t * msg = (rmqtt_rmsg_t *)ptr;
	assert(ptr);

	sdsfree(msg->payload);
	sdsfree(msg->mid);
	sdsfree(msg->topic);
//	sdsfree(msg->sid);

	free(ptr);
}

static int sdslist_match(void *ptr, void *key)
{
	assert(ptr);
	rmqtt_rmsg_t * msg = (rmqtt_rmsg_t *)ptr;
	assert(strlen(msg->mid) > 0);
	assert(strlen((char *)key) > 0);

	return strncmp(msg->mid, (char *)key, sdslen(msg->mid)) == 0;
}

/* QOS table. sds string -> QOS int */
static dictType qosTableDictType = {
	r_dictSdsHash,            /* hash function */
    NULL,                   /* key dup */
    NULL,                   /* val dup */
	r_dictSdsKeyCompare,      /* key compare */
    r_dictSdsDestructor,      /* key destructor */
	NULL                    /* val destructor */
};

static int rmqtt_io_t_init(rmqtt_io_t * io, rmqtt_io_ctx * rctx)
{
	if(!(io && rctx))
		return -1;

	memset(io, 0, sizeof(rmqtt_io_t));

	io->sid = sdsnew("");
	io->rctx = rctx;
	io->qos = dictCreate(&qosTableDictType);

	io->outlist = listCreate();
//	listSetDupMethod(io->outlist, sdslist_dup);
	listSetFreeMethod(io->outlist, sdslist_free);
	listSetMatchMethod(io->outlist, sdslist_match);

	return 0;
}


static void rmqtt_io_t_uninit(rmqtt_io_t * io)
{
	if(!io)
		return ;

	int rc;
	rmqtt_io_ctx * rctx = io->rctx;

	sds key = sdscatprintf(sdsempty(), "%s:online", g_rmqtt_conf("redis.topic"));
	redisAsyncCommand(rctx->c, 0, 0/* privdata */, "SREM %s %s", key, io->sid);
	sdsfree(key);

	if(io->subc){
		rc = hp_unsub(io->subc);
	}

	dictRelease(io->qos);
	listRelease(io->outlist);
	sdsfree(io->sid);

	HP_UNUSED(rc);
}

static int rmqtt_io_send(rmqtt_io_t * io, rmqtt_rmsg_t * rmsg, int flags)
{
	int rc;
	if(!(io && rmsg))
		return -1;

	rmqtt_io_ctx * rctx = io->rctx;
	sds topic = sdsnew(redis_cli_topic(rmsg->topic));
	int len = sdslen(rmsg->payload);

	uint16_t message_id = ++rctx->mqid;
	if(rctx->mqid + 1 == UINT16_MAX)
		rctx->mqid = 0;

	uint16_t netbytes;
	uint16_t topic_len = sdslen(topic);

	size_t total_len = 2 + topic_len + len;
	if (MG_MQTT_GET_QOS(flags) > 0) {
		total_len += 2;
	}

	rmqtt_io_send_header(io, MG_MQTT_CMD_PUBLISH, flags, total_len);

	netbytes = htons(topic_len);
	hp_io_write((hp_io_t *)io, &netbytes, 2, (void *)-1, 0);
	rc = hp_io_write((hp_io_t *)io, (void *)(topic), topic_len, (hp_io_free_t)sdsfree, 0);

	if (MG_MQTT_GET_QOS(flags) > 0) {
		netbytes = htons(message_id);
		hp_io_write((hp_io_t *)io, &netbytes, 2, (void *)-1, 0);
	}

	rc = hp_io_write((hp_io_t *)io, (void *)sdsdup(rmsg->payload), len, (hp_io_free_t)sdsfree, 0);
	/* for ACK */
	io->l_mid = message_id;

	if(hp_log_level > 7){
		int len = sdslen(rmsg->payload);
		hp_log(stdout, "%s: fd=%d, sending topic='%s', msgid/QOS=%u/%u, playload=%u/'%s'\n", __FUNCTION__
				, (hp_io_fd((hp_io_t *)io)), topic, io->l_mid, 2, len, dumpstr(rmsg->payload, len, 64));
	}

	return rc;
}

static int rmqtt_io_t_loop(rmqtt_io_t * io)
{
	assert(io && io->rctx);

	int rc = 0;
	rmqtt_io_ctx * rctx = io->rctx;
	rmqtt_rmsg_t * rmsg = 0;

	/* redis ping/pong */
	if(rctx->rping_interval > 0 && io->subc){

		if(difftime(time(0), io->rping) > rctx->rping_interval){

			if(hp_log_level > 8)
				hp_log(stdout, "%s: fd=%d, Redis PING ...\n", __FUNCTION__, (hp_io_fd((hp_io_t *)io)));

			hp_sub_ping(io->subc);
			io->rping = time(0);
		}
	}

	/* check for current sending message */
	if(io->l_msg){
		rmsg = (rmqtt_rmsg_t *)listNodeValue(io->l_msg);
		assert(rmsg);

		/* QOS > 0 need ACK */
		dictEntry * ent = dictFind(io->qos, rmsg->topic);
		int qos = (ent? dictGetUnsignedIntegerVal(ent) : 2);

		if(qos > 0){
			/* check if current ACKed */
			if(sdslen(rmsg->mid) > 0){
				if(difftime(time(0), io->l_time) <= 10)
					goto ret;

				if(io->l_mid == 0){
					/* resend */
					rc = rmqtt_io_send(io, rmsg, MG_MQTT_QOS(qos));
					io->l_time = time(0);
				}
				goto ret;
			}
		}
		else{ /* QOS=0 at most once */
			rc = rmqtt_io_send(io, rmsg, MG_MQTT_QOS(qos));
			rc = redis_sup_by_topic(rctx->c, io->sid, rmsg->topic, rmsg->mid, 0);

			if(hp_log_level > 0){
				hp_log(stdout, "%s: Redis sup, fd=%d, io='%s', key/value='%s'/'%s'\n", __FUNCTION__
						, (hp_io_fd((hp_io_t *)io)), io->sid, rmsg->topic, rmsg->mid);
			}
			sdsclear(rmsg->mid);
		}
	}

	/* fetch next message to send */
	listNode * node = 0;

	if(!io->l_msg)
		io->l_msg = listFirst(io->outlist);
	else{
		node = io->l_msg;
		io->l_msg = listNextNode(io->l_msg);
	}

	if(node)
		listDelNode(io->outlist, node);

	if(!io->l_msg)
		goto ret;	/* empty message, nothing to send */

	io->l_time = 0;
	io->l_mid = 0;
ret:
	return rc;
}

static hp_io_t *  rmqttc_on_new(hp_io_t * cio, hp_sock_t fd)
{
	if(!(cio && cio->user)) { return 0; }
	rmqtt_io_t * c = calloc(1, sizeof(rmqtt_io_t));
	int rc = rmqtt_io_t_init(c, (rmqtt_io_ctx *)cio->user);
	assert(rc == 0);

	hp_iohdl niohdl = cio->iohdl;
	niohdl.on_new = 0;
	rc = hp_io_add(cio->ioctx, (hp_io_t *)c, fd, niohdl);
	if (rc != 0) {
		rmqtt_io_t_uninit(c);
		free(c);
		return 0;
	}

	c->base.addr = cio->addr;

	return (hp_io_t *)c;
}

static int rmqttc_on_parse(hp_io_t * io, char * buf, size_t * len
	, void ** hdrp, void ** bodyp)
{
	return rmqtt_parse(buf, len, hdrp, bodyp);
}

static int rmqttc_on_dispatch(hp_io_t * io, void * hdr, void * body)
{
	return rmqtt_dispatch((rmqtt_io_t *)io, hdr, body);
}

static int rmqttc_on_loop(hp_io_t * io)
{
	if(io->iohdl.on_new)
		return 0;
	return rmqtt_io_t_loop((rmqtt_io_t *)io);
}

static void rmqttc_on_delete(hp_io_t * io, int err, char const * errstr)
{
	if(!io) { return; }
	rmqtt_io_t_uninit((rmqtt_io_t *)io);
	free(io);
}

/* callbacks for rmqtt clients */
static hp_iohdl s_rmqtthdl = {
	.on_new = rmqttc_on_new,
	.on_parse = rmqttc_on_parse,
	.on_dispatch = rmqttc_on_dispatch,
	.on_delete = rmqttc_on_delete,
	.on_loop = rmqttc_on_loop,
#ifdef _MSC_VER
	.wm_user = 0 	/* WM_USER + N */
	.hwnd = 0    /* hwnd */
#endif /* _MSC_VER */
};

/////////////////////////////////////////////////////////////////////////////////////////
/**
 * MQTT protocol
 * */
int rmqtt_io_send_header(rmqtt_io_t * io, uint8_t cmd,
        uint8_t flags, size_t len)
{
	int rc;
	uint8_t * buf = calloc(1, 1 + sizeof(size_t));
	uint8_t *vlen = &buf[1];

	buf[0] = (cmd << 4) | flags;

	/* mqtt variable length encoding */
	do {
		*vlen = len % 0x80;
		len /= 0x80;
		if (len > 0)
			*vlen |= 0x80;
		vlen++;
	} while (len > 0);

	rc = hp_io_write((hp_io_t *)io, buf, vlen - buf, free, 0);
	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////

int rmqtt_io_init(rmqtt_io_ctx * rmqtt, hp_io_ctx * ioctx
	, hp_sock_t fd, int tcp_keepalive
	, redisAsyncContext * c, redisAsyncContext * (*redis)()
	, int ping_interval)
{
	int rc;
	if (!(rmqtt && c && redis)) { return -1; }

	rmqtt->ioctx = ioctx;
	rmqtt->c = c;
	rmqtt->redis = redis;
	rmqtt->rping_interval = ping_interval;

	rmqtt->listenio.user = rmqtt;

	rc = hp_io_add(rmqtt->ioctx, &rmqtt->listenio, fd, s_rmqtthdl);
	if (rc != 0) { return -4; }


	return rc;
}

int rmqtt_io_uninit(rmqtt_io_ctx * ioctx)
{
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
/* tests */
#ifndef NDEBUG
#include "hp/sdsinc.h"    /* sds */
#include "hp/string_util.h"

int test_libim_mqtt_main(int argc, char ** argv)
{
	int rc = 0;
//	{
//		char const* data = "";
//		libim_hdr imhdr_obj = { 0 }, * imhdr = &imhdr_obj;
//		char * buf = 0;
//		int n_buf = 0;
//		libim_hdr_pack(imhdr, data, &buf, &n_buf);
//		assert(buf && n_buf > 0);
//		free(buf);
//	}
//	{
//		char const* data = "";
//		libim_hdr imhdr_obj = { 0 }, * imhdr = &imhdr_obj;
//		sds buf = 0;
//		libim_hdr_pack_sds(imhdr, data, &buf);
//		assert(buf && sdslen(buf) > 0);
//		sdsfree(buf);
//	}
//
//	{
//		libim_proto proto = _proto;
//		assert(proto.new && proto.delete && proto.dispatch);
//		rmqtt_io_ctx ctxobj, * ioctx = &ctxobj;
//		libim_hdr * imhdr = 0;
//		char * body = 0;
//
//		rmqtt_io_t * io = proto.new(10, ioctx);
//		assert(io);
//		assert(io->fd == 10);
//
//		rc = proto.dispatch(io, imhdr, body);
//		assert(rc == 0);
//
//		proto.delete(io);
//	}

	return rc;
}
#endif /* NDEBUG */
