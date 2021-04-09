/*!
 * This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/3/8
 *
 * MQTT client for libim
 * */

#ifndef _MSC_VER

#include "libim_rcli.h"
#include <unistd.h>
#include <sys/time.h>   /* gettimeofday */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>     /* assert */
//#include "libimio.h"
#include "hp/hp_pub.h"
#include "hp/hp_io.h"      /* hp_eti,... */
#include "hp/hp_log.h"
#include "hp/hp_epoll.h"   /* hp_epoll */
#include "hp/hp_net.h"     /* hp_net_connect */
#include "hp/hp_cjson.h"
#include "hp/hp_config.h"	/* hp_config_t */
#include "hp/str_dump.h"
#include "protocol.h"
#include "redis_pub.h"
#include "server.h"

extern hp_config_t g_conf;
extern int gloglevel;

/////////////////////////////////////////////////////////////////////////////////////////
/**
 * libim MQTT protocol
 * */
int libim_mqtt_send_header(libim_rcli * client, uint8_t cmd,
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

	rc = hp_eto_add(&client->base.eto, buf, vlen - buf, free, 0);
	assert(rc == 0);

	return rc;
}

int libim_mqtt_send(libim_rcli * client, libim_msg_t * rmsg, int flags)
{
	int rc;
	if(!(client && rmsg))
		return -1;

	libim_rctx * proto = (libim_rctx *)((libim_cli *)client)->ctx;

	int len = sdslen(rmsg->payload);

	uint16_t message_id = ++proto->mqid;
	if(proto->mqid + 1 == UINT16_MAX)
		proto->mqid = 0;

	uint16_t netbytes;
	uint16_t topic_len = sdslen(rmsg->topic);

	size_t total_len = 2 + topic_len + len;
	if (MG_MQTT_GET_QOS(flags) > 0) {
		total_len += 2;
	}

	libim_mqtt_send_header(client, MG_MQTT_CMD_PUBLISH, flags, total_len);

	netbytes = htons(topic_len);
	hp_eto_add(&client->base.eto, &netbytes, 2, (void *)-1, 0);
	hp_eto_add(&client->base.eto, (void *)sdsdup(rmsg->topic), topic_len, (hp_eto_free_t)sdsfree, 0);

	if (MG_MQTT_GET_QOS(flags) > 0) {
		netbytes = htons(message_id);
		hp_eto_add(&client->base.eto, &netbytes, 2, (void *)-1, 0);
	}

	rc = hp_eto_add(&client->base.eto, (void *)sdsdup(rmsg->payload), len, (hp_eto_free_t)sdsfree, 0);

	/* for ACK */
	client->l_mid = message_id;

	if(gloglevel > 7){
		int len = sdslen(rmsg->payload);
		hp_log(stdout, "%s: fd=%d, sending topic='%s', msgid/QOS=%u/%u, playload=%u/'%s'\n", __FUNCTION__
				, ((libim_cli *)client)->fd, rmsg->topic, client->l_mid, 2, len, dumpstr(rmsg->payload, len, 64));
	}

	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////
// libim_rcli::qos

/* QOS table. sds string -> QOS int */
static dictType qosTableDictType = {
	dictSdsHash,            /* hash function */
    NULL,                   /* key dup */
    NULL,                   /* val dup */
	dictSdsKeyCompare,      /* key compare */
    dictSdsDestructor,      /* key destructor */
	NULL                    /* val destructor */
};

/**
 * libim_rcli::outlist
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
	libim_msg_t * msg = (libim_msg_t *)ptr;
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
	libim_msg_t * msg = (libim_msg_t *)ptr;
	assert(strlen(msg->mid) > 0);
	assert(strlen((char *)key) > 0);

	return strncmp(msg->mid, (char *)key, sdslen(msg->mid)) == 0;
}

static int libim_rcli_init(libim_rcli * client, int confd, libim_ctx * ctx)
{
	if(!(client))
		return -1;

	memset(client, 0, sizeof(libim_rcli));
	if(libim_cli_init((libim_cli *)client, confd, ctx) != 0)
		return -2;

	client->qos = dictCreate(&qosTableDictType, NULL);

	client->outlist = listCreate();
//	listSetDupMethod(client->outlist, sdslist_dup);
	listSetFreeMethod(client->outlist, sdslist_free);
	listSetMatchMethod(client->outlist, sdslist_match);

	return 0;
}

int libim_rcli_append(libim_rcli * client, char const * topic, char const * mid, sds message, int flags)
{
	if(!(client && message))
		return -1;

	libim_msg_t * jmsg = calloc(1, sizeof(libim_msg_t));
	jmsg->payload = sdsnew(message);
	jmsg->topic = sdsnew(topic);
	jmsg->mid = sdsnew(mid);

	list * li = listAddNodeTail(((libim_rcli *)client)->outlist, jmsg);
	assert(li);

	return 0;
}

void libim_rcli_uninit(libim_rcli * client)
{
	if(!client)
		return ;

	int rc;
	libim_rctx * proto = (libim_rctx *)((libim_cli*)client)->ctx;

	sds key = sdscatprintf(sdsempty(), "%s:online", g_conf("redis.topic"));
	redisAsyncCommand(proto->c, 0, 0/* privdata */, "SREM %s %s", key, ((libim_cli*)client)->sid);
	sdsfree(key);

	if(client->subc){
		rc = hp_unsub(client->subc);
	}

	dictRelease(client->qos);
	listRelease(client->outlist);
	libim_cli_uninit((libim_cli *)client);

	HP_UNUSED(rc);
}

