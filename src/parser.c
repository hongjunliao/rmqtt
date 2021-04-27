/*!
* This file is PART of rmqtt project
* @date 2019/1/17
*
* libim from IM project
* */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "redis/src/adlist.h" /* list */
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "hp/hp_log.h"
#include "hp/hp_libc.h"
#include "protocol.h"
#include "server.h"
/////////////////////////////////////////////////////////////////////////////////////////

static uint16_t getu16(const char *p) {
  const uint8_t *up = (const uint8_t *) p;
  return (up[0] << 8) + up[1];
}

static const char *scanto(const char *p, struct rmqtt_str *s) {
  s->len = getu16(p);
  s->p = p + 2;
  return s->p + s->len;
}

/*
 * skip unrecognized data and continue read and parse?
 * if NOT set, return as parse error
 *  */
size_t rmqtt_parse(char * buf, size_t * nbuf, int flags
	, hp_iohdr_t ** iohdrp, char ** bodyp)
{
	int rc;
	if (!(buf && nbuf && iohdrp && bodyp))
		return -1;

	rc = -1;
	*iohdrp = (void *) 0;
	*bodyp = (void *) 0;

	hp_iohdr_t * hdr = calloc(1, sizeof(hp_iohdr_t));
	r_mqtt_message * imhdr = &hdr->mqtt;

	uint8_t header;
	uint32_t len, len_len; /* must be 32-bit, see #1055 */
	const char *p, *end, *eop = buf + *nbuf;
	unsigned char lc = 0;
	int cmd;

	if (*nbuf < 2){
		rc = 0;
		goto ret;
	}
	header = buf[0];
	cmd = header >> 4;

	/* decode mqtt variable length */
	len = len_len = 0;
	p = buf + 1;
	while (p < eop) {
		lc = *((const unsigned char *) p++);
		len += (lc & 0x7f) << 7 * len_len;
		len_len++;
		if (!(lc & 0x80))
			break;
		if (len_len > sizeof(len))
			goto ret;
	}

	end = p + len;
	if ((lc & 0x80) || end > eop){
		rc = 0;
		goto ret;
	}

	imhdr->cmd = cmd;
	imhdr->qos = MG_MQTT_GET_QOS(header);

	switch (cmd) {
	case MG_MQTT_CMD_CONNECT: {
		p = scanto(p, &imhdr->protocol_name);
		if (p > end - 4)
			goto ret;
		imhdr->protocol_version = *(uint8_t *) p++;
		imhdr->connect_flags = *(uint8_t *) p++;
		imhdr->keep_alive_timer = getu16(p);
		p += 2;
		if (p >= end)
			goto ret;
		p = scanto(p, &imhdr->client_id);
		if (p > end)
			goto ret;
		if (imhdr->connect_flags & MG_MQTT_HAS_WILL) {
			if (p >= end)
				goto ret;
			p = scanto(p, &imhdr->will_topic);
		}
		if (imhdr->connect_flags & MG_MQTT_HAS_WILL) {
			if (p >= end)
				goto ret;
			p = scanto(p, &imhdr->will_message);
		}
		if (imhdr->connect_flags & MG_MQTT_HAS_USER_NAME) {
			if (p >= end)
				goto ret;
			p = scanto(p, &imhdr->user_name);
		}
		if (imhdr->connect_flags & MG_MQTT_HAS_PASSWORD) {
			if (p >= end)
				goto ret;
			p = scanto(p, &imhdr->password);
		}
		if (p != end)
			goto ret;

	    hp_log(stdout,
	          "%s: %d %2x %d proto [%.*s] client_id [%.*s] will_topic [%.*s] "
	           "will_msg [%.*s] user_name [%.*s] password [%.*s]\n", __FUNCTION__,
	           (int) len, (int) imhdr->connect_flags, (int) imhdr->keep_alive_timer,
	           (int) imhdr->protocol_name.len, imhdr->protocol_name.p,
	           (int) imhdr->client_id.len, imhdr->client_id.p, (int) imhdr->will_topic.len,
	           imhdr->will_topic.p, (int) imhdr->will_message.len, imhdr->will_message.p,
	           (int) imhdr->user_name.len, imhdr->user_name.p, (int) imhdr->password.len,
	           imhdr->password.p);
		break;
	}
	case MG_MQTT_CMD_CONNACK:
		if (end - p < 2)
			goto ret;
		imhdr->connack_ret_code = p[1];
		break;
	case MG_MQTT_CMD_PUBACK:
	case MG_MQTT_CMD_PUBREC:
	case MG_MQTT_CMD_PUBREL:
	case MG_MQTT_CMD_PUBCOMP:
	case MG_MQTT_CMD_SUBACK:
		if (end - p < 2)
			goto ret;
		imhdr->message_id = getu16(p);
		p += 2;
		break;
	case MG_MQTT_CMD_PUBLISH: {
		p = scanto(p, &imhdr->topic);
		if (p > end)
			goto ret;
		if (imhdr->qos > 0) {
			if (end - p < 2)
				goto ret;
			imhdr->message_id = getu16(p);
			p += 2;
		}
 	    imhdr->payload.p = *bodyp = p;
	    imhdr->payload.len = end - p;
		break;
	}
	case MG_MQTT_CMD_SUBSCRIBE:
		if (end - p < 2)
			goto ret;
		imhdr->message_id = getu16(p);
		p += 2;
		/*
		 * topic expressions are left in the payload and can be parsed with
		 * `mg_mqtt_next_subscribe_topic`
		 */
 	    imhdr->payload.p = *bodyp = p;
	    imhdr->payload.len = end - p;
		break;
	default:
		/* Unhandled command */
		break;
	}

	imhdr->len = end - buf;

	memmove(buf, buf + imhdr->len, *nbuf - imhdr->len);
	*nbuf -= imhdr->len;

	imhdr->cmd += MG_MQTT_EVENT_BASE;

	rc = imhdr->len;

ret:
	if(rc <= 0)
		free(hdr);
	else{
		*iohdrp = hdr;
	}
	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

int test_mqtt_parser_main(int argc, char ** argv)
{
	int r = 0;
	return r;
}

#endif /* NDEBUG */



