/*
 * Copyright (c) 2013-2018 Joris Vink <joris@coders.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/stat.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>
#include <fcntl.h>
#include <pwd.h>

#include "kore.h"
#include "http.h"

#if defined(KORE_USE_PGSQL)
#include "pgsql.h"
#endif

#if defined(KORE_USE_TASKS)
#include "tasks.h"
#endif

#if defined(KORE_USE_PYTHON)
#include "python_api.h"
#endif

/* XXX - This is becoming a clusterfuck. Fix it. */

static int		configure_load(char *);

#if defined(KORE_SINGLE_BINARY)
static FILE		*config_file_write(void);
extern u_int8_t		asset_builtin_kore_conf[];
extern u_int32_t	asset_len_builtin_kore_conf;
#endif

static int		configure_include(char *);
static int		configure_bind(char *);
static int		configure_bind_unix(char *);
static int		configure_domain(char *);
static int		configure_root(char *);
static int		configure_runas(char *);
static int		configure_workers(char *);
static int		configure_pidfile(char *);
static int		configure_rlimit_nofiles(char *);
static int		configure_max_connections(char *);
static int		configure_accept_threshold(char *);
static int		configure_set_affinity(char *);
static int		configure_socket_backlog(char *);

#if defined(KORE_USE_PLATFORM_PLEDGE)
static int		configure_add_pledge(char *);
#endif

#if !defined(KORE_NO_TLS)
static int		configure_rand_file(char *);
static int		configure_certfile(char *);
static int		configure_certkey(char *);
static int		configure_tls_version(char *);
static int		configure_tls_cipher(char *);
static int		configure_tls_dhparam(char *);
static int		configure_keymgr_root(char *);
static int		configure_keymgr_runas(char *);
static int		configure_client_verify(char *);
static int		configure_client_verify_depth(char *);
#endif

#if !defined(KORE_NO_HTTP)
static int		configure_filemap(char *);
static int		configure_restrict(char *);
static int		configure_handler(int, char *);
static int		configure_static_handler(char *);
static int		configure_dynamic_handler(char *);
static int		configure_accesslog(char *);
static int		configure_http_header_max(char *);
static int		configure_http_body_max(char *);
static int		configure_filemap_ext(char *);
static int		configure_filemap_index(char *);
static int		configure_http_media_type(char *);
static int		configure_http_hsts_enable(char *);
static int		configure_http_keepalive_time(char *);
static int		configure_http_request_ms(char *);
static int		configure_http_request_limit(char *);
static int		configure_http_body_disk_offload(char *);
static int		configure_http_body_disk_path(char *);
static int		configure_validator(char *);
static int		configure_params(char *);
static int		configure_validate(char *);
static int		configure_authentication(char *);
static int		configure_authentication_uri(char *);
static int		configure_authentication_type(char *);
static int		configure_authentication_value(char *);
static int		configure_authentication_validator(char *);
static int		configure_websocket_maxframe(char *);
static int		configure_websocket_timeout(char *);
#endif

#if defined(KORE_USE_PGSQL)
static int		configure_pgsql_conn_max(char *);
static int		configure_pgsql_queue_limit(char *);
#endif

#if defined(KORE_USE_TASKS)
static int		configure_task_threads(char *);
#endif

#if defined(KORE_USE_PYTHON)
static int		configure_python_path(char *);
static int		configure_python_import(char *);
#endif

