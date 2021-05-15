 /*!
 * This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/7/9
 *
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "Win32_Interop.h"
#include "hp/sdsinc.h"	/* sds */
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OPTPARSE_IMPLEMENTATION
#define OPTPARSE_API static
#include "optparse/optparse.h"  /* option */
//#include "getopt.h"			/* getopt_long */
#include "hp/string_util.h"     /* hp_fread */
#include "hp/hp_mqtt.h"         /* hp_mqtt */
#include "hp/hp_config.h"   /* hp_config_t */
#include "hp/hp_libc.h"
#include "hp/hp_err.h"
#include "hp/hp_mqtt.h" /* hp_mqtt */
#include <uv.h> /* uv_fs_open */
/////////////////////////////////////////////////////////////////////////////////////////

typedef struct pub_file_cli {
	hp_mqtt base;
	uv_file f;
	sds file;
	uv_req_t dir_req;
	uv_fs_t open_req;
	uv_fs_t write_req;
	uv_buf_t iov[1];
	int total;
} pub_file_cli;

/* data for test */
struct pub_file_test {
	uv_loop_t * uvloop;
	sds mqtt_addr, mqtt_user, mqtt_pwd;
	sds topic;
	sds file;
	sds ids;
	sds dir;
	int is_conn;

	int done;
	int err;
	hp_err_t errstr;

	int total;

	hp_mqtt * mqttcli;

	uv_fs_t open_req;
	uv_fs_t read_req;
	uv_fs_t write_req;
	sds buffer;
	uv_buf_t iov;
};

static struct pub_file_test * s_test = 0;
static char const * cfg(char const * id) {
	if(strcmp(id, "mqtt.addr") == 0) { return s_test->mqtt_addr; }
	else if (strcmp(id, "mqtt.user") == 0) { return s_test->mqtt_user; }
	else if (strcmp(id, "mqtt.pwd") == 0) { return s_test->mqtt_pwd; }
	else return "0";
}
hp_config_t g_conf = cfg;
/////////////////////////////////////////////////////////////////////////////////////////

static void pub_file__connect_cb(hp_mqtt * mqtt, int err, char const * errstr, void * arg)
{
	assert(mqtt);
	assert(arg);
	struct pub_file_test * t = arg;
	t->is_conn = hp_max(t->is_conn, 0) + 1;

	char * topics[] = { t->topic };
	int qoses[] = { 2 };
	hp_mqtt_sub(mqtt, 1, topics, qoses, 0);

}

static void pub_file__disconnect_cb(hp_mqtt * mqtt, void * arg)
{
	assert(mqtt);
	assert(arg);
	struct pub_file_test * t = arg;
	t->is_conn = -1;

	printf("%s: disconnected, broker='%s'\n", __FUNCTION__, "");
}

static void pub_file__message_cb(
	hp_mqtt * mqtt, char const * topic, char const * msg, int len, void * arg)
{
	assert(mqtt);
	assert(arg);
	struct pub_file_test * t = arg;
	pub_file_cli  * mqttcli = (pub_file_cli  *)mqtt;

	if(mqttcli->f <= 0){
		sds path =  sdscatfmt(sdsempty(), "%s/%s", t->dir, t->topic);
		uv_fs_mkdir(s_test->uvloop, &mqttcli->dir_req, path, 0, NULL);

		mqttcli->file = sdscatfmt(sdsempty(), "%s/%s", path, mqtt->id);
		mqttcli->f = uv_fs_open(t->uvloop, &mqttcli->open_req, mqttcli->file, O_CREAT | O_APPEND, 0, NULL);
		if(mqttcli->f <= 0){
			s_test->err = -1;
			strcpy(s_test->errstr, uv_strerror(mqttcli->f));
		}

		sdsfree(path);
	}
	if(mqttcli->f > 0){
		mqttcli->iov[0] = uv_buf_init(msg, len);
		uv_fs_write(t->uvloop, &mqttcli->write_req, mqttcli->f, mqttcli->iov, 1, 0, NULL);

		mqttcli->total += len;

		char s1[128] = "";
		fprintf(stdout, "%s: writing '%s', %s\n", __FUNCTION__, mqttcli->file
				, byte_to_mb_kb_str_r(mqttcli->total, "%-3.1f %cB", s1));
	}
}

static void pub_file__sub_cb(hp_mqtt * mqtt, void * arg)
{
	assert(mqtt);
	assert(arg);
}

