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
#include "rmqtt_io_t.h"
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
size_t rmqtt_parse(char * buf, size_t * nbuf
	, void ** hdrp, void ** bodyp)
{
	int rc;
	if (!(buf && nbuf && hdrp && bodyp))
		return -1;

	rc = -1;
	*hdrp = (void *) 0;
	*bodyp = (void *) 0;

	r_mqtt_message * hdr = calloc(1, sizeof(r_mqtt_message));

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

	hdr->cmd = cmd;
	hdr->qos = MG_MQTT_GET_QOS(header);

	switch (cmd) {
	case MG_MQTT_CMD_CONNECT: {
		p = scanto(p, &hdr->protocol_name);
		if (p > end - 4)
			goto ret;
		hdr->protocol_version = *(uint8_t *) p++;
		hdr->connect_flags = *(uint8_t *) p++;
		hdr->keep_alive_timer = getu16(p);
		p += 2;
		if (p >= end)
			goto ret;
		p = scanto(p, &hdr->client_id);
		if (p > end)
			goto ret;
		if (hdr->connect_flags & MG_MQTT_HAS_WILL) {
			if (p >= end)
				goto ret;
			p = scanto(p, &hdr->will_topic);
		}
		if (hdr->connect_flags & MG_MQTT_HAS_WILL) {
			if (p >= end)
				goto ret;
			p = scanto(p, &hdr->will_message);
		}
		if (hdr->connect_flags & MG_MQTT_HAS_USER_NAME) {
			if (p >= end)
				goto ret;
			p = scanto(p, &hdr->user_name);
		}
		if (hdr->connect_flags & MG_MQTT_HAS_PASSWORD) {
			if (p >= end)
				goto ret;
			p = scanto(p, &hdr->password);
		}
		if (p != end)
			goto ret;

	    hp_log(stdout,
	          "%s: %d %2x %d proto [%.*s] client_id [%.*s] will_topic [%.*s] "
	           "will_msg [%.*s] user_name [%.*s] password [%.*s]\n", __FUNCTION__,
	           (int) len, (int) hdr->connect_flags, (int) hdr->keep_alive_timer,
	           (int) hdr->protocol_name.len, hdr->protocol_name.p,
	           (int) hdr->client_id.len, hdr->client_id.p, (int) hdr->will_topic.len,
	           hdr->will_topic.p, (int) hdr->will_message.len, hdr->will_message.p,
	           (int) hdr->user_name.len, hdr->user_name.p, (int) hdr->password.len,
	           hdr->password.p);
		break;
	}
	case MG_MQTT_CMD_CONNACK:
		if (end - p < 2)
			goto ret;
		hdr->connack_ret_code = p[1];
		break;
	case MG_MQTT_CMD_PUBACK:
	case MG_MQTT_CMD_PUBREC:
	case MG_MQTT_CMD_PUBREL:
	case MG_MQTT_CMD_PUBCOMP:
	case MG_MQTT_CMD_SUBACK:
		if (end - p < 2)
			goto ret;
		hdr->message_id = getu16(p);
		p += 2;
		break;
	case MG_MQTT_CMD_PUBLISH: {
		p = scanto(p, &hdr->topic);
		if (p > end)
			goto ret;
		if (hdr->qos > 0) {
			if (end - p < 2)
				goto ret;
			hdr->message_id = getu16(p);
			p += 2;
		}
 	    hdr->payload.p = *bodyp = p;
	    hdr->payload.len = end - p;
		break;
	}
	case MG_MQTT_CMD_SUBSCRIBE:
		if (end - p < 2)
			goto ret;
		hdr->message_id = getu16(p);
		p += 2;
		/*
		 * topic expressions are left in the payload and can be parsed with
		 * `mg_mqtt_next_subscribe_topic`
		 */
 	    hdr->payload.p = *bodyp = p;
	    hdr->payload.len = end - p;
		break;
	default:
		/* Unhandled command */
		break;
	}

	hdr->len = end - buf;

	memmove(buf, buf + hdr->len, *nbuf - hdr->len);
	*nbuf -= hdr->len;

	hdr->cmd += MG_MQTT_EVENT_BASE;

	rc = hdr->len;

ret:
	if(rc <= 0)
		free(hdr);
	else{
		*hdrp = hdr;
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