static struct {
	const char		*name;
	int			(*configure)(char *);
} config_names[] = {
	{ "include",			configure_include },
	{ "bind",			configure_bind },
	{ "bind_unix",			configure_bind_unix },
	{ "load",			configure_load },
#if defined(KORE_USE_PYTHON)
	{ "python_path",		configure_python_path },
	{ "python_import",		configure_python_import },
#endif
	{ "root",			configure_root },
	{ "chroot",			configure_root },
	{ "domain",			configure_domain },
	{ "runas",			configure_runas },
	{ "workers",			configure_workers },
	{ "worker_max_connections",	configure_max_connections },
	{ "worker_rlimit_nofiles",	configure_rlimit_nofiles },
	{ "worker_accept_threshold",	configure_accept_threshold },
	{ "worker_set_affinity",	configure_set_affinity },
	{ "pidfile",			configure_pidfile },
	{ "socket_backlog",		configure_socket_backlog },
#if defined(KORE_USE_PLATFORM_PLEDGE)
	{ "pledge",			configure_add_pledge },
#endif
#if !defined(KORE_NO_TLS)
	{ "tls_version",		configure_tls_version },
	{ "tls_cipher",			configure_tls_cipher },
	{ "tls_dhparam",		configure_tls_dhparam },
	{ "rand_file",			configure_rand_file },
	{ "keymgr_runas",		configure_keymgr_runas },
	{ "keymgr_root",		configure_keymgr_root },
	{ "certfile",			configure_certfile },
	{ "certkey",			configure_certkey },
	{ "client_verify",		configure_client_verify },
	{ "client_verify_depth",	configure_client_verify_depth },
#endif
#if !defined(KORE_NO_HTTP)
	{ "filemap",			configure_filemap },
	{ "filemap_ext",		configure_filemap_ext },
	{ "filemap_index",		configure_filemap_index },
	{ "static",			configure_static_handler },
	{ "dynamic",			configure_dynamic_handler },
	{ "accesslog",			configure_accesslog },
	{ "restrict",			configure_restrict },
	{ "http_media_type",		configure_http_media_type },
	{ "http_header_max",		configure_http_header_max },
	{ "http_body_max",		configure_http_body_max },
	{ "http_hsts_enable",		configure_http_hsts_enable },
	{ "http_keepalive_time",	configure_http_keepalive_time },
	{ "http_request_ms",		configure_http_request_ms },
	{ "http_request_limit",		configure_http_request_limit },
	{ "http_body_disk_offload",	configure_http_body_disk_offload },
	{ "http_body_disk_path",	configure_http_body_disk_path },
	{ "validator",			configure_validator },
	{ "params",			configure_params },
	{ "validate",			configure_validate },
	{ "authentication",		configure_authentication },
	{ "authentication_uri",		configure_authentication_uri },
	{ "authentication_type",	configure_authentication_type },
	{ "authentication_value",	configure_authentication_value },
	{ "authentication_validator",	configure_authentication_validator },
	{ "websocket_maxframe",		configure_websocket_maxframe },
	{ "websocket_timeout",		configure_websocket_timeout },
#endif
#if defined(KORE_USE_PGSQL)
	{ "pgsql_conn_max",		configure_pgsql_conn_max },
	{ "pgsql_queue_limit",		configure_pgsql_queue_limit },
#endif
#if defined(KORE_USE_TASKS)
	{ "task_threads",		configure_task_threads },
#endif
	{ NULL,				NULL },
};

#if !defined(KORE_SINGLE_BINARY)
char					*config_file = NULL;
#endif

#if !defined(KORE_NO_HTTP)
static u_int8_t				current_method = 0;
static int				current_flags = 0;
static struct kore_auth			*current_auth = NULL;
static struct kore_module_handle	*current_handler = NULL;
#endif

extern const char			*__progname;
static struct kore_domain		*current_domain = NULL;

void
kore_parse_config(void)
{
	FILE		*fp;
	char		path[PATH_MAX];

#if !defined(KORE_SINGLE_BINARY)
	if ((fp = fopen(config_file, "r")) == NULL)
		fatal("configuration given cannot be opened: %s", config_file);
#else
	fp = config_file_write();
#endif

	kore_parse_config_file(fp);
	(void)fclose(fp);

	if (!kore_module_loaded())
		fatal("no application module was loaded");

	if (kore_root_path == NULL) {
		if (getcwd(path, sizeof(path)) == NULL)
			fatal("getcwd: %s", errno_s);
		kore_root_path = kore_strdup(path);

		if (!kore_quiet) {
			kore_log(LOG_NOTICE, "privsep: no root path set, "
			    "using working directory");
		}
	}

	if (getuid() != 0 && skip_chroot == 0) {
		fatal("cannot chroot, use -n to skip it");
	}

	if (skip_runas != 1 && kore_runas_user == NULL) {
		fatal("missing runas user, use -r to skip it");
	}

	if (getuid() != 0 && skip_runas == 0) {
		fatal("cannot drop privileges, use -r to skip it");
	}

	if (skip_runas) {
		if (!kore_quiet)
			kore_log(LOG_WARNING, "privsep: will not change user");
	} else {
#if !defined(KORE_NO_TLS)
		if (keymgr_runas_user == NULL) {
			if (!kore_quiet) {
				kore_log(LOG_NOTICE, "privsep: no keymgr_runas "
				    "set, using 'runas` user");
			}
			keymgr_runas_user = kore_strdup(kore_runas_user);
		}
#endif
	}

#if !defined(KORE_NO_TLS)
	if (keymgr_root_path == NULL) {
		if (!kore_quiet) {
			kore_log(LOG_NOTICE, "privsep: no keymgr_root set, "
			    "using 'root` directory");
		}
		keymgr_root_path = kore_strdup(kore_root_path);
	}
#endif

	if (skip_chroot && !kore_quiet)
		kore_log(LOG_WARNING, "privsep: will not chroot");
}

