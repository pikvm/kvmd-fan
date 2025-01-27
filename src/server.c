/*****************************************************************************
#                                                                            #
#    KVMD-FAN - A small fan controller daemon for PiKVM.                     #
#                                                                            #
#    Copyright (C) 2018-2023  Maxim Devaev <mdevaev@gmail.com>               #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
*****************************************************************************/


#include "server.h"


static void _mhd_log(UNUSED void *ctx, const char *fmt, va_list args);

static enum MHD_Result _mhd_handler(void *v_server, struct MHD_Connection *conn,
	const char *url, const char *method, UNUSED const char *version,
	UNUSED const char *upload_data, size_t *upload_data_size,  // cppcheck-suppress constParameter
	UNUSED void **ctx);


server_s *server_init(bool has_hall, const char *path, bool rm, mode_t mode) {
	server_s *server;
	A_CALLOC(server, 1);
	A_MUTEX_INIT(&server->s_mutex);
	server->s_ok = true;
	server->s_last_fail_ts = -1;
	server->has_hall = has_hall;
	server->fd = -1;

	struct sockaddr_un addr = {0};

#	define MAX_SUN_PATH (sizeof(addr.sun_path) - 1)

	if (strlen(path) > MAX_SUN_PATH) {
		LOG_ERROR("server", "UNIX socket path is too long; max=%zu", MAX_SUN_PATH);
		goto error;
	}

	strncpy(addr.sun_path, path, MAX_SUN_PATH);
	addr.sun_family = AF_UNIX;

#   undef MAX_SUN_PATH

	assert((server->fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);

	if (rm && unlink(path) < 0) {
		if (errno != ENOENT) {
			LOG_PERROR("server", "Can't remove old UNIX socket '%s'", path);
			goto error;
		}
	}

	if (bind(server->fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) < 0) {
		LOG_PERROR("server", "Can't bind HTTP to UNIX socket '%s'", path);
		goto error;
	}
	if (mode && chmod(path, mode) < 0) {
		LOG_PERROR("server", "Can't set permissions %o to UNIX socket '%s'", mode, path);
		goto error;
	}
	if (listen(server->fd, 128) < 0) {
		LOG_PERROR("server", "Can't listen UNIX socket '%s'", path);
		goto error;
	}

	server->mhd = MHD_start_daemon(
		MHD_USE_THREAD_PER_CONNECTION,
		0, NULL, NULL,
		_mhd_handler, server,
		MHD_OPTION_LISTEN_SOCKET, server->fd,
		MHD_OPTION_CONNECTION_TIMEOUT, 10,
		MHD_OPTION_EXTERNAL_LOGGER, _mhd_log, NULL,
		MHD_OPTION_END);
	if (server->mhd == NULL) {
		LOG_PERROR("server", "Can't start HTTP");
		goto error;
	} else {
		LOG_INFO("server", "Listening HTTP on UNIX socket '%s'", path);
	}

	return server;
	error:
		server_destroy(server);
		return NULL;
}

void server_destroy(server_s *server) {
	if (server->mhd) {
		MHD_stop_daemon(server->mhd);
	}
	if (server->fd > 0) {
		close(server->fd);
	}
	A_MUTEX_DESTROY(&server->s_mutex);
	free(server);
}

void server_set_state(server_s *server, float temp_real, float temp_fixed, float speed, unsigned pwm, unsigned rpm, bool ok) {
	A_MUTEX_LOCK(&server->s_mutex);
	server->s_temp_real = temp_real;
	server->s_temp_fixed = temp_fixed;
	server->s_speed = speed;
	server->s_pwm = pwm;
	server->s_rpm = rpm;
	if (server->s_ok != ok) {
		server->s_last_fail_ts = get_now_monotonic();
	}
	server->s_ok = ok;
	A_MUTEX_UNLOCK(&server->s_mutex);
}

static void _mhd_log(UNUSED void *ctx, const char *fmt, va_list args) {
	A_MUTEX_LOCK(&log_mutex);
	char buf[4096];
	vsnprintf(buf, sizeof(buf), fmt, args);
	LOG_ERROR("server", "%s", buf);
	A_MUTEX_UNLOCK(&log_mutex);
}

static enum MHD_Result _mhd_handler(void *v_server, struct MHD_Connection *conn,
	const char *url, const char *method, UNUSED const char *version,
	UNUSED const char *upload_data, size_t *upload_data_size,  // cppcheck-suppress [constParameter, constParameterCallback]
	UNUSED void **ctx) {

	server_s *server = (server_s *)v_server;

	if (strcmp(method, "GET") != 0 || *upload_data_size > 0) {
		return MHD_NO;
	}

	unsigned status = MHD_HTTP_OK;
	char *content_type = "text/plain";
	char *page = "Stub";
	enum MHD_ResponseMemoryMode page_mode = MHD_RESPMEM_PERSISTENT;

	if (!strcmp(url, "/")) {
		content_type = "application/json";
		A_ASPRINTF(page,
			"{\"ok\": true, \"result\": {\"version\": \"%s\"}}\n",
			VERSION);
		page_mode = MHD_RESPMEM_MUST_FREE;

	} else if (!strcmp(url, "/state")) {
		content_type = "application/json";
		A_MUTEX_LOCK(&server->s_mutex);
		A_ASPRINTF(page,
			"{\"ok\": true, \"result\": {"
			"\"service\": {\"now_ts\": %.2Lf},"
			" \"temp\": {\"real\": %.2f, \"fixed\": %.2f},"
			" \"fan\": {\"speed\": %.2f, \"pwm\": %u, \"ok\": %s, \"last_fail_ts\": %.2Lf},"
			" \"hall\": {\"available\": %s, \"rpm\": %u}"
			"}}\n",
			get_now_monotonic(),
			server->s_temp_real,
			server->s_temp_fixed,
			server->s_speed,
			server->s_pwm,
			(server->s_ok ? "true" : "false"),
			server->s_last_fail_ts,
			(server->has_hall ? "true" : "false"),
			server->s_rpm);
		A_MUTEX_UNLOCK(&server->s_mutex);
		page_mode = MHD_RESPMEM_MUST_FREE;

	} else {
		status = MHD_HTTP_NOT_FOUND;
		page = "Not found\n";
	}

	struct MHD_Response *resp;
	assert(resp = MHD_create_response_from_buffer(strlen(page), page, page_mode));
	assert(MHD_add_response_header(resp, "Content-Type", content_type) == MHD_YES);

	enum MHD_Result result = MHD_queue_response(conn, status, resp);
	MHD_destroy_response(resp);
	return result;
}
