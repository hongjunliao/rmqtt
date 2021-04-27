/*!
 * This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/11
 *
 * message dispatch for MQTT
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "Win32_Interop.h"
#include "redis/src/adlist.h" /* list */
#include "hp/sdsinc.h"     	/* sds */
#include <unistd.h>
#include <string.h> 	/* strlen */
#include <stdio.h>
#include <string.h>     /* memset, ... */
#include <errno.h>      /* errno */
#include <assert.h>     /* define NDEBUG to disable assertion */
#include <time.h>
#include <stdlib.h>
#include "zlog.h"
#include "c-vector/cvector.h"
#include "hp/hp_log.h"     /* hp_log */
#include "hp/hp_libc.h"    /* hp_min */
#include "hp/hp_pub.h"     /*  */
#include "hp/str_dump.h"   /* dumpstr */
#include "hp/string_util.h"/* sdslen_null */
#include "hp/hp_redis.h"/* hp_redis_uninit */
#include "hp/hp_config.h"	/* hp_config_t */
#include "hp/str_dump.h"
#include "protocol.h"
#include "rmqtt_io_t.h"	/* rmqtt_io_t */
#include "redis_pub.h"
#include "server.h"

#ifdef _MSC_VER
#define SHUT_WR SD_SEND 
#endif /* _MSC_VER */

extern int gloglevel;
/////////////////////////////////////////////////////////////////////////////////////

static void sub_cb(hp_sub_t * s, char const * topic, sds id, sds msg)
{
	assert(s && msg && s->arg._1);
	int rc;

	rmqtt_io_ctx * ioctx = (rmqtt_io_ctx *)s->arg._1;
	hp_io_t key = { 0 };
	key.id = s->arg._2;
	listNode * node = listSearchKey(((hp_io_ctx *)ioctx)->iolist, &key);
	rmqtt_io_t * io = node? (rmqtt_io_t *)listNodeValue(node) : 0;

	if(!topic){
		hp_log(stderr, "%s: erorr %s\n", __FUNCTION__, msg);

		/* close fd to force disconnect of this io */
		if(io){
			io->subc = 0;
			/* NOT close(fd) */
			shutdown(((hp_io_t *)io)->fd, SHUT_WR);
		}

		return;
	}
	else if(topic[0] == '\0') {
		hp_log(stdout, "%s: unsubscribed\n", __FUNCTION__);

		hp_redis_uninit(s->subc);

		return;
	}
	if(!io) { return; }

	listNode * it = listSearchKey(((rmqtt_io_t *)io)->outlist, id);
	if(it) { return; }

	if(gloglevel > 0){
		hp_log(stdout, "%s: Redis message, fd=%d, io='%s', key/value='%s'/'%s'\n", __FUNCTION__
				, ((hp_io_t *)io)->fd
				, io->sid, topic
				, dumpstr(msg, sdslen(msg), 8));
	}
	rc = rmqtt_io_append(io, topic, id, msg, 1);
	assert(rc == 0);

	return;
}

static int mg_mqtt_next_subscribe_topic(struct r_mqtt_message *msg,
                                 struct rmqtt_str *topic, uint8_t *qos, int pos) {
  unsigned char *buf = (unsigned char *) msg->payload.p + pos;
  int new_pos;

  if ((size_t) pos >= msg->payload.len) return -1;

  topic->len = buf[0] << 8 | buf[1];
  topic->p = (char *) buf + 2;
  new_pos = pos + 2 + topic->len + 1;
  if ((size_t) new_pos > msg->payload.len) return -1;
  *qos = buf[2 + topic->len];
  return new_pos;
}

static int libim_mqtt_conack(rmqtt_io_t * io, uint8_t cmd,
        uint8_t flags, size_t len, uint8_t return_code)
{
	int rc;

	rc = rmqtt_io_send_header(io, cmd, flags, len);

	uint8_t unused = 0;
	rc = hp_io_write((hp_io_t *)io, &unused, 1, (void *)-1, 0);
	rc = hp_io_write((hp_io_t *)io, &return_code, 1, (void *)-1, 0);

	return rc;
}

static int libim_mqtt_suback(rmqtt_io_t * io, uint8_t *qoss, size_t qoss_len,
        uint16_t message_id)
{
	int rc;
	size_t i;
	uint16_t netbytes;
	rc = rmqtt_io_send_header(io, MG_MQTT_CMD_SUBACK, 0,
			2 + qoss_len);
	netbytes = htons(message_id);
	hp_io_write((hp_io_t *)io, &netbytes, 2, (void *)-1, 0);

	for (i = 0; i < qoss_len; i++) {
		hp_io_write((hp_io_t *)io, &qoss[i], 1, (void *)-1, 0);
	}
	return rc;
}

static int libim_mqtt_pong(rmqtt_io_t * io) {
  return rmqtt_io_send_header(io, MG_MQTT_CMD_PINGRESP, 0, 0);
}