static libim_cli * libim_rcli_new(int confd, libim_ctx * ctx)
{
	if(!(ctx))
		return 0;

	int rc;

	libim_rcli * client = calloc(1, sizeof(libim_rcli));
	if(client){
		rc = libim_rcli_init(client, confd, ctx);
		assert(rc == 0);
	}

	return (libim_cli *)client;
}

static void libim_rcli_del(libim_cli * client)
{
	if(!(client))
		return;

	libim_rcli_uninit((libim_rcli *)client);
	free(client);
}

size_t hp_libim_unpack_mqtt(int magic, char * buf, size_t * nbuf, int flags
		, libimhdr ** hdrp, char ** bodyp);
int libim_mqtt_dispatch(libim_cli * base, libimhdr * hdr, char * rawbody);

static int libim_mqtt_loop(libim_cli * c)
{
	assert(c);

	int rc;
	libim_rcli * client = (libim_rcli *)c;
	libim_rctx * proto = (libim_rctx *)c->ctx;
	libim_msg_t * rmsg = 0;

	/* redis ping/pong */
	if(proto->rping_interval > 0 && client->subc){

		if(difftime(time(0), client->rping) > proto->rping_interval){

			if(gloglevel > 8)
				hp_log(stdout, "%s: fd=%d, Redis PING ...\n", __FUNCTION__, c->fd);

			hp_sub_ping(client->subc);
			client->rping = time(0);
		}
	}

	/* check for current sending message */
	if(client->l_msg){
		rmsg = (libim_msg_t *)listNodeValue(client->l_msg);
		assert(rmsg);

		/* QOS > 0 need ACK */
		dictEntry * ent = dictFind(client->qos, rmsg->topic);
		int qos = (ent? ent->v.u64 : 2);

		if(qos > 0){
			/* check if current ACKed */
			if(sdslen(rmsg->mid) > 0){
				if(difftime(time(0), client->l_time) <= 10)
					goto ret;

				if(client->l_mid == 0){
					/* resend */
					rc = libim_mqtt_send(client, rmsg, MG_MQTT_QOS(qos));
					client->l_time = time(0);
				}
				else{
					hp_log(stderr, "%s: fd=%d, closing unresponsive client='%s' ...\n"
							, __FUNCTION__, c->fd, c->sid);
					/* NOT close(fd) */
					shutdown(((libim_cli *)client)->fd, SHUT_WR);
				}
				goto ret;
			}
		}
		else{ /* QOS=0 at most once */
			rc = libim_mqtt_send(client, rmsg, MG_MQTT_QOS(qos));
			rc = redis_sup_by_topic(proto->c, c->sid, rmsg->topic, rmsg->mid, 0);

			if(gloglevel > 0){
				hp_log(stdout, "%s: Redis sup, fd=%d, client='%s', key/value='%s'/'%s'\n", __FUNCTION__
						, c->fd, c->sid, rmsg->topic, rmsg->mid);
			}
			sdsclear(rmsg->mid);
		}
	}

	/* fetch next message to send */
	listNode * node = 0;

	if(!client->l_msg)
		client->l_msg = listFirst(client->outlist);
	else{
		node = client->l_msg;
		client->l_msg = listNextNode(client->l_msg);
	}

	if(node)
		listDelNode(client->outlist, node);

	if(!client->l_msg)
		goto ret;	/* empty message, nothing to send */

	client->l_time = 0;
	client->l_mid = 0;
ret:
	return rc;
}


static libim_proto const _proto = {
	.new = libim_rcli_new,
	.unpack = hp_libim_unpack_mqtt,
	.delete = libim_rcli_del,
	.loop = libim_mqtt_loop,
	.dispatch = libim_mqtt_dispatch,
};

int libim_rctx_init(libim_rctx * ctx, hp_epoll * efds
		, int fd, int tcp_keepalive
		, redisAsyncContext * c, redisAsyncContext * (* redis)()
		, int ping_interval)
{
	if(xhmdm_libim_init((libim_ctx *)ctx, efds, fd, tcp_keepalive
			, _proto, 0) != 0)
		return -1;
	if(!(c && redis))
		return -1;
	ctx->c = c;
	ctx->redis = redis;
	ctx->rping_interval = ping_interval;

	return 0;
}

void libim_rctx_uninit(libim_rctx * ctx)
{
	return xhmdm_libim_uninit((libim_ctx *)ctx);
}

/////////////////////////////////////////////////////////////////////////////////////////


/* tests */
#ifndef NDEBUG
#include "sds/sds.h"    /* sds */
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
//		libim_ctx ctxobj, * ctx = &ctxobj;
//		libim_hdr * imhdr = 0;
//		char * body = 0;
//
//		libim_cli * client = proto.new(10, ctx);
//		assert(client);
//		assert(client->fd == 10);
//
//		rc = proto.dispatch(client, imhdr, body);
//		assert(rc == 0);
//
//		proto.delete(client);
//	}

	return rc;
}
#endif /* NDEBUG */
#endif /* _MSC_VER */