void
kore_parse_config_file(FILE *fp)
{
	int		i, lineno;
	char		buf[BUFSIZ], *p, *t;

	lineno = 1;
	while ((p = kore_read_line(fp, buf, sizeof(buf))) != NULL) {
		if (strlen(p) == 0) {
			lineno++;
			continue;
		}

#if !defined(KORE_NO_HTTP)
		if (!strcmp(p, "}") && current_handler != NULL) {
			lineno++;
			current_flags = 0;
			current_method = 0;
			current_handler = NULL;
			continue;
		}

		if (!strcmp(p, "}") && current_auth != NULL) {
			if (current_auth->validator == NULL) {
				fatal("no authentication validator for %s",
				    current_auth->name);
			}

			lineno++;
			current_auth = NULL;
			continue;
		}
#endif

		if (!strcmp(p, "}") && current_domain != NULL)
			current_domain = NULL;

		if (!strcmp(p, "}")) {
			lineno++;
			continue;
		}

		if ((t = strchr(p, ' ')) == NULL) {
			printf("ignoring \"%s\" on line %d\n", p, lineno++);
			continue;
		}

		*(t)++ = '\0';

		p = kore_text_trim(p, strlen(p));
		t = kore_text_trim(t, strlen(t));

		if (strlen(p) == 0 || strlen(t) == 0) {
			printf("ignoring \"%s\" on line %d\n", p, lineno++);
			continue;
		}

		for (i = 0; config_names[i].name != NULL; i++) {
			if (!strcmp(config_names[i].name, p)) {
				if (config_names[i].configure(t))
					break;
				fatal("configuration error on line %d", lineno);
				/* NOTREACHED */
			}
		}

		if (config_names[i].name == NULL)
			printf("ignoring \"%s\" on line %d\n", p, lineno);
		lineno++;
	}
}

static int
configure_include(char *path)
{
	FILE		*fp;

	if ((fp = fopen(path, "r")) == NULL)
		fatal("failed to open include '%s'", path);

	kore_parse_config_file(fp);
	(void)fclose(fp);

	return (KORE_RESULT_OK);
}

static int
configure_bind(char *options)
{
	char		*argv[4];

	kore_split_string(options, " ", argv, 4);
	if (argv[0] == NULL || argv[1] == NULL)
		return (KORE_RESULT_ERROR);

	return (kore_server_bind(argv[0], argv[1], argv[2]));
}

static int
configure_bind_unix(char *options)
{
	char		*argv[3];

	kore_split_string(options, " ", argv, 3);
	if (argv[0] == NULL)
		return (KORE_RESULT_ERROR);

	return (kore_server_bind_unix(argv[0], argv[1]));
}

static int
configure_load(char *options)
{
	char		*argv[3];

	kore_split_string(options, " ", argv, 3);
	if (argv[0] == NULL)
		return (KORE_RESULT_ERROR);

	kore_module_load(argv[0], argv[1], KORE_MODULE_NATIVE);
	return (KORE_RESULT_OK);
}

#if defined(KORE_SINGLE_BINARY)
static FILE *
config_file_write(void)
{
	FILE		*fp;
	ssize_t		ret;
	int		fd, len;
	char		fpath[MAXPATHLEN];

	len = snprintf(fpath, sizeof(fpath), "/tmp/%s.XXXXXX", __progname);
	if (len == -1 || (size_t)len >= sizeof(fpath))
		fatal("failed to create temporary path");

	if ((fd = mkstemp(fpath)) == -1)
		fatal("mkstemp(%s): %s", fpath, errno_s);

	(void)unlink(fpath);

	for (;;) {
		ret = write(fd, asset_builtin_kore_conf,
		    asset_len_builtin_kore_conf);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			fatal("failed to write temporary config: %s", errno_s);
		}

		if ((size_t)ret != asset_len_builtin_kore_conf)
			fatal("failed to write temporary config");
		break;
	}

	if ((fp = fdopen(fd, "w+")) == NULL)
		fatal("fdopen(): %s", errno_s);

	rewind(fp);

	return (fp);
}
#endif

