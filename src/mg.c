/*!
 * This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2021/3/15
 *
 * HTTP using mongoose
 * */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef _WIN32
#include "redis/src/Win32_Interop/win32_types.h"
#include "redis/src/Win32_Interop/Win32_FDAPI.h"
#include "redis/src/Win32_Interop/Win32_Portability.h"
#endif

#include "mongoose/mongoose.h"
#include "hp/hp_config.h"	/* hp_config_t */
#include "hp/hp_pub.h"
#include "hp/hp_cjson.h"
#include "hp/hp_err.h"

extern redisAsyncContext * g_redis;
extern hp_config_t g_conf;
/////////////////////////////////////////////////////////////////////////////////////////

/**!
 * https://docs.emqx.cn/broker/latest/advanced/http-api.html#消息发布
 * {"topic":"a/b/c","payload":"Hello World", "encoding":"base64","qos":1,"retain":false,"clientid":"example"}
 * {"code":0}
 */
static sds api_pub(const char *ptr, size_t len)
{
	int rc;
	hp_err_t err;
	cJSON * ijson = cjson_parselen(ptr, len);
	if(!ijson) {
		rc = -1;
		strncpy(err, "invalid json", sizeof(hp_err_t));
		goto ret;
	}
	err[0] =  '\0';

	sds topic = sdscatfmt(sdsempty(), "%s:%s", g_conf("redis.topic"), cjson_sval(ijson, "topic", ""));
	char const * payload = cjson_sval(ijson, "payload", "");
	rc = hp_pub(g_redis, topic, payload, strlen(payload), 0);

	sdsfree(topic);
	cJSON_Delete(ijson);

ret:
	return sdscatfmt(sdsempty(), "{\"code\":\"%i\",\"err\":\"%s\"}",
			(rc == 0? 200 : 400), err);
}
/////////////////////////////////////////////////////////////////////////////////////////

// Authenticated user.
// A user can be authenticated by:
//   - a name:pass pair
//   - a token
// When a user is shown a login screen, she enters a user:pass. If successful,
// a server returns user info which includes token. From that point on,
// client can use token for authentication. Tokens could be refreshed/changed
// on a server side, forcing clients to re-login.
struct user {
	char *name, *pass, token[256];
};

// Parse HTTP requests, return authenticated user or NULL
static struct user *getuser(struct mg_http_message *hm, struct user *u) {
	if(!u) { return 0; }
	// In production, make passwords strong and tokens randomly generated
	// In this example, user list is kept in RAM. In production, it can
	// be backed by file, database, or some other method.
	struct user U = { g_conf("mqtt.user"), g_conf("mqtt.pwd"), "admin_token" };
	char user[256], pass[256];
	mg_http_creds(hm, user, sizeof(user), pass, sizeof(pass));
	if (user[0] != '\0' && pass[0] != '\0') {
		// Both user and password is set, search by user/password
			*u = U;
			if (strcmp(user, u->name) == 0 && strcmp(pass, u->pass) == 0){
				return u;
			}
	} else if (user[0] == '\0') {
		// Only password is set, search by token
		*u = U;
		if (strcmp(pass, u->token) == 0)
			return u;
	}
	return NULL;
}

// HTTP request handler function
static void cb(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
	if (ev == MG_EV_HTTP_MSG) {
		struct mg_http_message *hm = (struct mg_http_message *) ev_data;
		struct user uobj, *u = getuser(hm, &uobj);
		// LOG(LL_INFO, ("%p [%.*s] auth %s", c->fd, (int) hm->uri.len, hm->uri.ptr,
		// u ? u->name : "NULL"));
		if (u == NULL && mg_http_match_uri(hm, "/api/#")) {
			// All URIs starting with /api/ must be authenticated
			mg_printf(c, "%s",
					"HTTP/1.1 403 Denied\r\nContent-Length: 0\r\n\r\n");
		} else if (mg_http_match_uri(hm, "/api/log/static")) {
			mg_http_serve_file(c, hm, "log.txt", "text/plain", "");
		} else if (mg_http_match_uri(hm, "/api/log/live")) {
			c->label[0] = 'L';  // Mark that connection as live log listener
			mg_printf(c,
					"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
		} else if (mg_http_match_uri(hm, "/api/login")) {

			mg_printf(c,
					"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
			mg_http_printf_chunk(c, "{\"user\":\"%s\",\"token\":\"%s\"}\n",
					u->name, u->token);
			mg_http_printf_chunk(c, "");

		} else if (mg_http_match_uri(hm, "/api/pub")) {

			sds s = api_pub(hm->body.ptr, hm->body.len);

			mg_printf(c,
					"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
			mg_http_printf_chunk(c, "%s\n", s);
			mg_http_printf_chunk(c, "");

			sdsfree(s);
		}
		else {
			struct mg_http_serve_opts opts = { .root_dir = g_conf("web_root") };
			mg_http_serve_dir(c, ev_data, &opts);
		}
	}
}

int mg_init(struct mg_mgr * mgr, struct mg_timer * t1, struct mg_timer * t2) {
	mg_log_set("2");                              // Set to 3 to enable debug
	mg_mgr_init(mgr);
	struct mg_connection * nc = mg_http_listen(mgr, g_conf("url"), cb, mgr);

	return (nc ? 0 : -1);
}