int rmqtt_dispatch(rmqtt_io_t * io, hp_iohdr_t * iohdr, char * body) 
{
	if (!(io && iohdr && io->ioctx)){ return -1; }

	int i, rc = 0;
	r_mqtt_message * msg = &iohdr->mqtt;
	rmqtt_io_ctx * ioctx = (rmqtt_io_ctx *) io->ioctx;

    if (msg->cmd == MG_EV_MQTT_PINGREQ) {
    	io->l_time = time(0);
		if(gloglevel > 0){
			hp_log(stdout, "%s: <== fd=%d, PINGREQ\n", __FUNCTION__, ((hp_io_t *)io)->fd);
		}
    	libim_mqtt_pong(io);
    	rc = 0;
    	goto ret;
    }

	switch (msg->cmd) {
	case MG_EV_MQTT_CONNECT:
		io->sid = sdscpylen(io->sid, msg->client_id.p, msg->client_id.len);
		if(gloglevel > 0){
			hp_log(stdout, "%s: <== fd=%d, CONNECT with id/username/password='%s'/'%.*s'/'%.*s'\n", __FUNCTION__
					, ((hp_io_t *)io)->fd, (msg->client_id.len > 0? io->sid : "")
							, (int) msg->user_name.len, msg->user_name.p, (int) msg->password.len,
			           msg->password.p);
		}
		rc = libim_mqtt_conack(io, MG_MQTT_CMD_CONNACK, 0, 2, MG_EV_MQTT_CONNACK_ACCEPTED);

		if(!io->subc){
			io->subc = ioctx->redis();

			hp_sub_arg_t arg = {io->ioctx, ((hp_io_t *)io)->id};
			io->subc = redis_subc_arg(ioctx->c, io->subc, io->sid, sub_cb, arg);
		}

		rc = redis_sub(io, 0, 0, 0);

		break;
	case MG_EV_MQTT_SUBSCRIBE: {
		sds * topics = 0;
		uint8_t * qoss = 0;

		cvector_init(topics, 1);
		cvector_init(qoss, 1);

		int pos;
		uint8_t qos;
		struct rmqtt_str topic;
		for (pos = 0; pos < (int) msg->payload.len &&
			(pos = mg_mqtt_next_subscribe_topic(msg, &topic, &qos, pos))!= -1;) {

			cvector_push_back(topics, sdsnewlen(topic.p, topic.len));
			cvector_push_back(qoss, qos);
		}

		if (pos == (int) msg->payload.len) {

			if(io->subc){
				rc = redis_sub(io, cvector_size(topics), topics, qoss);
			}

			rc = libim_mqtt_suback(io, qoss, cvector_size(qoss), msg->message_id);

			if(gloglevel > 0){
				hp_log(stdout, "%s: <== fd=%d, SUBSCRIBE topics=%d\n", __FUNCTION__, ((hp_io_t *)io)->fd, cvector_size(topics));
			}
		} else {
			/* We did not fully parse the payload, something must be wrong. */
		}

		for (i = 0; i < cvector_size(topics); ++i)
			sdsfree(topics[i]);
		cvector_free(topics);
		cvector_free(qoss);
	}
		break;
	case MG_EV_MQTT_PUBLISH:{
		sds topic = sdsnewlen(msg->topic.p, msg->topic.len);
		void * data = (void *)msg->payload.p;
		int len = msg->payload.len;
		int flags = (strcmp(io->sid, topic) == 0? 0 : 8);
		rc = redis_pub(ioctx->c, topic, data, len, flags, 0);

		if(msg->qos > 0){
			uint16_t netbytes;
			rc = rmqtt_io_send_header(io,
					(msg->qos > 1? MG_MQTT_CMD_PUBREC : MG_MQTT_CMD_PUBACK), MG_MQTT_QOS(msg->qos), 2);
			assert(rc == 0);

			netbytes = htons(msg->message_id);
			hp_io_write((hp_io_t *)io, &netbytes, 2, (void *)-1, 0);
		}

		if(gloglevel > 0){
			hp_log(stdout, "%s: <== fd=%d, PUBLISH topic='%s', msgid/QOS=%u/%u, playload=%u/'%s', return=%d\n", __FUNCTION__
					, ((hp_io_t *)io)->fd, topic, msg->message_id, msg->qos, len, dumpstr(data, len, 8), rc);
		}
		sdsfree(topic);
	}
		break;
	case MG_EV_MQTT_PUBREC:{
		if(msg->message_id == io->l_mid){
			io->l_time = time(0);
			rmqtt_io_send_header(io, MG_MQTT_CMD_PUBREL, MG_MQTT_QOS(msg->qos), 2);
			uint16_t netbytes = htons(msg->message_id);
			hp_io_write((hp_io_t *)io, &netbytes, 2, (void *)-1, 0);
		}
	}
		break;
	case MG_EV_MQTT_PUBACK:
	case MG_EV_MQTT_PUBCOMP:{
		if(msg->message_id == io->l_mid && io->l_msg){
    		rmqtt_rmsg_t * rmsg = (rmqtt_rmsg_t *)listNodeValue(io->l_msg);
			rc = redis_sup_by_topic(ioctx->c, io->sid, rmsg->topic, rmsg->mid, 0);

			if(gloglevel > 0){
				hp_log(stdout, "%s: Redis sup, fd=%d, io='%s', key/value='%s'/'%s'\n", __FUNCTION__
						, ((hp_io_t *)io)->fd, io->sid, rmsg->topic, rmsg->mid);
			}
			/* this message is ACKed */
			sdsclear(rmsg->mid);
		}
	}
		break;
	case MG_EV_MQTT_PUBREL:
		rmqtt_io_send_header(io, MG_MQTT_CMD_PUBCOMP, 0, 0);
		break;
	case MG_EV_MQTT_DISCONNECT:
		shutdown(((hp_io_t *)io)->fd, SHUT_WR);
		break;
	default:
		hp_log(stderr, "%s: <== fd=%d, unknown MQTT message, id=%d\n", __FUNCTION__, ((hp_io_t *)io)->fd, msg->cmd);
		break;
	}
ret:
	free(iohdr);

	return rc;
}
/////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#include <assert.h>        /* assert */

int test_libim_dispatch_mqtt_main(int argc, char ** argv)
{
	int r = 0;

	r = rmqtt_dispatch(0, 0, 0); assert(r != 0);

	return 0;
}
#endif