#if !defined(KORE_NO_TLS)
static int
configure_tls_version(char *version)
{
	if (!strcmp(version, "1.3")) {
		tls_version = KORE_TLS_VERSION_1_3;
	} else if (!strcmp(version, "1.2")) {
		tls_version = KORE_TLS_VERSION_1_2;
	} else if (!strcmp(version, "both")) {
		tls_version = KORE_TLS_VERSION_BOTH;
	} else {
		printf("unknown value for tls_version: %s\n", version);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_tls_cipher(char *cipherlist)
{
	if (strcmp(kore_tls_cipher_list, KORE_DEFAULT_CIPHER_LIST)) {
		printf("tls_cipher specified twice\n");
		return (KORE_RESULT_ERROR);
	}

	kore_tls_cipher_list = kore_strdup(cipherlist);
	return (KORE_RESULT_OK);
}

static int
configure_tls_dhparam(char *path)
{
	BIO		*bio;

	if (tls_dhparam != NULL) {
		printf("tls_dhparam specified twice\n");
		return (KORE_RESULT_ERROR);
	}

	if ((bio = BIO_new_file(path, "r")) == NULL) {
		printf("%s did not exist\n", path);
		return (KORE_RESULT_ERROR);
	}

	tls_dhparam = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
	BIO_free(bio);

	if (tls_dhparam == NULL) {
		printf("PEM_read_bio_DHparams(): %s\n", ssl_errno_s);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_client_verify_depth(char *value)
{
	int	err, depth;

	if (current_domain == NULL) {
		printf("client_verify_depth not specified in domain context\n");
		return (KORE_RESULT_ERROR);
	}

	depth = kore_strtonum(value, 10, 0, INT_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad client_verify_depth value: %s\n", value);
		return (KORE_RESULT_ERROR);
	}

	current_domain->x509_verify_depth = depth;

	return (KORE_RESULT_OK);
}

static int
configure_client_verify(char *options)
{
	char		*argv[3];

	if (current_domain == NULL) {
		printf("client_verify not specified in domain context\n");
		return (KORE_RESULT_ERROR);
	}

	kore_split_string(options, " ", argv, 3);
	if (argv[0] == NULL) {
		printf("client_verify is missing a parameter\n");
		return (KORE_RESULT_ERROR);
	}

	if (current_domain->cafile != NULL) {
		printf("client_verify already set for %s\n",
		    current_domain->domain);
		return (KORE_RESULT_ERROR);
	}

	current_domain->cafile = kore_strdup(argv[0]);
	if (argv[1] != NULL)
		current_domain->crlfile = kore_strdup(argv[1]);

	return (KORE_RESULT_OK);
}

static int
configure_rand_file(char *path)
{
	if (rand_file != NULL)
		kore_free(rand_file);

	rand_file = kore_strdup(path);

	return (KORE_RESULT_OK);
}

static int
configure_certfile(char *path)
{
	if (current_domain == NULL) {
		printf("certfile not specified in domain context\n");
		return (KORE_RESULT_ERROR);
	}

	if (current_domain->certfile != NULL) {
		printf("certfile specified twice for %s\n",
		    current_domain->domain);
		return (KORE_RESULT_ERROR);
	}

	current_domain->certfile = kore_strdup(path);
	return (KORE_RESULT_OK);
}

static int
configure_certkey(char *path)
{
	if (current_domain == NULL) {
		printf("certkey not specified in domain text\n");
		return (KORE_RESULT_ERROR);
	}

	if (current_domain->certkey != NULL) {
		printf("certkey specified twice for %s\n",
		    current_domain->domain);
		return (KORE_RESULT_ERROR);
	}

	current_domain->certkey = kore_strdup(path);
	return (KORE_RESULT_OK);
}

static int
configure_keymgr_runas(char *user)
{
	if (keymgr_runas_user != NULL)
		kore_free(keymgr_runas_user);
	keymgr_runas_user = kore_strdup(user);

	return (KORE_RESULT_OK);
}

static int
configure_keymgr_root(char *root)
{
	if (keymgr_root_path != NULL)
		kore_free(keymgr_root_path);
	keymgr_root_path = kore_strdup(root);

	return (KORE_RESULT_OK);
}

#endif /* !KORE_NO_TLS */

static int
configure_domain(char *options)
{
	char		*argv[3];

	if (current_domain != NULL) {
		printf("nested domain contexts are not allowed\n");
		return (KORE_RESULT_ERROR);
	}

	kore_split_string(options, " ", argv, 3);

	if (strcmp(argv[1], "{")) {
		printf("domain context not opened correctly\n");
		return (KORE_RESULT_ERROR);
	}

	if (strlen(argv[0]) >= KORE_DOMAINNAME_LEN - 1) {
		printf("domain name '%s' too long\n", argv[0]);
		return (KORE_RESULT_ERROR);
	}

	if (!kore_domain_new(argv[0])) {
		printf("could not create new domain %s\n", argv[0]);
		return (KORE_RESULT_ERROR);
	}

	current_domain = kore_domain_lookup(argv[0]);
	return (KORE_RESULT_OK);
}

#if !defined(KORE_NO_HTTP)
static int
configure_static_handler(char *options)
{
	return (configure_handler(HANDLER_TYPE_STATIC, options));
}

static int
configure_dynamic_handler(char *options)
{
	return (configure_handler(HANDLER_TYPE_DYNAMIC, options));
}

static int
configure_handler(int type, char *options)
{
	char		*argv[4];

	if (current_domain == NULL) {
		printf("page handler not specified in domain context\n");
		return (KORE_RESULT_ERROR);
	}

	kore_split_string(options, " ", argv, 4);

	if (argv[0] == NULL || argv[1] == NULL) {
		printf("missing parameters for page handler\n");
		return (KORE_RESULT_ERROR);
	}

	if (!kore_module_handler_new(argv[0],
	    current_domain->domain, argv[1], argv[2], type)) {
		printf("cannot create handler for %s\n", argv[0]);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_filemap(char *options)
{
	char		*argv[3];

	if (current_domain == NULL) {
		printf("filemap outside of domain context\n");
		return (KORE_RESULT_ERROR);
	}

	kore_split_string(options, " ", argv, 3);

	if (argv[0] == NULL || argv[1] == NULL) {
		printf("missing parameters for filemap\n");
		return (KORE_RESULT_ERROR);
	}

	if (!kore_filemap_create(current_domain, argv[1], argv[0])) {
		printf("cannot create filemap for %s\n", argv[1]);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_accesslog(char *path)
{
	if (current_domain == NULL) {
		kore_debug("accesslog not specified in domain context\n");
		return (KORE_RESULT_ERROR);
	}

	if (current_domain->accesslog != -1) {
		printf("domain %s already has an open accesslog\n",
		    current_domain->domain);
		return (KORE_RESULT_ERROR);
	}

	current_domain->accesslog = open(path,
	    O_CREAT | O_APPEND | O_WRONLY,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (current_domain->accesslog == -1) {
		printf("accesslog open(%s): %s\n", path, errno_s);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_restrict(char *options)
{
	struct kore_module_handle	*hdlr;
	int				i, cnt;
	char				*argv[10];

	if (current_domain == NULL) {
		printf("restrict not used in domain context\n");
		return (KORE_RESULT_ERROR);
	}

	cnt = kore_split_string(options, " ", argv, 10);
	if (cnt < 2) {
		printf("bad restrict option '%s', missing methods\n", options);
		return (KORE_RESULT_ERROR);
	}

	hdlr = NULL;
	TAILQ_FOREACH(hdlr, &(current_domain->handlers), list) {
		if (!strcmp(hdlr->path, argv[0]))
			break;
	}

	if (hdlr == NULL) {
		printf("bad restrict option handler '%s' not found\n", argv[0]);
		return (KORE_RESULT_ERROR);
	}

	hdlr->methods = 0;

	for (i = 1; i < cnt; i++) {
		if (!strcasecmp(argv[i], "post")) {
			hdlr->methods |= HTTP_METHOD_POST;
		} else if (!strcasecmp(argv[i], "get")) {
			hdlr->methods |= HTTP_METHOD_GET;
		} else if (!strcasecmp(argv[i], "put")) {
			hdlr->methods |= HTTP_METHOD_PUT;
		} else if (!strcasecmp(argv[i], "delete")) {
			hdlr->methods |= HTTP_METHOD_DELETE;
		} else if (!strcasecmp(argv[i], "head")) {
			hdlr->methods |= HTTP_METHOD_HEAD;
		} else if (!strcasecmp(argv[i], "patch")) {
			hdlr->methods |= HTTP_METHOD_PATCH;
		} else {
			printf("unknown method: %s in restrict for %s\n",
			    argv[i], argv[0]);
			return (KORE_RESULT_ERROR);
		}
	}

	return (KORE_RESULT_OK);
}

static int
configure_filemap_ext(char *ext)
{
	kore_free(kore_filemap_ext);
	kore_filemap_ext = kore_strdup(ext);

	return (KORE_RESULT_OK);
}

static int
configure_filemap_index(char *index)
{
	kore_free(kore_filemap_index);
	kore_filemap_index = kore_strdup(index);

	return (KORE_RESULT_OK);
}

static int
configure_http_media_type(char *type)
{
	int		i;
	char		*extensions, *ext[10];

	extensions = strchr(type, ' ');
	if (extensions == NULL) {
		printf("bad http_media_type value: %s\n", type);
		return (KORE_RESULT_ERROR);
	}

	*(extensions)++ = '\0';

	kore_split_string(extensions, " \t", ext, 10);
	for (i = 0; ext[i] != NULL; i++) {
		if (!http_media_register(ext[i], type)) {
			printf("duplicate extension found: %s\n", ext[i]);
			return (KORE_RESULT_ERROR);
		}
	}

	if (i == 0) {
		printf("missing extensions in: %s\n", type);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_http_header_max(char *option)
{
	int		err;

	http_header_max = kore_strtonum(option, 10, 1, 65535, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad http_header_max value: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_http_body_max(char *option)
{
	int		err;

	http_body_max = kore_strtonum(option, 10, 0, LONG_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad http_body_max value: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_http_body_disk_offload(char *option)
{
	int		err;

	http_body_disk_offload = kore_strtonum(option, 10, 0, LONG_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad http_body_disk_offload value: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_http_body_disk_path(char *path)
{
	if (strcmp(http_body_disk_path, HTTP_BODY_DISK_PATH))
		kore_free(http_body_disk_path);

	http_body_disk_path = kore_strdup(path);
	return (KORE_RESULT_OK);
}

static int
configure_http_hsts_enable(char *option)
{
	int		err;

	http_hsts_enable = kore_strtonum(option, 10, 0, LONG_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad http_hsts_enable value: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_http_keepalive_time(char *option)
{
	int		err;

	http_keepalive_time = kore_strtonum(option, 10, 0, USHRT_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad http_keepalive_time value: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_http_request_ms(char *option)
{
	int		err;

	http_request_ms = kore_strtonum(option, 10, 0, UINT_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad http_request_ms value: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_http_request_limit(char *option)
{
	int		err;

	http_request_limit = kore_strtonum(option, 10, 0, UINT_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad http_request_limit value: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_validator(char *name)
{
	u_int8_t	type;
	char		*tname, *value;

	if ((tname = strchr(name, ' ')) == NULL) {
		printf("missing validator name\n");
		return (KORE_RESULT_ERROR);
	}

	*(tname)++ = '\0';
	tname = kore_text_trim(tname, strlen(tname));
	if ((value = strchr(tname, ' ')) == NULL) {
		printf("missing validator value\n");
		return (KORE_RESULT_ERROR);
	}

	*(value)++ = '\0';
	value = kore_text_trim(value, strlen(value));

	if (!strcmp(tname, "regex")) {
		type = KORE_VALIDATOR_TYPE_REGEX;
	} else if (!strcmp(tname, "function")) {
		type = KORE_VALIDATOR_TYPE_FUNCTION;
	} else {
		printf("bad type for validator %s\n", tname);
		return (KORE_RESULT_ERROR);
	}

	if (!kore_validator_add(name, type, value)) {
		printf("bad validator specified: %s\n", tname);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_params(char *options)
{
	struct kore_module_handle	*hdlr;
	char				*method, *argv[3];

	if (current_domain == NULL) {
		printf("params not used in domain context\n");
		return (KORE_RESULT_ERROR);
	}

	if (current_handler != NULL) {
		printf("previous params block not closed\n");
		return (KORE_RESULT_ERROR);
	}

	kore_split_string(options, " ", argv, 3);
	if (argv[1] == NULL)
		return (KORE_RESULT_ERROR);

	if ((method = strchr(argv[0], ':')) != NULL) {
		*(method)++ = '\0';
		if (!strcasecmp(argv[0], "qs")) {
			current_flags = KORE_PARAMS_QUERY_STRING;
		} else {
			printf("unknown prefix '%s' for '%s'\n",
			    argv[0], argv[1]);
			return (KORE_RESULT_ERROR);
		}
	} else {
		method = argv[0];
	}

	if (!strcasecmp(method, "post")) {
		current_method = HTTP_METHOD_POST;
	} else if (!strcasecmp(method, "get")) {
		current_method = HTTP_METHOD_GET;
		/* Let params get /foo {} imply qs:get automatically. */
		current_flags |= KORE_PARAMS_QUERY_STRING;
	} else if (!strcasecmp(method, "put")) {
		current_method = HTTP_METHOD_PUT;
	} else if (!strcasecmp(method, "delete")) {
		current_method = HTTP_METHOD_DELETE;
	} else if (!strcasecmp(method, "head")) {
		current_method = HTTP_METHOD_HEAD;
	} else if (!strcasecmp(method, "patch")) {
		current_method = HTTP_METHOD_PATCH;
	} else {
		printf("unknown method: %s in params block for %s\n",
		    method, argv[1]);
		return (KORE_RESULT_ERROR);
	}

	/*
	 * Find the handler ourselves, otherwise the regex is applied
	 * in case of a dynamic page.
	 */
	TAILQ_FOREACH(hdlr, &(current_domain->handlers), list) {
		if (!strcmp(hdlr->path, argv[1])) {
			current_handler = hdlr;
			return (KORE_RESULT_OK);
		}
	}

	printf("params for unknown page handler: %s\n", argv[1]);
	return (KORE_RESULT_ERROR);
}

static int
configure_validate(char *options)
{
	struct kore_handler_params	*p;
	struct kore_validator		*val;
	char				*argv[3];

	if (current_handler == NULL) {
		printf("validate not used in domain context\n");
		return (KORE_RESULT_ERROR);
	}

	kore_split_string(options, " ", argv, 3);
	if (argv[1] == NULL)
		return (KORE_RESULT_ERROR);

	if ((val = kore_validator_lookup(argv[1])) == NULL) {
		printf("unknown validator %s for %s\n", argv[1], argv[0]);
		return (KORE_RESULT_ERROR);
	}

	p = kore_malloc(sizeof(*p));
	p->validator = val;
	p->flags = current_flags;
	p->method = current_method;
	p->name = kore_strdup(argv[0]);

	TAILQ_INSERT_TAIL(&(current_handler->params), p, list);
	return (KORE_RESULT_OK);
}

static int
configure_authentication(char *options)
{
	char		*argv[3];

	if (current_auth != NULL) {
		printf("previous authentication block not closed\n");
		return (KORE_RESULT_ERROR);
	}

	kore_split_string(options, " ", argv, 3);
	if (argv[1] == NULL) {
		printf("missing name for authentication block\n");
		return (KORE_RESULT_ERROR);
	}

	if (strcmp(argv[1], "{")) {
		printf("missing { for authentication block\n");
		return (KORE_RESULT_ERROR);
	}

	if (!kore_auth_new(argv[0]))
		return (KORE_RESULT_ERROR);

	current_auth = kore_auth_lookup(argv[0]);

	return (KORE_RESULT_OK);
}

static int
configure_authentication_type(char *option)
{
	if (current_auth == NULL) {
		printf("authentication_type outside authentication context\n");
		return (KORE_RESULT_ERROR);
	}

	if (!strcmp(option, "cookie")) {
		current_auth->type = KORE_AUTH_TYPE_COOKIE;
	} else if (!strcmp(option, "header")) {
		current_auth->type = KORE_AUTH_TYPE_HEADER;
	} else if (!strcmp(option, "request")) {
		current_auth->type = KORE_AUTH_TYPE_REQUEST;
	} else {
		printf("unknown authentication type '%s'\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_authentication_value(char *option)
{
	if (current_auth == NULL) {
		printf("authentication_value outside authentication context\n");
		return (KORE_RESULT_ERROR);
	}

	if (current_auth->value != NULL)
		kore_free(current_auth->value);
	current_auth->value = kore_strdup(option);

	return (KORE_RESULT_OK);
}

static int
configure_authentication_validator(char *validator)
{
	struct kore_validator		*val;

	if (current_auth == NULL) {
		printf("authentication_validator outside authentication\n");
		return (KORE_RESULT_ERROR);
	}

	if ((val = kore_validator_lookup(validator)) == NULL) {
		printf("authentication validator '%s' not found\n", validator);
		return (KORE_RESULT_ERROR);
	}

	current_auth->validator = val;

	return (KORE_RESULT_OK);
}

static int
configure_authentication_uri(char *uri)
{
	if (current_auth == NULL) {
		printf("authentication_uri outside authentication context\n");
		return (KORE_RESULT_ERROR);
	}

	if (current_auth->redirect != NULL)
		kore_free(current_auth->redirect);
	current_auth->redirect = kore_strdup(uri);

	return (KORE_RESULT_OK);
}

static int
configure_websocket_maxframe(char *option)
{
	int	err;

	kore_websocket_maxframe = kore_strtonum64(option, 1, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad kore_websocket_maxframe value: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_websocket_timeout(char *option)
{
	int	err;

	kore_websocket_timeout = kore_strtonum64(option, 1, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad kore_websocket_timeout value: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	kore_websocket_timeout = kore_websocket_timeout * 1000;

	return (KORE_RESULT_OK);
}

#endif /* !KORE_NO_HTTP */

static int
configure_root(char *path)
{
	if (kore_root_path != NULL)
		kore_free(kore_root_path);
	kore_root_path = kore_strdup(path);

	return (KORE_RESULT_OK);
}

static int
configure_runas(char *user)
{
	if (kore_runas_user != NULL)
		kore_free(kore_runas_user);
	kore_runas_user = kore_strdup(user);

	return (KORE_RESULT_OK);
}

static int
configure_workers(char *option)
{
	int		err;

	worker_count = kore_strtonum(option, 10, 1, 255, &err);
	if (err != KORE_RESULT_OK) {
		printf("%s is not a valid worker number\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_pidfile(char *path)
{
	if (strcmp(kore_pidfile, KORE_PIDFILE_DEFAULT))
		kore_free(kore_pidfile);
	kore_pidfile = kore_strdup(path);

	return (KORE_RESULT_OK);
}

static int
configure_max_connections(char *option)
{
	int		err;

	worker_max_connections = kore_strtonum(option, 10, 1, UINT_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad value for worker_max_connections: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_rlimit_nofiles(char *option)
{
	int		err;

	worker_rlimit_nofiles = kore_strtonum(option, 10, 1, UINT_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad value for worker_rlimit_nofiles: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_accept_threshold(char *option)
{
	int		err;

	worker_accept_threshold = kore_strtonum(option, 0, 1, UINT_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad value for worker_accept_threshold: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_set_affinity(char *option)
{
	int		err;

	worker_set_affinity = kore_strtonum(option, 10, 0, 1, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad value for worker_set_affinity: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_socket_backlog(char *option)
{
	int		err;

	kore_socket_backlog = kore_strtonum(option, 10, 0, UINT_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad socket_backlog value: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

#if defined(KORE_USE_PGSQL)
static int
configure_pgsql_conn_max(char *option)
{
	int		err;

	pgsql_conn_max = kore_strtonum(option, 10, 0, USHRT_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad value for pgsql_conn_max: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}

static int
configure_pgsql_queue_limit(char *option)
{
	int		err;

	pgsql_queue_limit = kore_strtonum(option, 10, 0, UINT_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad value for pgsql_queue_limit: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}
#endif

#if defined(KORE_USE_TASKS)
static int
configure_task_threads(char *option)
{
	int		err;

	kore_task_threads = kore_strtonum(option, 10, 0, UCHAR_MAX, &err);
	if (err != KORE_RESULT_OK) {
		printf("bad value for task_threads: %s\n", option);
		return (KORE_RESULT_ERROR);
	}

	return (KORE_RESULT_OK);
}
#endif

#if defined(KORE_USE_PYTHON)
static int
configure_python_path(char *path)
{
	kore_python_path(path);

	return (KORE_RESULT_OK);
}

static int
configure_python_import(char *module)
{
	char		*argv[3];

	kore_split_string(module, " ", argv, 3);
	if (argv[0] == NULL)
		return (KORE_RESULT_ERROR);

	kore_module_load(argv[0], argv[1], KORE_MODULE_PYTHON);
	return (KORE_RESULT_OK);
}
#endif

#if defined(KORE_USE_PLATFORM_PLEDGE)
static int
configure_add_pledge(char *pledge)
{
	kore_platform_add_pledge(pledge);

	return (KORE_RESULT_OK);
}
#endif