static void pub_file__connect_cb_2(hp_mqtt * mqtt, int err, char const * errstr, void * arg)
{
	assert(mqtt);
	assert(arg);
	struct pub_file_test * t = arg;
	t->is_conn = hp_max(t->is_conn, 0) + 1;
}

static void pub_file__message_cb_2(
	hp_mqtt * mqtt, char const * topic, char const * msg, int len, void * arg)
{
	assert(mqtt);
	assert(arg);
}

/////////////////////////////////////////////////////////////////////////////////////////
/*
 * file I/O
 */
static void on_read(uv_fs_t *req) {
	assert(req);
	int rc;

	if (req->result < 0) {
		snprintf(s_test->errstr, sizeof(s_test->errstr), "Read error: %s\n", uv_strerror(req->result));
		s_test->err = -2;
	}
	else if (req->result == 0) {
		uv_fs_t close_req;
		// synchronous
		uv_fs_close(s_test->uvloop, &close_req, s_test->open_req.result, NULL);
		s_test->done = 1;
	}
	else if (req->result > 0) {
		//s_test->iov.len = req->result;
		//uv_fs_write(s_test->uvloop, &s_test->write_req, 1, &s_test->iov, 1, -1, on_write);

		s_test->total += req->result;
		MQTTAsync_token token;
		rc = hp_mqtt_pub(s_test->mqttcli, s_test->topic, 2, s_test->iov.base, req->result, &token);
		if (rc != 0) {
			s_test->err = -4;
			uv_fs_t close_req;
			// synchronous
			uv_fs_close(s_test->uvloop, &close_req, s_test->open_req.result, NULL);
			goto ret;
		}
		else {
			for (rc = MQTTASYNC_FAILURE; rc != MQTTASYNC_SUCCESS; ) {
				rc = MQTTAsync_waitForCompletion(s_test->mqttcli->context, token, 200);
				if(rc == MQTTASYNC_DISCONNECTED) {
					s_test->err = -5;
					uv_fs_t close_req;
					uv_fs_close(s_test->uvloop, &close_req, s_test->open_req.result, NULL);
					goto ret;
				}
			}
			s_test->iov.len = 1024 * 16;
			uv_fs_read(s_test->uvloop, &s_test->read_req, s_test->open_req.result,
				&s_test->iov, 1, s_test->total, on_read);
		}
		char s1[128] = "";
		fprintf(stdout, "%s: Pub %s\n", __FUNCTION__, byte_to_mb_kb_str_r(s_test->total, "%-3.1f %cB", s1));
	}
ret:
	return;
}

static void on_open(uv_fs_t *req)
{
	// The request passed to the callback is the same as the one the call setup
	// function was passed.
	assert(req == &s_test->open_req);
	if (req->result >= 0) {
		s_test->iov = uv_buf_init(s_test->buffer, 1024 * 16);
		uv_fs_read(s_test->uvloop, &s_test->read_req, req->result,
			&s_test->iov, 1, -1, on_read);
	}
	else {
		s_test->err = -1;
		snprintf(s_test->errstr, sizeof(s_test->errstr), "error opening file: %s\n", uv_strerror((int)req->result));
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char ** argv)
{
	int i, rc;

	struct pub_file_test testobj = {
		.uvloop = uv_default_loop(),
		.mqtt_addr = sdsempty(),
		.file = sdsempty(),
		.topic = sdsempty(),
		.mqtt_user = sdsempty(),
		.mqtt_pwd = sdsempty(),
		.ids = sdsempty(),
		.dir = sdsempty(),
		.buffer = sdsMakeRoomFor(sdsempty(), 1024 * 16),
	};

	sds txt = 0;
	char const * txt_sep = "\n";
	s_test = &testobj;
	int mode = 0;

	/* parse argc/argv */
	struct optparse_long longopts[] = {
		{"mqtt_addr", 'm', OPTPARSE_REQUIRED},
		{"user",      'u', OPTPARSE_REQUIRED},
		{"password",  'p', OPTPARSE_REQUIRED},
		{"topic",     't', OPTPARSE_REQUIRED},
		{"file",      'f', OPTPARSE_REQUIRED},
		{"ids",       'i', OPTPARSE_REQUIRED},
		{"dir",       'd', OPTPARSE_REQUIRED},
		{0}
	};
	struct optparse options;
	optparse_init(&options, argv);

	for (; (rc = optparse_long(&options, longopts, NULL)) != -1; ) {
		char const * arg = (options.optarg ? options.optarg : "");
		switch (rc) {
		case 'm': { s_test->mqtt_addr = sdscpy(s_test->mqtt_addr, arg); break; }
		case 'u': { s_test->mqtt_user = sdscpy(s_test->mqtt_user, arg); break; }
		case 'p': { s_test->mqtt_pwd = sdscpy(s_test->mqtt_pwd, arg); break; }
		case 't': { s_test->topic = sdscpy(s_test->topic, arg); break; }
		case 'f': { s_test->file = sdscpy(s_test->file, arg); break; }
		case 'i': { s_test->ids = sdscpy(s_test->ids, arg); break; }
		case 'd': { s_test->dir = sdscpy(s_test->dir, arg); break; }
		case '?':
			fprintf(stdout, "%s --mqtt_addr,-m=STRING --file,-f=STRING --dir,d=STRING\n", argv[0]);
			return 0;
		}
	}

	mode = ((sdslen(s_test->mqtt_addr) > 0 && sdslen(s_test->topic) > 0
				&& sdslen(s_test->ids) > 0 && sdslen(s_test->dir) > 0 )? 2 : mode);
	mode = ((sdslen(s_test->mqtt_addr) > 0 && sdslen(s_test->topic) > 0 && sdslen(s_test->file) > 0 )? 1 : mode);

	if (mode == 2) {
		if (strncmp(s_test->ids, "file://", 7) == 0)
			txt = hp_fread(s_test->ids + 7);
		else {
			txt = sdsnewlen(s_test->ids, strlen(s_test->ids));
			txt_sep = ",";
		}

		int count = 0;
		sds * s = sdssplitlen(txt, strlen(txt), txt_sep, strlen(txt_sep), &count);

		for (i = 0; i < count; ++i) {
			if (sdslen(s[i]) == 0)
				continue;

			pub_file_cli * mqttcli = (pub_file_cli *)calloc(1, sizeof(pub_file_cli));
			rc = hp_mqtt_init((hp_mqtt *)mqttcli, s[i]
				, pub_file__connect_cb, pub_file__message_cb, pub_file__disconnect_cb, 0
				, s_test->mqtt_addr, s_test->mqtt_user, s_test->mqtt_pwd
				, 0, s_test, 0);
			assert(rc == 0);

			rc = hp_mqtt_connect((hp_mqtt *)mqttcli);
			assert(rc == 0);
		}
		sdsfreesplitres(s, count);

		for (; !s_test->err && !s_test->done;) {
			uv_run(s_test->uvloop, UV_RUN_NOWAIT);
		}

		if(s_test->err){
			fprintf(stderr, "%s: Sub '%s/%s' failed, err/errstr=%d/'%s'\n", __FUNCTION__
				, s_test->dir, s_test->topic, s_test->err, s_test->errstr);
		}
	}
	else if (mode == 1) {
		uv_fs_t dir_req;
		uv_fs_mkdir(s_test->uvloop, &dir_req, s_test->dir, 0, NULL);

		s_test->mqttcli = (hp_mqtt *)calloc(1, sizeof(hp_mqtt));
		rc = hp_mqtt_init(s_test->mqttcli, "pub_file"
			, pub_file__connect_cb_2, pub_file__message_cb_2, pub_file__disconnect_cb, 0
			, s_test->mqtt_addr, s_test->mqtt_user, s_test->mqtt_pwd
			, 0, s_test, 0);
		assert(rc == 0);

		rc = hp_mqtt_connect(s_test->mqttcli);
		assert(rc == 0);
		while (!(s_test->err || s_test->is_conn > 0)) { usleep(200); }

		if(!s_test->err){
			uv_fs_open(s_test->uvloop, &s_test->open_req, s_test->file, O_RDONLY, 0, on_open);
			for (; !s_test->err && !s_test->done;) {
				uv_run(s_test->uvloop, UV_RUN_NOWAIT);
			}

			uv_fs_req_cleanup(&s_test->open_req);
			uv_fs_req_cleanup(&s_test->read_req);
			uv_fs_req_cleanup(&s_test->write_req);
		}

		hp_mqtt_disconnect(s_test->mqttcli);
		hp_mqtt_uninit(s_test->mqttcli);

		if(s_test->err){
			fprintf(stderr, "%s: Pub file '%s' failed, err/errstr=%d/'%s'\n", __FUNCTION__
				, s_test->file, s_test->err, s_test->errstr);
		}
	}

	return s_test->err;
}
