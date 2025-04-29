/*
 * Copyright 2011-2018 Con Kolivas
 * Copyright 2011-2015 Andrew Smith
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <jansson.h>
#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#ifndef WIN32
#include <fcntl.h>
# ifdef __linux
#  include <sys/prctl.h>
# endif
# include <sys/socket.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <netdb.h>
#else
# include <winsock2.h>
# include <ws2tcpip.h>
# include <mmsystem.h>
#endif
#include <sched.h>

#include "miner.h"
#include "elist.h"
#include "compat.h"
#include "util.h"
#include "libssplus.h"

#ifdef USE_AVALON7
#include "driver-avalon7.h"
#endif

#define DEFAULT_SOCKWAIT 60
#ifndef STRATUM_USER_AGENT
#define STRATUM_USER_AGENT
#endif

bool successful_connect = false;

int no_yield(void)
{
	return 0;
}

int (*selective_yield)(void) = &no_yield;

static void keep_sockalive(SOCKETTYPE fd)
{
	const int tcp_one = 1;
#ifndef WIN32
	const int tcp_keepidle = 45;
	const int tcp_keepintvl = 30;
	int flags = fcntl(fd, F_GETFL, 0);

	fcntl(fd, F_SETFL, O_NONBLOCK | flags);
#else
	u_long flags = 1;

	ioctlsocket(fd, FIONBIO, &flags);
#endif

	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const void *)&tcp_one, sizeof(tcp_one));
	if (!opt_delaynet)
#ifndef __linux
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const void *)&tcp_one, sizeof(tcp_one));
#else /* __linux */
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	setsockopt(fd, SOL_TCP, TCP_NODELAY, (const void *)&tcp_one, sizeof(tcp_one));
	setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &tcp_one, sizeof(tcp_one));
	setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &tcp_keepidle, sizeof(tcp_keepidle));
	setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &tcp_keepintvl, sizeof(tcp_keepintvl));
#endif /* __linux */

#ifdef __APPLE_CC__
	setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &tcp_keepintvl, sizeof(tcp_keepintvl));
#endif /* __APPLE_CC__ */

}

#ifdef WIN32
/* Generic versions of inet_pton for windows, using different names in case
 * it is implemented in ming in the future. */
#define W32NS_INADDRSZ  4
#define W32NS_IN6ADDRSZ 16
#define W32NS_INT16SZ   2

static int Inet_Pton4(const char *src, char *dst)
{
	uint8_t tmp[W32NS_INADDRSZ], *tp;

	int saw_digit = 0;
	int octets = 0;
	*(tp = tmp) = 0;

	int ch;
	while ((ch = *src++) != '\0')
	{
		if (ch >= '0' && ch <= '9')
		{
			uint32_t n = *tp * 10 + (ch - '0');

			if (saw_digit && *tp == 0)
				return 0;

			if (n > 255)
				return 0;

			*tp = n;
			if (!saw_digit)
			{
				if (++octets > 4)
					return 0;
				saw_digit = 1;
			}
		}
		else if (ch == '.' && saw_digit)
		{
			if (octets == 4)
				return 0;
			*++tp = 0;
			saw_digit = 0;
		}
		else
			return 0;
	}
	if (octets < 4)
		return 0;

	cg_memcpy(dst, tmp, W32NS_INADDRSZ);

	return 1;
}

static int Inet_Pton6(const char *src, char *dst)
{
	static const char xdigits[] = "0123456789abcdef";
	uint8_t tmp[W32NS_IN6ADDRSZ];

	uint8_t *tp = (uint8_t*) memset(tmp, '\0', W32NS_IN6ADDRSZ);
	uint8_t *endp = tp + W32NS_IN6ADDRSZ;
	uint8_t *colonp = NULL;

	/* Leading :: requires some special handling. */
	if (*src == ':')
	{
		if (*++src != ':')
			return 0;
	}

	const char *curtok = src;
	int saw_xdigit = 0;
	uint32_t val = 0;
	int ch;
	while ((ch = tolower(*src++)) != '\0')
	{
		const char *pch = strchr(xdigits, ch);
		if (pch != NULL)
		{
			val <<= 4;
			val |= (pch - xdigits);
			if (val > 0xffff)
				return 0;
			saw_xdigit = 1;
			continue;
		}
		if (ch == ':')
		{
			curtok = src;
			if (!saw_xdigit)
			{
				if (colonp)
					return 0;
				colonp = tp;
				continue;
			}
			else if (*src == '\0')
			{
				return 0;
			}
			if (tp + W32NS_INT16SZ > endp)
				return 0;
			*tp++ = (uint8_t) (val >> 8) & 0xff;
			*tp++ = (uint8_t) val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}
		if (ch == '.' && ((tp + W32NS_INADDRSZ) <= endp) &&
			Inet_Pton4(curtok, (char*) tp) > 0)
		{
			tp += W32NS_INADDRSZ;
			saw_xdigit = 0;
			break; /* '\0' was seen by inet_pton4(). */
		}
		return 0;
	}
	if (saw_xdigit)
	{
		if (tp + W32NS_INT16SZ > endp)
			return 0;
		*tp++ = (uint8_t) (val >> 8) & 0xff;
		*tp++ = (uint8_t) val & 0xff;
	}
	if (colonp != NULL)
	{
		int i;
		/*
			* Since some memmove()'s erroneously fail to handle
			* overlapping regions, we'll do the shift by hand.
			*/
		const int n = tp - colonp;

		if (tp == endp)
			return 0;

		for (i = 1; i <= n; i++)
		{
			endp[-i] = colonp[n - i];
			colonp[n - i] = 0;
		}
		tp = endp;
	}
	if (tp != endp)
		return 0;

	cg_memcpy(dst, tmp, W32NS_IN6ADDRSZ);

	return 1;
}

int Inet_Pton(int af, const char *src, void *dst)
{
	switch (af)
	{
		case AF_INET:
			return Inet_Pton4(src, dst);
		case AF_INET6:
			return Inet_Pton6(src, dst);
		default:
			return -1;
	}
}
#endif

/* Align a size_t to 4 byte boundaries for fussy arches */
static inline void align_len(size_t *len)
{
	if (*len % 4)
		*len += 4 - (*len % 4);
}

void *_cgmalloc(size_t size, const char *file, const char *func, const int line)
{
	void *ret;

	align_len(&size);
	ret = malloc(size);
	if (unlikely(!ret))
		quit(1, "Failed to malloc size %d from %s %s:%d", (int)size, file, func, line);
	return ret;
}

void *_cgcalloc(const size_t memb, size_t size, const char *file, const char *func, const int line)
{
	void *ret;

	align_len(&size);
	ret = calloc(memb, size);
	if (unlikely(!ret))
		quit(1, "Failed to calloc memb %d size %d from %s %s:%d", (int)memb, (int)size, file, func, line);
	return ret;
}

void *_cgrealloc(void *ptr, size_t size, const char *file, const char *func, const int line)
{
	void *ret;

	align_len(&size);
	ret = realloc(ptr, size);
	if (unlikely(!ret))
		quit(1, "Failed to realloc size %d from %s %s:%d", (int)size, file, func, line);
	return ret;
}

struct tq_ent {
	void			*data;
	struct list_head	q_node;
};

#ifdef HAVE_LIBCURL
struct timeval nettime;

struct data_buffer {
	void		*buf;
	size_t		len;
};

struct upload_buffer {
	const void	*buf;
	size_t		len;
};

struct header_info {
	char		*lp_path;
	int		rolltime;
	char		*reason;
	char		*stratum_url;
	bool		hadrolltime;
	bool		canroll;
	bool		hadexpire;
};

static void databuf_free(struct data_buffer *db)
{
	if (!db)
		return;

	free(db->buf);

	memset(db, 0, sizeof(*db));
}

static size_t all_data_cb(const void *ptr, size_t size, size_t nmemb,
			  void *user_data)
{
	struct data_buffer *db = user_data;
	size_t len = size * nmemb;
	size_t oldlen, newlen;
	void *newmem;
	static const unsigned char zero = 0;

	oldlen = db->len;
	newlen = oldlen + len;

	newmem = cgrealloc(db->buf, newlen + 1);
	db->buf = newmem;
	db->len = newlen;
	cg_memcpy(db->buf + oldlen, ptr, len);
	cg_memcpy(db->buf + newlen, &zero, 1);	/* null terminate */

	return len;
}

static size_t upload_data_cb(void *ptr, size_t size, size_t nmemb,
			     void *user_data)
{
	struct upload_buffer *ub = user_data;
	unsigned int len = size * nmemb;

	if (len > ub->len)
		len = ub->len;

	if (len) {
		cg_memcpy(ptr, ub->buf, len);
		ub->buf += len;
		ub->len -= len;
	}

	return len;
}

static size_t resp_hdr_cb(void *ptr, size_t size, size_t nmemb, void *user_data)
{
	struct header_info *hi = user_data;
	size_t remlen, slen, ptrlen = size * nmemb;
	char *rem, *val = NULL, *key = NULL;
	void *tmp;

	val = cgcalloc(1, ptrlen);
	key = cgcalloc(1, ptrlen);

	tmp = memchr(ptr, ':', ptrlen);
	if (!tmp || (tmp == ptr))	/* skip empty keys / blanks */
		goto out;
	slen = tmp - ptr;
	if ((slen + 1) == ptrlen)	/* skip key w/ no value */
		goto out;
	cg_memcpy(key, ptr, slen);		/* store & nul term key */
	key[slen] = 0;

	rem = ptr + slen + 1;		/* trim value's leading whitespace */
	remlen = ptrlen - slen - 1;
	while ((remlen > 0) && (isspace(*rem))) {
		remlen--;
		rem++;
	}

	cg_memcpy(val, rem, remlen);	/* store value, trim trailing ws */
	val[remlen] = 0;
	while ((*val) && (isspace(val[strlen(val) - 1])))
		val[strlen(val) - 1] = 0;

	if (!*val)			/* skip blank value */
		goto out;

	if (opt_protocol)
		applog(LOG_DEBUG, "HTTP hdr(%s): %s", key, val);

	if (!strcasecmp("X-Roll-Ntime", key)) {
		hi->hadrolltime = true;
		if (!strncasecmp("N", val, 1))
			applog(LOG_DEBUG, "X-Roll-Ntime: N found");
		else {
			hi->canroll = true;

			/* Check to see if expire= is supported and if not, set
			 * the rolltime to the default scantime */
			if (strlen(val) > 7 && !strncasecmp("expire=", val, 7)) {
				sscanf(val + 7, "%d", &hi->rolltime);
				hi->hadexpire = true;
			} else
				hi->rolltime = max_scantime;
			applog(LOG_DEBUG, "X-Roll-Ntime expiry set to %d", hi->rolltime);
		}
	}

	if (!strcasecmp("X-Long-Polling", key)) {
		hi->lp_path = val;	/* steal memory reference */
		val = NULL;
	}

	if (!strcasecmp("X-Reject-Reason", key)) {
		hi->reason = val;	/* steal memory reference */
		val = NULL;
	}

	if (!strcasecmp("X-Stratum", key)) {
		hi->stratum_url = val;
		val = NULL;
	}

out:
	free(key);
	free(val);
	return ptrlen;
}

static void last_nettime(struct timeval *last)
{
	rd_lock(&netacc_lock);
	last->tv_sec = nettime.tv_sec;
	last->tv_usec = nettime.tv_usec;
	rd_unlock(&netacc_lock);
}

static void set_nettime(void)
{
	wr_lock(&netacc_lock);
	cgtime(&nettime);
	wr_unlock(&netacc_lock);
}

#if CURL_HAS_KEEPALIVE
static void keep_curlalive(CURL *curl)
{
	const int tcp_keepidle = 45;
	const int tcp_keepintvl = 30;
	const long int keepalive = 1;

	curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, keepalive);
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, tcp_keepidle);
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, tcp_keepintvl);
}
#else
static void keep_curlalive(CURL *curl)
{
	SOCKETTYPE sock;

	curl_easy_getinfo(curl, CURLINFO_LASTSOCKET, (long *)&sock);
	keep_sockalive(sock);
}
#endif

static int curl_debug_cb(__maybe_unused CURL *handle, curl_infotype type,
			 __maybe_unused char *data, size_t size, void *userdata)
{
	struct pool *pool = (struct pool *)userdata;

	switch(type) {
		case CURLINFO_HEADER_IN:
		case CURLINFO_DATA_IN:
		case CURLINFO_SSL_DATA_IN:
			pool->cgminer_pool_stats.net_bytes_received += size;
			break;
		case CURLINFO_HEADER_OUT:
		case CURLINFO_DATA_OUT:
		case CURLINFO_SSL_DATA_OUT:
			pool->cgminer_pool_stats.net_bytes_sent += size;
			break;
		case CURLINFO_TEXT:
		default:
			break;
	}
	return 0;
}

json_t *json_web_config(const char *url)
{
	struct data_buffer all_data = {NULL, 0};
	char curl_err_str[CURL_ERROR_SIZE];
	long timeout = 60;
	json_error_t err;
	json_t *val;
	CURL *curl;
	int rc;

	memset(&err, 0, sizeof(err));

	curl = curl_easy_init();
	if (unlikely(!curl))
		quithere(1, "CURL initialisation failed");

	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, all_data_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &all_data);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_str);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);

	val = NULL;
	rc = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (rc) {
		applog(LOG_ERR, "HTTP config request of '%s' failed: %s", url, curl_err_str);
		goto c_out;
	}

	if (!all_data.buf) {
		applog(LOG_ERR, "Empty config data received from '%s'", url);
		goto c_out;
	}

	val = JSON_LOADS(all_data.buf, &err);
	if (!val) {
		applog(LOG_ERR, "JSON config decode of '%s' failed(%d): %s", url,
		       err.line, err.text);
	}
	databuf_free(&all_data);

c_out:
	return val;
}

json_t *json_rpc_call(CURL *curl, const char *url,
		      const char *userpass, const char *rpc_req,
		      bool probe, bool longpoll, int *rolltime,
		      struct pool *pool, bool share)
{
	long timeout = longpoll ? (60 * 60) : 60;
	struct data_buffer all_data = {NULL, 0};
	struct header_info hi = {NULL, 0, NULL, NULL, false, false, false};
	char len_hdr[64], user_agent_hdr[128];
	char curl_err_str[CURL_ERROR_SIZE];
	struct curl_slist *headers = NULL;
	struct upload_buffer upload_data;
	json_t *val, *err_val, *res_val;
	bool probing = false;
	double byte_count;
	json_error_t err;
	int rc;

	memset(&err, 0, sizeof(err));

	/* it is assumed that 'curl' is freshly [re]initialized at this pt */

	if (probe)
		probing = !pool->probed;
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

	// CURLOPT_VERBOSE won't write to stderr if we use CURLOPT_DEBUGFUNCTION
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
	curl_easy_setopt(curl, CURLOPT_DEBUGDATA, (void *)pool);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

	/* Shares are staggered already and delays in submission can be costly
	 * so do not delay them */
	if (!opt_delaynet || share)
		curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, all_data_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &all_data);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, upload_data_cb);
	curl_easy_setopt(curl, CURLOPT_READDATA, &upload_data);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_str);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, resp_hdr_cb);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hi);
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
	if (pool->rpc_proxy) {
		curl_easy_setopt(curl, CURLOPT_PROXY, pool->rpc_proxy);
		curl_easy_setopt(curl, CURLOPT_PROXYTYPE, pool->rpc_proxytype);
	} else if (opt_socks_proxy) {
		curl_easy_setopt(curl, CURLOPT_PROXY, opt_socks_proxy);
		curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4);
	}
	if (userpass) {
		curl_easy_setopt(curl, CURLOPT_USERPWD, userpass);
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
	}
	if (longpoll)
		keep_curlalive(curl);
	curl_easy_setopt(curl, CURLOPT_POST, 1);

	if (opt_protocol)
		applog(LOG_DEBUG, "JSON protocol request:\n%s", rpc_req);

	upload_data.buf = rpc_req;
	upload_data.len = strlen(rpc_req);
	sprintf(len_hdr, "Content-Length: %lu",
		(unsigned long) upload_data.len);
	sprintf(user_agent_hdr, "User-Agent: %s", PACKAGE_STRING);

	headers = curl_slist_append(headers,
		"Content-type: application/json");
	headers = curl_slist_append(headers,
		"X-Mining-Extensions: longpoll midstate rollntime submitold");

	if (likely(global_hashrate)) {
		char ghashrate[255];

		sprintf(ghashrate, "X-Mining-Hashrate: %"PRIu64, global_hashrate);
		headers = curl_slist_append(headers, ghashrate);
	}

	headers = curl_slist_append(headers, len_hdr);
	headers = curl_slist_append(headers, user_agent_hdr);
	headers = curl_slist_append(headers, "Expect:"); /* disable Expect hdr*/

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	if (opt_delaynet) {
		/* Don't delay share submission, but still track the nettime */
		if (!share) {
			long long now_msecs, last_msecs;
			struct timeval now, last;

			cgtime(&now);
			last_nettime(&last);
			now_msecs = (long long)now.tv_sec * 1000;
			now_msecs += now.tv_usec / 1000;
			last_msecs = (long long)last.tv_sec * 1000;
			last_msecs += last.tv_usec / 1000;
			if (now_msecs > last_msecs && now_msecs - last_msecs < 250) {
				struct timespec rgtp;

				rgtp.tv_sec = 0;
				rgtp.tv_nsec = (250 - (now_msecs - last_msecs)) * 1000000;
				nanosleep(&rgtp, NULL);
			}
		}
		set_nettime();
	}

	rc = curl_easy_perform(curl);
	if (rc) {
		applog(LOG_INFO, "HTTP request failed: %s", curl_err_str);
		goto err_out;
	}

	if (!all_data.buf) {
		applog(LOG_DEBUG, "Empty data received in json_rpc_call.");
		goto err_out;
	}

	pool->cgminer_pool_stats.times_sent++;
	if (curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &byte_count) == CURLE_OK)
		pool->cgminer_pool_stats.bytes_sent += byte_count;
	pool->cgminer_pool_stats.times_received++;
	if (curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &byte_count) == CURLE_OK)
		pool->cgminer_pool_stats.bytes_received += byte_count;

	if (probing) {
		pool->probed = true;
		/* If X-Long-Polling was found, activate long polling */
		if (hi.lp_path) {
			if (pool->hdr_path != NULL)
				free(pool->hdr_path);
			pool->hdr_path = hi.lp_path;
		} else
			pool->hdr_path = NULL;
		if (hi.stratum_url) {
			pool->stratum_url = hi.stratum_url;
			hi.stratum_url = NULL;
		}
	} else {
		if (hi.lp_path) {
			free(hi.lp_path);
			hi.lp_path = NULL;
		}
		if (hi.stratum_url) {
			free(hi.stratum_url);
			hi.stratum_url = NULL;
		}
	}

	*rolltime = hi.rolltime;
	pool->cgminer_pool_stats.rolltime = hi.rolltime;
	pool->cgminer_pool_stats.hadrolltime = hi.hadrolltime;
	pool->cgminer_pool_stats.canroll = hi.canroll;
	pool->cgminer_pool_stats.hadexpire = hi.hadexpire;

	val = JSON_LOADS(all_data.buf, &err);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);

		if (opt_protocol)
			applog(LOG_DEBUG, "JSON protocol response:\n%s", (char *)(all_data.buf));

		goto err_out;
	}

	if (opt_protocol) {
		char *s = json_dumps(val, JSON_INDENT(3));

		applog(LOG_DEBUG, "JSON protocol response:\n%s", s);
		free(s);
	}

	/* JSON-RPC valid response returns a non-null 'result',
	 * and a null 'error'.
	 */
	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");

	if (!res_val ||(err_val && !json_is_null(err_val))) {
		char *s;

		if (err_val)
			s = json_dumps(err_val, JSON_INDENT(3));
		else
			s = strdup("(unknown reason)");

		applog(LOG_INFO, "JSON-RPC call failed: %s", s);

		free(s);

		goto err_out;
	}

	if (hi.reason) {
		json_object_set_new(val, "reject-reason", json_string(hi.reason));
		free(hi.reason);
		hi.reason = NULL;
	}
	successful_connect = true;
	databuf_free(&all_data);
	curl_slist_free_all(headers);
	curl_easy_reset(curl);
	return val;

err_out:
	databuf_free(&all_data);
	curl_slist_free_all(headers);
	curl_easy_reset(curl);
	if (!successful_connect)
		applog(LOG_DEBUG, "Failed to connect in json_rpc_call");
	curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
	return NULL;
}
#define PROXY_HTTP	CURLPROXY_HTTP
#define PROXY_HTTP_1_0	CURLPROXY_HTTP_1_0
#define PROXY_SOCKS4	CURLPROXY_SOCKS4
#define PROXY_SOCKS5	CURLPROXY_SOCKS5
#define PROXY_SOCKS4A	CURLPROXY_SOCKS4A
#define PROXY_SOCKS5H	CURLPROXY_SOCKS5_HOSTNAME
#else /* HAVE_LIBCURL */
#define PROXY_HTTP	0
#define PROXY_HTTP_1_0	1
#define PROXY_SOCKS4	2
#define PROXY_SOCKS5	3
#define PROXY_SOCKS4A	4
#define PROXY_SOCKS5H	5
#endif /* HAVE_LIBCURL */

static struct {
	const char *name;
	proxytypes_t proxytype;
} proxynames[] = {
	{ "http:",	PROXY_HTTP },
	{ "http0:",	PROXY_HTTP_1_0 },
	{ "socks4:",	PROXY_SOCKS4 },
	{ "socks5:",	PROXY_SOCKS5 },
	{ "socks4a:",	PROXY_SOCKS4A },
	{ "socks5h:",	PROXY_SOCKS5H },
	{ NULL,	0 }
};

const char *proxytype(proxytypes_t proxytype)
{
	int i;

	for (i = 0; proxynames[i].name; i++)
		if (proxynames[i].proxytype == proxytype)
			return proxynames[i].name;

	return "invalid";
}

char *get_proxy(char *url, struct pool *pool)
{
	pool->rpc_proxy = NULL;

	char *split;
	int plen, len, i;

	for (i = 0; proxynames[i].name; i++) {
		plen = strlen(proxynames[i].name);
		if (strncmp(url, proxynames[i].name, plen) == 0) {
			if (!(split = strchr(url, '|')))
				return url;

			*split = '\0';
			len = split - url;
			pool->rpc_proxy = cgmalloc(1 + len - plen);
			strcpy(pool->rpc_proxy, url + plen);
			extract_sockaddr(pool->rpc_proxy, &pool->sockaddr_proxy_url, &pool->sockaddr_proxy_port);
			pool->rpc_proxytype = proxynames[i].proxytype;
			url = split + 1;
			break;
		}
	}
	return url;
}

/* Adequate size s==len*2 + 1 must be alloced to use this variant */
void __bin2hex(char *s, const unsigned char *p, size_t len)
{
	int i;
	static const char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

	for (i = 0; i < (int)len; i++) {
		*s++ = hex[p[i] >> 4];
		*s++ = hex[p[i] & 0xF];
	}
	*s++ = '\0';
}

/* Returns a malloced array string of a binary value of arbitrary length. The
 * array is rounded up to a 4 byte size to appease architectures that need
 * aligned array  sizes */
char *bin2hex(const unsigned char *p, size_t len)
{
	ssize_t slen;
	char *s;

	slen = len * 2 + 1;
	if (slen % 4)
		slen += 4 - (slen % 4);
	s = cgcalloc(slen, 1);
	__bin2hex(s, p, len);

	return s;
}

static const int hex2bin_tbl[256] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

/* Does the reverse of bin2hex but does not allocate any ram */
bool hex2bin(unsigned char *p, const char *hexstr, size_t len)
{
	int nibble1, nibble2;
	unsigned char idx;
	bool ret = false;

	while (*hexstr && len) {
		if (unlikely(!hexstr[1])) {
			applog(LOG_ERR, "hex2bin str truncated");
			return ret;
		}

		idx = *hexstr++;
		nibble1 = hex2bin_tbl[idx];
		idx = *hexstr++;
		nibble2 = hex2bin_tbl[idx];

		if (unlikely((nibble1 < 0) || (nibble2 < 0))) {
			applog(LOG_ERR, "hex2bin scan failed");
			return ret;
		}

		*p++ = (((unsigned char)nibble1) << 4) | ((unsigned char)nibble2);
		--len;
	}

	if (likely(len == 0 && *hexstr == 0))
		ret = true;
	return ret;
}

static bool _valid_hex(char *s, const char *file, const char *func, const int line)
{
	bool ret = false;
	int i, len;

	if (unlikely(!s)) {
		applog(LOG_ERR, "Null string passed to valid_hex from"IN_FMT_FFL, file, func, line);
		return ret;
	}
	len = strlen(s);
	for (i = 0; i < len; i++) {
		unsigned char idx = s[i];

		if (unlikely(hex2bin_tbl[idx] < 0)) {
			applog(LOG_ERR, "Invalid char 0x%x passed to valid_hex from"IN_FMT_FFL, idx, file, func, line);
			return ret;
		}
	}
	ret = true;
	return ret;
}

#define valid_hex(s) _valid_hex(s, __FILE__, __func__, __LINE__)

static bool _valid_ascii(char *s, const char *file, const char *func, const int line)
{
	bool ret = false;
	int i, len;

	if (unlikely(!s)) {
		applog(LOG_ERR, "Null string passed to valid_ascii from"IN_FMT_FFL, file, func, line);
		return ret;
	}
	len = strlen(s);
	if (unlikely(!len)) {
		applog(LOG_ERR, "Zero length string passed to valid_ascii from"IN_FMT_FFL, file, func, line);
		return ret;
	}
	for (i = 0; i < len; i++) {
		unsigned char idx = s[i];

		if (unlikely(idx < 32 || idx > 126)) {
			applog(LOG_ERR, "Invalid char 0x%x passed to valid_ascii from"IN_FMT_FFL, idx, file, func, line);
			return ret;
		}
	}
	ret = true;
	return ret;
}

#define valid_ascii(s) _valid_ascii(s, __FILE__, __func__, __LINE__)

static const int b58tobin_tbl[] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1,  0,  1,  2,  3,  4,  5,  6,  7,  8, -1, -1, -1, -1, -1, -1,
	-1,  9, 10, 11, 12, 13, 14, 15, 16, -1, 17, 18, 19, 20, 21, -1,
	22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1, -1,
	-1, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, -1, 44, 45, 46,
	47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57
};

/* b58bin should always be at least 25 bytes long and already checked to be
 * valid. */
void b58tobin(unsigned char *b58bin, const char *b58)
{
	uint32_t c, bin32[7];
	int len, i, j;
	uint64_t t;

	memset(bin32, 0, 7 * sizeof(uint32_t));
	len = strlen(b58);
	for (i = 0; i < len; i++) {
		c = b58[i];
		c = b58tobin_tbl[c];
		for (j = 6; j >= 0; j--) {
			t = ((uint64_t)bin32[j]) * 58 + c;
			c = (t & 0x3f00000000ull) >> 32;
			bin32[j] = t & 0xffffffffull;
		}
	}
	*(b58bin++) = bin32[0] & 0xff;
	for (i = 1; i < 7; i++) {
		*((uint32_t *)b58bin) = htobe32(bin32[i]);
		b58bin += sizeof(uint32_t);
	}
}

void address_to_pubkeyhash(unsigned char *pkh, const char *addr)
{
	unsigned char b58bin[25];

	memset(b58bin, 0, 25);
	b58tobin(b58bin, addr);
	pkh[0] = 0x76;
	pkh[1] = 0xa9;
	pkh[2] = 0x14;
	cg_memcpy(&pkh[3], &b58bin[1], 20);
	pkh[23] = 0x88;
	pkh[24] = 0xac;
}

/*  For encoding nHeight into coinbase, return how many bytes were used */
int ser_number(unsigned char *s, int32_t val)
{
	int32_t *i32 = (int32_t *)&s[1];
	int len;

	if (val < 17) {
		s[0] = 0x50 + val;
		return 1;
	}
	if (val < 128)
		len = 1;
	else if (val < 32768)
		len = 2;
	else if (val < 8388608)
		len = 3;
	else
		len = 4;
	*i32 = htole32(val);
	s[0] = len++;
	return len;
}

/* For encoding variable length strings */
unsigned char *ser_string(char *s, int *slen)
{
	size_t len = strlen(s);
	unsigned char *ret;

	ret = cgmalloc(1 + len + 8); // Leave room for largest size
	if (len < 253) {
		ret[0] = len;
		cg_memcpy(ret + 1, s, len);
		*slen = len + 1;
	} else if (len < 0x10000) {
		uint16_t *u16 = (uint16_t *)&ret[1];

		ret[0] = 253;
		*u16 = htobe16(len);
		cg_memcpy(ret + 3, s, len);
		*slen = len + 3;
	} else {
		/* size_t is only 32 bit on many platforms anyway */
		uint32_t *u32 = (uint32_t *)&ret[1];

		ret[0] = 254;
		*u32 = htobe32(len);
		cg_memcpy(ret + 5, s, len);
		*slen = len + 5;
	}
	return ret;
}

bool fulltest(const unsigned char *hash, const unsigned char *target)
{
	uint32_t *hash32 = (uint32_t *)hash;
	uint32_t *target32 = (uint32_t *)target;
	bool rc = true;
	int i;

	for (i = 28 / 4; i >= 0; i--) {
		uint32_t h32tmp = le32toh(hash32[i]);
		uint32_t t32tmp = le32toh(target32[i]);

		if (h32tmp > t32tmp) {
			rc = false;
			break;
		}
		if (h32tmp < t32tmp) {
			rc = true;
			break;
		}
	}

	if (opt_debug) {
		unsigned char hash_swap[32], target_swap[32];
		char *hash_str, *target_str;

		swab256(hash_swap, hash);
		swab256(target_swap, target);
		hash_str = bin2hex(hash_swap, 32);
		target_str = bin2hex(target_swap, 32);

		applog(LOG_DEBUG, " Proof: %s\nTarget: %s\nTrgVal? %s",
			hash_str,
			target_str,
			rc ? "YES (hash <= target)" :
			     "no (false positive; hash > target)");

		free(hash_str);
		free(target_str);
	}

	return rc;
}

struct thread_q *tq_new(void)
{
	struct thread_q *tq;

	tq = cgcalloc(1, sizeof(*tq));
	INIT_LIST_HEAD(&tq->q);
	pthread_mutex_init(&tq->mutex, NULL);
	pthread_cond_init(&tq->cond, NULL);

	return tq;
}

void tq_free(struct thread_q *tq)
{
	struct tq_ent *ent, *iter;

	if (!tq)
		return;

	list_for_each_entry_safe(ent, iter, &tq->q, q_node) {
		list_del(&ent->q_node);
		free(ent);
	}

	pthread_cond_destroy(&tq->cond);
	pthread_mutex_destroy(&tq->mutex);

	memset(tq, 0, sizeof(*tq));	/* poison */
	free(tq);
}

static void tq_freezethaw(struct thread_q *tq, bool frozen)
{
	mutex_lock(&tq->mutex);
	tq->frozen = frozen;
	pthread_cond_signal(&tq->cond);
	mutex_unlock(&tq->mutex);
}

void tq_freeze(struct thread_q *tq)
{
	tq_freezethaw(tq, true);
}

void tq_thaw(struct thread_q *tq)
{
	tq_freezethaw(tq, false);
}

bool tq_push(struct thread_q *tq, void *data)
{
	struct tq_ent *ent;
	bool rc = true;

	ent = cgcalloc(1, sizeof(*ent));
	ent->data = data;
	INIT_LIST_HEAD(&ent->q_node);

	mutex_lock(&tq->mutex);
	if (!tq->frozen) {
		list_add_tail(&ent->q_node, &tq->q);
	} else {
		free(ent);
		rc = false;
	}
	pthread_cond_signal(&tq->cond);
	mutex_unlock(&tq->mutex);

	return rc;
}
#if defined (USE_AVALON2) || defined (USE_AVALON4) || defined (USE_AVALON7) || defined (USE_AVALON8) || defined (USE_AVALON9) || defined (USE_AVALON_MINER) || defined (USE_HASHRATIO)
void *tq_pop(struct thread_q *tq, const struct timespec *abstime)
#else
void *tq_pop(struct thread_q *tq)
#endif
{
	struct tq_ent *ent;
	void *rval = NULL;
	int rc;

	mutex_lock(&tq->mutex);
	if (!list_empty(&tq->q))
		goto pop;
#if defined (USE_AVALON2) || defined (USE_AVALON4) || defined (USE_AVALON7) || defined (USE_AVALON8) || defined (USE_AVALON9) || defined (USE_AVALON_MINER) || defined (USE_HASHRATIO)
	if (abstime)
		rc = pthread_cond_timedwait(&tq->cond, &tq->mutex, abstime);
	else
#endif
	rc = pthread_cond_wait(&tq->cond, &tq->mutex);
	if (rc)
		goto out;
	if (list_empty(&tq->q))
		goto out;
pop:
	ent = list_entry(tq->q.next, struct tq_ent, q_node);
	rval = ent->data;

	list_del(&ent->q_node);
	free(ent);
out:
	mutex_unlock(&tq->mutex);

	return rval;
}

int thr_info_create(struct thr_info *thr, pthread_attr_t *attr, void *(*start) (void *), void *arg)
{
	cgsem_init(&thr->sem);

	return pthread_create(&thr->pth, attr, start, arg);
}

void thr_info_cancel(struct thr_info *thr)
{
	if (!thr)
		return;

	if (PTH(thr) != 0L) {
		pthread_cancel(thr->pth);
		PTH(thr) = 0L;
	}
	cgsem_destroy(&thr->sem);
}

void subtime(struct timeval *a, struct timeval *b)
{
	timersub(a, b, b);
}

void addtime(struct timeval *a, struct timeval *b)
{
	timeradd(a, b, b);
}

bool time_more(struct timeval *a, struct timeval *b)
{
	return timercmp(a, b, >);
}

bool time_less(struct timeval *a, struct timeval *b)
{
	return timercmp(a, b, <);
}

void copy_time(struct timeval *dest, const struct timeval *src)
{
	cg_memcpy(dest, src, sizeof(struct timeval));
}

void timespec_to_val(struct timeval *val, const struct timespec *spec)
{
	val->tv_sec = spec->tv_sec;
	val->tv_usec = spec->tv_nsec / 1000;
}

void timeval_to_spec(struct timespec *spec, const struct timeval *val)
{
	spec->tv_sec = val->tv_sec;
	spec->tv_nsec = val->tv_usec * 1000;
}

void us_to_timeval(struct timeval *val, int64_t us)
{
	lldiv_t tvdiv = lldiv(us, 1000000);

	val->tv_sec = tvdiv.quot;
	val->tv_usec = tvdiv.rem;
}

void us_to_timespec(struct timespec *spec, int64_t us)
{
	lldiv_t tvdiv = lldiv(us, 1000000);

	spec->tv_sec = tvdiv.quot;
	spec->tv_nsec = tvdiv.rem * 1000;
}

void ms_to_timespec(struct timespec *spec, int64_t ms)
{
	lldiv_t tvdiv = lldiv(ms, 1000);

	spec->tv_sec = tvdiv.quot;
	spec->tv_nsec = tvdiv.rem * 1000000;
}

void ms_to_timeval(struct timeval *val, int64_t ms)
{
	lldiv_t tvdiv = lldiv(ms, 1000);

	val->tv_sec = tvdiv.quot;
	val->tv_usec = tvdiv.rem * 1000;
}

static void spec_nscheck(struct timespec *ts)
{
	while (ts->tv_nsec >= 1000000000) {
		ts->tv_nsec -= 1000000000;
		ts->tv_sec++;
	}
	while (ts->tv_nsec < 0) {
		ts->tv_nsec += 1000000000;
		ts->tv_sec--;
	}
}

void timeraddspec(struct timespec *a, const struct timespec *b)
{
	a->tv_sec += b->tv_sec;
	a->tv_nsec += b->tv_nsec;
	spec_nscheck(a);
}

#ifdef USE_BITMAIN_SOC
static int __maybe_unused timespec_to_ms(struct timespec *ts)
{
	return ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
}

/* Subtract b from a */
static void __maybe_unused timersubspec(struct timespec *a, const struct timespec *b)
{
	a->tv_sec -= b->tv_sec;
	a->tv_nsec -= b->tv_nsec;
	spec_nscheck(a);
}
#else /* USE_BITMAIN_SOC */
static int __maybe_unused timespec_to_ms(struct timespec *ts)
{
	return ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
}

static int64_t __maybe_unused timespec_to_us(struct timespec *ts)
{
	return (int64_t)ts->tv_sec * 1000000 + ts->tv_nsec / 1000;
}

/* Subtract b from a */
static void __maybe_unused timersubspec(struct timespec *a, const struct timespec *b)
{
	a->tv_sec -= b->tv_sec;
	a->tv_nsec -= b->tv_nsec;
	spec_nscheck(a);
}
#endif /* USE_BITMAIN_SOC */

char *Strcasestr(char *haystack, const char *needle)
{
	char *lowhay, *lowneedle, *ret;
	int hlen, nlen, i, ofs;

	if (unlikely(!haystack || !needle))
		return NULL;
	hlen = strlen(haystack);
	nlen = strlen(needle);
	if (!hlen || !nlen)
		return NULL;
	lowhay = alloca(hlen);
	lowneedle = alloca(nlen);
	for (i = 0; i < hlen; i++)
		lowhay[i] = tolower(haystack[i]);
	for (i = 0; i < nlen; i++)
		lowneedle[i] = tolower(needle[i]);
	ret = strstr(lowhay, lowneedle);
	if (!ret)
		return ret;
	ofs = ret - lowhay;
	return haystack + ofs;
}

char *Strsep(char **stringp, const char *delim)
{
	char *ret = *stringp;
	char *p;

	p = (ret != NULL) ? strpbrk(ret, delim) : NULL;

	if (p == NULL)
		*stringp = NULL;
	else {
		*p = '\0';
		*stringp = p + 1;
	}

	return ret;
}

/* Get timespec specifically for use by cond_timedwait functions which use
 * CLOCK_REALTIME for expiry */
void cgcond_time(struct timespec *abstime)
{
	clock_gettime(CLOCK_REALTIME, abstime);
}

/* Get CLOCK_REALTIME for display purposes */
void cgtime_real(struct timeval *tv)
{
	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);
	tv->tv_sec = tp.tv_sec;
	tv->tv_usec = tp.tv_nsec / 1000;
}

#ifdef WIN32
/* Mingw32 has no strsep so create our own custom one  */

/* Windows start time is since 1601 LOL so convert it to unix epoch 1970. */
#define EPOCHFILETIME (116444736000000000LL)

/* These are cgminer specific sleep functions that use an absolute nanosecond
 * resolution timer to avoid poor usleep accuracy and overruns. */

/* Return the system time as an lldiv_t in decimicroseconds. */
static void decius_time(lldiv_t *lidiv)
{
	FILETIME ft;
	LARGE_INTEGER li;

	GetSystemTimeAsFileTime(&ft);
	li.LowPart  = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;
	li.QuadPart -= EPOCHFILETIME;

	/* SystemTime is in decimicroseconds so divide by an unusual number */
	*lidiv = lldiv(li.QuadPart, 10000000);
}

/* This is a cgminer gettimeofday wrapper. Since we always call gettimeofday
 * with tz set to NULL, and windows' default resolution is only 15ms, this
 * gives us higher resolution times on windows. */
void cgtime(struct timeval *tv)
{
	lldiv_t lidiv;

	decius_time(&lidiv);
	tv->tv_sec = lidiv.quot;
	tv->tv_usec = lidiv.rem / 10;
}

#else /* WIN32 */
void cgtime(struct timeval *tv)
{
	cgtimer_t cgt;

	cgtimer_time(&cgt);
	timespec_to_val(tv, &cgt);
}

int cgtimer_to_ms(cgtimer_t *cgt)
{
	return timespec_to_ms(cgt);
}

/* Subtracts b from a and stores it in res. */
void cgtimer_sub(cgtimer_t *a, cgtimer_t *b, cgtimer_t *res)
{
	res->tv_sec = a->tv_sec - b->tv_sec;
	res->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (res->tv_nsec < 0) {
		res->tv_nsec += 1000000000;
		res->tv_sec--;
	}
}
#endif /* WIN32 */

#if defined(CLOCK_MONOTONIC) && !defined(__FreeBSD__) && !defined(__APPLE__) && !defined(WIN32) /* Essentially just linux */
//#ifdef CLOCK_MONOTONIC /* Essentially just linux */
void cgtimer_time(cgtimer_t *ts_start)
{
	clock_gettime(CLOCK_MONOTONIC, ts_start);
}

static void nanosleep_abstime(struct timespec *ts_end)
{
	int ret;

	do {
		ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ts_end, NULL);
	} while (ret == EINTR);
}

/* Reentrant version of cgsleep functions allow start time to be set separately
 * from the beginning of the actual sleep, allowing scheduling delays to be
 * counted in the sleep. */
#ifdef USE_BITMAIN_SOC
void cgsleep_ms_r(cgtimer_t *ts_start, int ms)
{
	struct timespec ts_end;

	ms_to_timespec(&ts_end, ms);
	timeraddspec(&ts_end, ts_start);
	nanosleep_abstime(&ts_end);
}

void cgsleep_us_r(cgtimer_t *ts_start, int64_t us)
{
	struct timespec ts_end;

	us_to_timespec(&ts_end, us);
	timeraddspec(&ts_end, ts_start);
	nanosleep_abstime(&ts_end);
}
#else /* USE_BITMAIN_SOC */
int cgsleep_ms_r(cgtimer_t *ts_start, int ms)
{
	struct timespec ts_end, ts_diff;
	int msdiff;

	ms_to_timespec(&ts_end, ms);
	timeraddspec(&ts_end, ts_start);
	cgtimer_time(&ts_diff);
	/* Should be a negative value if we still have to sleep */
	timersubspec(&ts_diff, &ts_end);
	msdiff = -timespec_to_ms(&ts_diff);
	if (msdiff <= 0)
		return 0;

	nanosleep_abstime(&ts_end);
	return msdiff;
}

int64_t cgsleep_us_r(cgtimer_t *ts_start, int64_t us)
{
	struct timespec ts_end, ts_diff;
	int64_t usdiff;

	us_to_timespec(&ts_end, us);
	timeraddspec(&ts_end, ts_start);
	cgtimer_time(&ts_diff);
	usdiff = -timespec_to_us(&ts_diff);
	if (usdiff <= 0)
		return 0;

	nanosleep_abstime(&ts_end);
	return usdiff;
}
#endif /* USE_BITMAIN_SOC */
#else /* CLOCK_MONOTONIC */
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
void cgtimer_time(cgtimer_t *ts_start)
{
	clock_serv_t cclock;
	mach_timespec_t mts;

	host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
	clock_get_time(cclock, &mts);
	mach_port_deallocate(mach_task_self(), cclock);
	ts_start->tv_sec = mts.tv_sec;
	ts_start->tv_nsec = mts.tv_nsec;
}
#elif !defined(WIN32) /* __MACH__ - Everything not linux/macosx/win32 */
void cgtimer_time(cgtimer_t *ts_start)
{
	struct timeval tv;

	cgtime(&tv);
	ts_start->tv_sec = tv.tv_sec;
	ts_start->tv_nsec = tv.tv_usec * 1000;
}
#endif /* __MACH__ */

#ifdef WIN32
/* For windows we use the SystemTime stored as a LARGE_INTEGER as the cgtimer_t
 * typedef, allowing us to have sub-microsecond resolution for times, do simple
 * arithmetic for timer calculations, and use windows' own hTimers to get
 * accurate absolute timeouts. */
int cgtimer_to_ms(cgtimer_t *cgt)
{
	return (int)(cgt->QuadPart / 10000LL);
}

/* Subtracts b from a and stores it in res. */
void cgtimer_sub(cgtimer_t *a, cgtimer_t *b, cgtimer_t *res)
{
	res->QuadPart = a->QuadPart - b->QuadPart;
}

/* Note that cgtimer time is NOT offset by the unix epoch since we use absolute
 * timeouts with hTimers. */
void cgtimer_time(cgtimer_t *ts_start)
{
	FILETIME ft;

	GetSystemTimeAsFileTime(&ft);
	ts_start->LowPart = ft.dwLowDateTime;
	ts_start->HighPart = ft.dwHighDateTime;
}

static void liSleep(LARGE_INTEGER *li, int timeout)
{
	HANDLE hTimer;
	DWORD ret;

	if (unlikely(timeout <= 0))
		return;

	hTimer = CreateWaitableTimer(NULL, TRUE, NULL);
	if (unlikely(!hTimer))
		quit(1, "Failed to create hTimer in liSleep");
	ret = SetWaitableTimer(hTimer, li, 0, NULL, NULL, 0);
	if (unlikely(!ret))
		quit(1, "Failed to SetWaitableTimer in liSleep");
	/* We still use a timeout as a sanity check in case the system time
	 * is changed while we're running */
	ret = WaitForSingleObject(hTimer, timeout);
	if (unlikely(ret != WAIT_OBJECT_0 && ret != WAIT_TIMEOUT))
		quit(1, "Failed to WaitForSingleObject in liSleep");
	CloseHandle(hTimer);
}

void cgsleep_ms_r(cgtimer_t *ts_start, int ms)
{
	LARGE_INTEGER li;

	li.QuadPart = ts_start->QuadPart + (int64_t)ms * 10000LL;
	liSleep(&li, ms);
}

void cgsleep_us_r(cgtimer_t *ts_start, int64_t us)
{
	LARGE_INTEGER li;
	int ms;

	li.QuadPart = ts_start->QuadPart + us * 10LL;
	ms = us / 1000;
	if (!ms)
		ms = 1;
	liSleep(&li, ms);
}
#else /* WIN32 */
static void cgsleep_spec(struct timespec *ts_diff, const struct timespec *ts_start)
{
	struct timespec now;

	timeraddspec(ts_diff, ts_start);
	cgtimer_time(&now);
	timersubspec(ts_diff, &now);
	if (unlikely(ts_diff->tv_sec < 0))
		return;
	nanosleep(ts_diff, NULL);
}

void cgsleep_ms_r(cgtimer_t *ts_start, int ms)
{
	struct timespec ts_diff;

	ms_to_timespec(&ts_diff, ms);
	cgsleep_spec(&ts_diff, ts_start);
}

void cgsleep_us_r(cgtimer_t *ts_start, int64_t us)
{
	struct timespec ts_diff;

	us_to_timespec(&ts_diff, us);
	cgsleep_spec(&ts_diff, ts_start);
}
#endif /* WIN32 */
#endif /* CLOCK_MONOTONIC */

void cgsleep_ms(int ms)
{
	cgtimer_t ts_start;

	cgsleep_prepare_r(&ts_start);
	cgsleep_ms_r(&ts_start, ms);
}

static void busywait_us(int64_t us)
{
	struct timeval diff, end, now;

	cgtime(&end);
	us_to_timeval(&diff, us);
	addtime(&diff, &end);
	do {
		sched_yield();
		cgtime(&now);
	} while (time_less(&now, &end));
}

void cgsleep_us(int64_t us)
{
	cgtimer_t ts_start;

	/* Most timer resolution is unlikely to be able to sleep accurately
	 * for less than 1ms so busywait instead. */
	if (us < 1000)
		return busywait_us(us);
	cgsleep_prepare_r(&ts_start);
	cgsleep_us_r(&ts_start, us);
}

/* Returns the microseconds difference between end and start times as a double */
double us_tdiff(struct timeval *end, struct timeval *start)
{
	/* Sanity check. We should only be using this for small differences so
	 * limit the max to 60 seconds. */
	if (unlikely(end->tv_sec - start->tv_sec > 60))
		return 60000000;
	return (end->tv_sec - start->tv_sec) * 1000000 + (end->tv_usec - start->tv_usec);
}

/* Returns the milliseconds difference between end and start times */
int ms_tdiff(struct timeval *end, struct timeval *start)
{
	/* Like us_tdiff, limit to 1 hour. */
	if (unlikely(end->tv_sec - start->tv_sec > 3600))
		return 3600000;
	return (end->tv_sec - start->tv_sec) * 1000 + (end->tv_usec - start->tv_usec) / 1000;
}

/* Returns the seconds difference between end and start times as a double */
double tdiff(struct timeval *end, struct timeval *start)
{
	return end->tv_sec - start->tv_sec + (end->tv_usec - start->tv_usec) / 1000000.0;
}

bool extract_sockaddr(char *url, char **sockaddr_url, char **sockaddr_port)
{
	char *url_begin, *url_end, *ipv6_begin, *ipv6_end, *port_start = NULL;
	char url_address[256], port[6];
	int url_len, port_len = 0;

	*sockaddr_url = url;
	url_begin = strstr(url, "//");
	if (!url_begin)
		url_begin = url;
	else
		url_begin += 2;

	/* Look for numeric ipv6 entries */
	ipv6_begin = strstr(url_begin, "[");
	ipv6_end = strstr(url_begin, "]");
	if (ipv6_begin && ipv6_end && ipv6_end > ipv6_begin)
		url_end = strstr(ipv6_end, ":");
	else
		url_end = strstr(url_begin, ":");
	if (url_end) {
		url_len = url_end - url_begin;
		port_len = strlen(url_begin) - url_len - 1;
		if (port_len < 1)
			return false;
		port_start = url_end + 1;
	} else
		url_len = strlen(url_begin);

	if (url_len < 1)
		return false;

	/* Get rid of the [] */
	if (ipv6_begin && ipv6_end && ipv6_end > ipv6_begin) {
		url_len -= 2;
		url_begin++;
	}

	snprintf(url_address, 254, "%.*s", url_len, url_begin);

	if (port_len) {
		char *slash;

		snprintf(port, 6, "%.*s", port_len, port_start);
#ifdef USE_XTRANONCE
		slash = strpbrk(port, "/#");
#else
		slash = strchr(port, '/');
#endif
		if (slash)
			*slash = '\0';
	} else
		strcpy(port, "80");

	*sockaddr_port = strdup(port);
	*sockaddr_url = strdup(url_address);

	return true;
}

enum send_ret {
	SEND_OK,
	SEND_SELECTFAIL,
	SEND_SENDFAIL,
	SEND_INACTIVE
};

/* Send a single command across a socket, appending \n to it. This should all
 * be done under stratum lock except when first establishing the socket */
static enum send_ret __stratum_send(struct pool *pool, char *s, ssize_t len)
{
	SOCKETTYPE sock = pool->sock;
	ssize_t ssent = 0;

	strcat(s, "\n");
	len++;

	while (len > 0 ) {
		struct timeval timeout = {1, 0};
		ssize_t sent;
		fd_set wd;
retry:
		FD_ZERO(&wd);
		FD_SET(sock, &wd);
		if (select(sock + 1, NULL, &wd, NULL, &timeout) < 1) {
			if (interrupted())
				goto retry;
			return SEND_SELECTFAIL;
		}
#ifdef __APPLE__
		sent = send(pool->sock, s + ssent, len, SO_NOSIGPIPE);
#elif WIN32
		sent = send(pool->sock, s + ssent, len, 0);
#else
		sent = send(pool->sock, s + ssent, len, MSG_NOSIGNAL);
#endif
		if (sent < 0) {
			if (!sock_blocks())
				return SEND_SENDFAIL;
			sent = 0;
		}
		ssent += sent;
		len -= sent;
	}

	pool->cgminer_pool_stats.times_sent++;
	pool->cgminer_pool_stats.bytes_sent += ssent;
	pool->cgminer_pool_stats.net_bytes_sent += ssent;
	return SEND_OK;
}

bool stratum_send(struct pool *pool, char *s, ssize_t len)
{
	enum send_ret ret = SEND_INACTIVE;

	if (opt_protocol)
		applog(LOG_DEBUG, "SEND: %s", s);

	mutex_lock(&pool->stratum_lock);
	if (pool->stratum_active)
		ret = __stratum_send(pool, s, len);
	mutex_unlock(&pool->stratum_lock);

	/* This is to avoid doing applog under stratum_lock */
	switch (ret) {
		default:
		case SEND_OK:
			break;
		case SEND_SELECTFAIL:
			applog(LOG_DEBUG, "Write select failed on pool %d sock", pool->pool_no);
			suspend_stratum(pool);
			break;
		case SEND_SENDFAIL:
			applog(LOG_DEBUG, "Failed to send in stratum_send");
			suspend_stratum(pool);
			break;
		case SEND_INACTIVE:
			applog(LOG_DEBUG, "Stratum send failed due to no pool stratum_active");
			break;
	}
	return (ret == SEND_OK);
}

static bool socket_full(struct pool *pool, int wait)
{
	SOCKETTYPE sock = pool->sock;
	struct timeval timeout;
	fd_set rd;

	if (unlikely(wait < 0))
		wait = 0;
	FD_ZERO(&rd);
	FD_SET(sock, &rd);
	timeout.tv_usec = 0;
	timeout.tv_sec = wait;
	if (select(sock + 1, &rd, NULL, NULL, &timeout) > 0)
		return true;
	return false;
}

/* Check to see if Santa's been good to you */
bool sock_full(struct pool *pool)
{
	if (strlen(pool->sockbuf))
		return true;

	return (socket_full(pool, 0));
}

static void clear_sockbuf(struct pool *pool)
{
	if (likely(pool->sockbuf))
		strcpy(pool->sockbuf, "");
}

static void clear_sock(struct pool *pool)
{
	ssize_t n;

	mutex_lock(&pool->stratum_lock);
	do {
		if (pool->sock)
			n = recv(pool->sock, pool->sockbuf, RECVSIZE, 0);
		else
			n = 0;
	} while (n > 0);
	mutex_unlock(&pool->stratum_lock);

	clear_sockbuf(pool);
}

/* Realloc memory to new size and zero any extra memory added */
void ckrecalloc(void **ptr, size_t old, size_t new, const char *file, const char *func, const int line)
{
	if (new == old)
		return;
	*ptr = _cgrealloc(*ptr, new, file, func, line);
	if (new > old)
		memset(*ptr + old, 0, new - old);
}

/* Make sure the pool sockbuf is large enough to cope with any coinbase size
 * by reallocing it to a large enough size rounded up to a multiple of RBUFSIZE
 * and zeroing the new memory */
static void recalloc_sock(struct pool *pool, size_t len)
{
	size_t old, new;

	old = strlen(pool->sockbuf);
	new = old + len + 1;
	if (new < pool->sockbuf_size)
		return;
	new = new + (RBUFSIZE - (new % RBUFSIZE));
	// Avoid potentially recursive locking
	// applog(LOG_DEBUG, "Recallocing pool sockbuf to %d", new);
	pool->sockbuf = cgrealloc(pool->sockbuf, new);
	memset(pool->sockbuf + old, 0, new - old);
	pool->sockbuf_size = new;
}

/* Peeks at a socket to find the first end of line and then reads just that
 * from the socket and returns that as a malloced char */
char *recv_line(struct pool *pool)
{
	char *tok, *sret = NULL;
	ssize_t len, buflen;
	int waited = 0;

	if (!strstr(pool->sockbuf, "\n")) {
		struct timeval rstart, now;

		cgtime(&rstart);
		if (!socket_full(pool, DEFAULT_SOCKWAIT)) {
			applog(LOG_DEBUG, "Timed out waiting for data on socket_full");
			goto out;
		}

		do {
			char s[RBUFSIZE];
			size_t slen;
			ssize_t n;

			memset(s, 0, RBUFSIZE);
			n = recv(pool->sock, s, RECVSIZE, 0);
			if (!n) {
				applog(LOG_DEBUG, "Socket closed waiting in recv_line");
				suspend_stratum(pool);
				break;
			}
			cgtime(&now);
			waited = tdiff(&now, &rstart);
			if (n < 0) {
				if (!sock_blocks() || !socket_full(pool, DEFAULT_SOCKWAIT - waited)) {
					applog(LOG_DEBUG, "Failed to recv sock in recv_line");
					suspend_stratum(pool);
					break;
				}
			} else {
				slen = strlen(s);
				recalloc_sock(pool, slen);
				strcat(pool->sockbuf, s);
			}
		} while (waited < DEFAULT_SOCKWAIT && !strstr(pool->sockbuf, "\n"));
	}

	buflen = strlen(pool->sockbuf);
	tok = strtok(pool->sockbuf, "\n");
	if (!tok) {
		applog(LOG_DEBUG, "Failed to parse a \\n terminated string in recv_line");
		goto out;
	}
	sret = strdup(tok);
	len = strlen(sret);

	/* Copy what's left in the buffer after the \n, including the
	 * terminating \0 */
	if (buflen > len + 1)
		memmove(pool->sockbuf, pool->sockbuf + len + 1, buflen - len + 1);
	else
		strcpy(pool->sockbuf, "");

	pool->cgminer_pool_stats.times_received++;
	pool->cgminer_pool_stats.bytes_received += len;
	pool->cgminer_pool_stats.net_bytes_received += len;
out:
	if (!sret)
		clear_sock(pool);
	else if (opt_protocol)
		applog(LOG_DEBUG, "RECVD: %s", sret);
	return sret;
}

/* Extracts a string value from a json array with error checking. To be used
 * when the value of the string returned is only examined and not to be stored.
 * See json_array_string below */
static char *__json_array_string(json_t *val, unsigned int entry)
{
	json_t *arr_entry;

	if (json_is_null(val))
		return NULL;
	if (!json_is_array(val))
		return NULL;
	if (entry > json_array_size(val))
		return NULL;
	arr_entry = json_array_get(val, entry);
	if (!json_is_string(arr_entry))
		return NULL;

	return (char *)json_string_value(arr_entry);
}

/* Creates a freshly malloced dup of __json_array_string */
static char *json_array_string(json_t *val, unsigned int entry)
{
	char *buf = __json_array_string(val, entry);

	if (buf)
		return strdup(buf);
	return NULL;
}

static char *blank_merkle = "0000000000000000000000000000000000000000000000000000000000000000";

#ifdef HAVE_LIBCURL
static void decode_exit(struct pool *pool, char *cb)
{
	CURL *curl = curl_easy_init();
	char *decreq, *s;
	json_t *val;
	int dummy;

	if (!opt_btcd && !sleep(3) && !opt_btcd) {
		applog(LOG_ERR, "No bitcoind specified, unable to decode coinbase.");
		exit(1);
	}
	decreq = cgmalloc(strlen(cb) + 256);

	sprintf(decreq, "{\"id\": 0, \"method\": \"decoderawtransaction\", \"params\": [\"%s\"]}\n",
		cb);
	val = json_rpc_call(curl, opt_btcd->rpc_url, opt_btcd->rpc_userpass, decreq,
			    false, false, &dummy, opt_btcd, false);
	free(decreq);
	if (!val) {
		applog(LOG_ERR, "Failed json_rpc_call to btcd %s", opt_btcd->rpc_url);
		exit(1);
	}
	s = json_dumps(val, JSON_INDENT(4));
	printf("Pool %s:\n%s\n", pool->rpc_url, s);
	free(s);
	exit(0);
}
#else
static void decode_exit(struct pool __maybe_unused *pool, char __maybe_unused *b)
{
}
#endif

static int calculate_num_bits(int num)
{
	int ret=0;
	while(num != 0)
	{
		ret++;
		num /= 16;
	}
	return ret;
}

static void get_vmask(struct pool *pool, char *bbversion)
{
	char defaultStr[9]= "00000000";
	int bversion, num_bits, i, j;
	uint8_t buffer[4] = {};
	uint32_t uiMagicNum;
	char *tmpstr;
	uint32_t *p1;

	p1 = (uint32_t *)buffer;
	bversion = strtol(bbversion, NULL, 16);

	for (i = 0; i < 4; i++) {
		uiMagicNum = bversion | pool->vmask_003[i];
		//printf("[ccx]uiMagicNum:0x%x. \n", uiMagicNum);
		*p1 = bswap_32(uiMagicNum);

		//printf("[ccx]*p1:0x%x. \n", *p1);
		switch(i) {
			case 0:
				pool->vmask_001[8] = *p1;
				break;
			case 1:
				pool->vmask_001[4] = *p1;
				break;
			case 2:
				pool->vmask_001[2] = *p1;
				break;
			case 3:
				pool->vmask_001[0] = *p1;
				break;
			default:
				break;
		}
	}

	for (i = 0; i < 16; i++) {
		if ((i!= 2) && (i!=4) && (i!=8))
			pool->vmask_001[i] = pool->vmask_001[0];
	}

	for (i = 0; i < 16; i++)
		memcpy(pool->vmask_002[i], defaultStr, 9);

	for (i = 0; i < 3; i++) {
		char cMask[12];

		tmpstr = (char *)cgcalloc(9, 1);
		num_bits = calculate_num_bits(pool->vmask_003[i]);
		for (j = 0; j < (8-num_bits); j++)
			tmpstr[j] = '0';

		snprintf(cMask, 9, "%x", pool->vmask_003[i]);
		memcpy(tmpstr + 8 - num_bits, cMask, num_bits);
		tmpstr[8] = '\0';

		//printf("[ccx]tmpstr:%s. \n", tmpstr);
		switch(i) {
			case 0:
				memcpy(pool->vmask_002[8], tmpstr, 9);
				break;
			case 1:
				memcpy(pool->vmask_002[4], tmpstr, 9);
				break;
			case 2:
				memcpy(pool->vmask_002[2], tmpstr, 9);
				break;
			default:
				break;
		}
		free(tmpstr);
	}
}

static bool set_vmask(struct pool *pool, json_t *val)
{
	int mask, tmpMask = 0, cnt = 0, i, rem;
	const char *version_mask;

	version_mask = json_string_value(val);
	applog(LOG_INFO, "Pool %d version_mask:%s.", pool->pool_no, version_mask);

	mask = strtol(version_mask, NULL, 16);
	if (!mask)
		return false;

	pool->vmask_003[0] = mask;

	while (mask % 16 == 0) {
		cnt++;
		mask /= 16;
	}

	if ((rem = mask % 16))
		tmpMask = rem;
	else if ((rem = mask % 8))
		tmpMask = rem;
	else if ((rem = mask % 4))
		tmpMask = rem;
	else if ((rem = mask % 2))
		tmpMask = rem;

	for (i = 0; i < cnt; i++)
		tmpMask *= 16;
	pool->vmask_003[2] = tmpMask;
	pool->vmask_003[1] = pool->vmask_003[0] - tmpMask;

	return true;
}

#ifdef USE_VMASK

#define STRATUM_VERSION_ROLLING "version-rolling"
#define STRATUM_VERSION_ROLLING_LEN (sizeof(STRATUM_VERSION_ROLLING) - 1)

/**
 * Configures stratum mining based on connected hardware capabilities
 * (version rolling etc.)
 *
 * Sample communication
 * Request:
 * {"id": 1, "method": "mining.configure", "params": [ ["version-rolling"], "version-rolling.mask": "ffffffff" }]}\n
 * Response:
 * {"id": 1, "result": { "version-rolling": True, "version-rolling.mask": "00003000" }, "error": null}\n
 *
 * @param pool
 *
 *
 * @return
 */
static bool configure_stratum_mining(struct pool *pool)
{
	char s[RBUFSIZE];
	char *response_str = NULL;
	bool config_status = false;
	bool version_rolling_status = false;
	bool version_mask_valid = false;
	const char *key;
	json_t *response, *value, *res_val, *err_val;
	json_error_t err;

	snprintf(s, RBUFSIZE,
		 "{\"id\": %d, \"method\": \"mining.configure\", \"params\": "
		 "[[\""STRATUM_VERSION_ROLLING"\"], "
		 "{\""STRATUM_VERSION_ROLLING".mask\": \"%x\""
		 "}]}",
	  swork_id++, 0xffffffff);

	if (__stratum_send(pool, s, strlen(s)) != SEND_OK) {
		applog(LOG_DEBUG, "Failed to send mining.configure");
		goto out;
	}
	if (!socket_full(pool, DEFAULT_SOCKWAIT)) {
		applog(LOG_DEBUG, "Timed out waiting for response in %s", __FUNCTION__);
		goto out;
	}
	response_str = recv_line(pool);
	if (!response_str)
		goto out;

	response = JSON_LOADS(response_str, &err);
	free(response_str);

	res_val = json_object_get(response, "result");
	err_val = json_object_get(response, "error");

	if (!res_val || json_is_null(res_val) ||
		(err_val && !json_is_null(err_val))) {
				char *ss;

			if (err_val)
				ss = json_dumps(err_val, JSON_INDENT(3));
			else
				ss = strdup("(unknown reason)");

			applog(LOG_INFO, "JSON-RPC decode failed: %s", ss);

			free(ss);

			goto json_response_error;
	}

	json_object_foreach(res_val, key, value) {
		if (!strcasecmp(key, STRATUM_VERSION_ROLLING) &&
		    strlen(key) == STRATUM_VERSION_ROLLING_LEN)
			version_rolling_status = json_boolean_value(value);
		else if (!strcasecmp(key, STRATUM_VERSION_ROLLING ".mask"))
			pool->vmask = version_mask_valid = set_vmask(pool, value);
		else
			applog(LOG_ERR, "JSON-RPC unexpected mining.configure value: %s", key);
	}

	/* Valid configuration for now only requires enabled version rolling and valid bit mask */
	config_status = version_rolling_status && version_mask_valid;

	json_response_error:
	json_decref(response);

out:
	return config_status;
}
#else
static inline bool configure_stratum_mining(struct pool __maybe_unused *pool)
{
	return true;
}
#endif

static bool parse_notify(struct pool *pool, json_t *val)
{
#ifdef USE_AVALON7
	static int32_t th_clean_jobs;
	static struct timeval last_notify;
	struct timeval current;
#endif
	char *job_id, *prev_hash, *coinbase1, *coinbase2, *bbversion, *nbit,
	     *ntime, header[260];
	unsigned char *cb1 = NULL, *cb2 = NULL;
	size_t cb1_len, cb2_len, alloc_len;
	bool clean, ret = false;
	int merkles, i;
	json_t *arr;

	arr = json_array_get(val, 4);
	if (!arr || !json_is_array(arr))
		goto out;

	merkles = json_array_size(arr);

	job_id = json_array_string(val, 0);
	prev_hash = __json_array_string(val, 1);
	coinbase1 = json_array_string(val, 2);
	coinbase2 = json_array_string(val, 3);
	bbversion = __json_array_string(val, 5);
	nbit = __json_array_string(val, 6);
	ntime = __json_array_string(val, 7);
	clean = json_is_true(json_array_get(val, 8));

	get_vmask(pool, bbversion);

	if (!valid_ascii(job_id) || !valid_hex(prev_hash) || !valid_hex(coinbase1) ||
	    !valid_hex(coinbase2) || !valid_hex(bbversion) || !valid_hex(nbit) ||
	    !valid_hex(ntime)) {
		/* Annoying but we must not leak memory */
		free(job_id);
		free(coinbase1);
		free(coinbase2);
		goto out;
	}

	cg_wlock(&pool->data_lock);
	free(pool->swork.job_id);
	pool->swork.job_id = job_id;
	if (memcmp(pool->prev_hash, prev_hash, 64)) {
		pool->swork.clean = true;
	} else {
		pool->swork.clean = clean;
	}
	snprintf(pool->prev_hash, 65, "%s", prev_hash);
	cb1_len = strlen(coinbase1) / 2;
	cb2_len = strlen(coinbase2) / 2;
	snprintf(pool->bbversion, 9, "%s", bbversion);
	snprintf(pool->nbit, 9, "%s", nbit);
	snprintf(pool->ntime, 9, "%s", ntime);
	if (pool->next_diff > 0) {
		pool->sdiff = pool->next_diff;
		pool->next_diff = pool->diff_after;
		pool->diff_after = 0;
	}
	alloc_len = pool->coinbase_len = cb1_len + pool->n1_len + pool->n2size + cb2_len;
	pool->nonce2_offset = cb1_len + pool->n1_len;

	for (i = 0; i < pool->merkles; i++)
		free(pool->swork.merkle_bin[i]);
	if (merkles) {
		pool->swork.merkle_bin = cgrealloc(pool->swork.merkle_bin,
						   sizeof(char *) * merkles + 1);
		for (i = 0; i < merkles; i++) {
			char *merkle = json_array_string(arr, i);

			pool->swork.merkle_bin[i] = cgmalloc(32);
			if (opt_protocol)
				applog(LOG_DEBUG, "merkle %d: %s", i, merkle);
			ret = hex2bin(pool->swork.merkle_bin[i], merkle, 32);
			free(merkle);
			if (unlikely(!ret)) {
				applog(LOG_ERR, "Failed to convert merkle to merkle_bin in parse_notify");
				goto out_unlock;
			}
		}
	}
	pool->merkles = merkles;
	if (pool->merkles < 2)
		pool->bad_work++;
	if (clean)
		pool->nonce2 = 0;
#if 0
	header_len = 		 strlen(pool->bbversion) +
				 strlen(pool->prev_hash);
	/* merkle_hash */	 32 +
				 strlen(pool->ntime) +
				 strlen(pool->nbit) +
	/* nonce */		 8 +
	/* workpadding */	 96;
#endif
	snprintf(header, 257,
		"%s%s%s%s%s%s%s",
		pool->bbversion,
		pool->prev_hash,
		blank_merkle,
		pool->ntime,
		pool->nbit,
		"00000000", /* nonce */
		workpadding);

	ret = hex2bin(pool->header_bin, header, 128);
	if (unlikely(!ret)) {
		applog(LOG_ERR, "Failed to convert header to header_bin in parse_notify");
		goto out_unlock;
	}

	cb1 = alloca(cb1_len);
	ret = hex2bin(cb1, coinbase1, cb1_len);
	if (unlikely(!ret)) {
		applog(LOG_ERR, "Failed to convert cb1 to cb1_bin in parse_notify");
		goto out_unlock;
	}
	cb2 = alloca(cb2_len);
	ret = hex2bin(cb2, coinbase2, cb2_len);
	if (unlikely(!ret)) {
		applog(LOG_ERR, "Failed to convert cb2 to cb2_bin in parse_notify");
		goto out_unlock;
	}
	free(pool->coinbase);
	pool->coinbase = cgcalloc(alloc_len, 1);
	cg_memcpy(pool->coinbase, cb1, cb1_len);
	if (pool->n1_len)
		cg_memcpy(pool->coinbase + cb1_len, pool->nonce1bin, pool->n1_len);
	cg_memcpy(pool->coinbase + cb1_len + pool->n1_len + pool->n2size, cb2, cb2_len);
	if (opt_debug || opt_decode) {
		char *cb = bin2hex(pool->coinbase, pool->coinbase_len);

		if (opt_decode)
			decode_exit(pool, cb);
		applog(LOG_DEBUG, "Pool %d coinbase %s", pool->pool_no, cb);
		free(cb);
	}
out_unlock:
	cg_wunlock(&pool->data_lock);

	if (opt_protocol) {
		applog(LOG_DEBUG, "job_id: %s", job_id);
		applog(LOG_DEBUG, "prev_hash: %s", prev_hash);
		applog(LOG_DEBUG, "coinbase1: %s", coinbase1);
		applog(LOG_DEBUG, "coinbase2: %s", coinbase2);
		applog(LOG_DEBUG, "bbversion: %s", bbversion);
		applog(LOG_DEBUG, "nbit: %s", nbit);
		applog(LOG_DEBUG, "ntime: %s", ntime);
		applog(LOG_DEBUG, "clean: %s", clean ? "yes" : "no");
	}
	free(coinbase1);
	free(coinbase2);

	/* A notify message is the closest stratum gets to a getwork */
	pool->getwork_requested++;
	total_getworks++;
	if (pool == current_pool()) {
		opt_work_update = true;
#ifdef USE_AVALON7
		if (opt_avalon7_ssplus_enable & pool->has_stratum) {
			/* -1:Ignore, 0:Accept, n:Accept after n seconds, n > 0 */
			if (opt_force_clean_jobs == -1 && clean)
				opt_clean_jobs = true;

			if (!opt_force_clean_jobs)
				opt_clean_jobs = true;

			if (opt_force_clean_jobs > 0) {
				if (clean)
					opt_clean_jobs = true;
				else {
					if (!last_notify.tv_sec && !last_notify.tv_usec)
						cgtime(&last_notify);
					else {
						cgtime(&current);
						th_clean_jobs += (int32_t)ms_tdiff(&current, &last_notify);
						cgtime(&last_notify);
					}

					if (th_clean_jobs >= opt_force_clean_jobs * 1000)
						opt_clean_jobs = true;
				}
			}

			if (opt_clean_jobs) {
				ssp_hasher_update_stratum(pool, true);
				th_clean_jobs = 0;
			}
		}
#endif
	}
out:
	return ret;
}

static bool parse_diff(struct pool *pool, json_t *val)
{
	double old_diff, diff;

	diff = json_number_value(json_array_get(val, 0));
	if (diff <= 0)
		return false;

	/* We can only change one diff per notify so assume diffs are being
	 * stacked for successive notifies. */
	cg_wlock(&pool->data_lock);
	if (pool->next_diff)
		pool->diff_after = diff;
	else
		pool->next_diff = diff;
	old_diff = pool->sdiff;
	cg_wunlock(&pool->data_lock);

	if (old_diff != diff) {
		int idiff = diff;

		if ((double)idiff == diff)
			applog(LOG_NOTICE, "Pool %d difficulty changed to %d",
			       pool->pool_no, idiff);
		else
			applog(LOG_NOTICE, "Pool %d difficulty changed to %.1f",
			       pool->pool_no, diff);
	} else
		applog(LOG_DEBUG, "Pool %d difficulty set to %f", pool->pool_no,
		       diff);

	return true;
}

#ifdef USE_XTRANONCE
static bool parse_extranonce(struct pool *pool, json_t *val)
{
	char s[RBUFSIZE], *nonce1;
	int n2size;

	nonce1 = json_array_string(val, 0);
	if (!valid_hex(nonce1)) {
		applog(LOG_INFO, "Failed to get valid nonce1 in parse_extranonce");
		return false;
	}
	n2size = json_integer_value(json_array_get(val, 1));
	if (!n2size) {
		applog(LOG_INFO, "Failed to get valid n2size in parse_extranonce");
		free(nonce1);
		return false;
	}

	cg_wlock(&pool->data_lock);
	free(pool->nonce1);
	pool->nonce1 = nonce1;
	pool->n1_len = strlen(nonce1) / 2;
	free(pool->nonce1bin);
	pool->nonce1bin = (unsigned char *)calloc(pool->n1_len, 1);
	if (unlikely(!pool->nonce1bin))
		quithere(1, "Failed to calloc pool->nonce1bin");
	hex2bin(pool->nonce1bin, pool->nonce1, pool->n1_len);
	pool->n2size = n2size;
	cg_wunlock(&pool->data_lock);

	applog(LOG_NOTICE, "Pool %d extranonce change requested", pool->pool_no);

	return true;
}
#endif

static void __suspend_stratum(struct pool *pool)
{
	clear_sockbuf(pool);
	pool->stratum_active = pool->stratum_notify = false;
	if (pool->sock)
		CLOSESOCKET(pool->sock);
	pool->sock = 0;
}

static bool parse_reconnect(struct pool *pool, json_t *val)
{
	char *sockaddr_url, *stratum_port, *tmp;
	char *url, *port, address[256];
	int port_no;

	memset(address, 0, 255);
	url = (char *)json_string_value(json_array_get(val, 0));
	if (!url)
		url = pool->sockaddr_url;
	else {
		char *dot_pool, *dot_reconnect;
		dot_pool = strchr(pool->sockaddr_url, '.');
		if (!dot_pool) {
			applog(LOG_ERR, "Denied stratum reconnect request for pool without domain '%s'",
			       pool->sockaddr_url);
			return false;
		}
		dot_reconnect = strchr(url, '.');
		if (!dot_reconnect) {
			applog(LOG_ERR, "Denied stratum reconnect request to url without domain '%s'",
			       url);
			return false;
		}
		if (strcmp(dot_pool, dot_reconnect)) {
			applog(LOG_ERR, "Denied stratum reconnect request to non-matching domain url '%s'",
				pool->sockaddr_url);
			return false;
		}
	}

	port_no = json_integer_value(json_array_get(val, 1));
	if (port_no) {
		port = alloca(256);
		sprintf(port, "%d", port_no);
	} else {
		port = (char *)json_string_value(json_array_get(val, 1));
		if (!port)
			port = pool->stratum_port;
	}

	snprintf(address, 254, "%s:%s", url, port);

	if (!extract_sockaddr(address, &sockaddr_url, &stratum_port))
		return false;

	applog(LOG_WARNING, "Stratum reconnect requested from pool %d to %s", pool->pool_no, address);

	clear_pool_work(pool);

	mutex_lock(&pool->stratum_lock);
	__suspend_stratum(pool);
	tmp = pool->sockaddr_url;
	pool->sockaddr_url = sockaddr_url;
	pool->stratum_url = pool->sockaddr_url;
	free(tmp);
	tmp = pool->stratum_port;
	pool->stratum_port = stratum_port;
	free(tmp);
	mutex_unlock(&pool->stratum_lock);

	return restart_stratum(pool);
}

static bool send_version(struct pool *pool, json_t *val)
{
	json_t *id_val = json_object_get(val, "id");
	char s[RBUFSIZE];
	int id;

	if (!id_val)
		return false;
	id = json_integer_value(json_object_get(val, "id"));

	sprintf(s, "{\"id\": %d, \"result\": \""PACKAGE"/"VERSION""STRATUM_USER_AGENT"\", \"error\": null}", id);
	if (!stratum_send(pool, s, strlen(s)))
		return false;

	return true;
}

static bool send_pong(struct pool *pool, json_t *val)
{
	json_t *id_val = json_object_get(val, "id");
	char s[RBUFSIZE];
	int id;

	if (!id_val)
		return false;
	id = json_integer_value(json_object_get(val, "id"));

	sprintf(s, "{\"id\": %d, \"result\": \"pong\", \"error\": null}", id);
	if (!stratum_send(pool, s, strlen(s)))
		return false;

	return true;
}

static bool show_message(struct pool *pool, json_t *val)
{
	char *msg;

	if (!json_is_array(val))
		return false;
	msg = (char *)json_string_value(json_array_get(val, 0));
	if (!msg)
		return false;
	applog(LOG_NOTICE, "Pool %d message: %s", pool->pool_no, msg);
	return true;
}

static bool parse_vmask(struct pool *pool, json_t *params)
{
	bool ret = false;

	if (!params) {
		applog(LOG_INFO, "No params with parse_vmask given for pool %d",
		       pool->pool_no);
		goto out;
	}
	if (json_is_array(params))
		params = json_array_get(params, 0);
	if (!json_is_string(params) || !json_string_length(params)) {
		applog(LOG_INFO, "Params invalid string for parse_vmask for pool %d",
		       pool->pool_no);
		goto out;
	}
	pool->vmask = set_vmask(pool, params);
	ret = true;
out:
	return ret;
}

bool parse_method(struct pool *pool, char *s)
{
	json_t *val = NULL, *method, *err_val, *params;
	json_error_t err;
	bool ret = false;
	char *buf;

	if (!s)
		goto out;

	val = JSON_LOADS(s, &err);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
		goto out;
	}

	method = json_object_get(val, "method");
	if (!method)
		goto out_decref;
	err_val = json_object_get(val, "error");
	params = json_object_get(val, "params");

	if (err_val && !json_is_null(err_val)) {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");

		applog(LOG_INFO, "JSON-RPC method decode of %s failed: %s", s, ss);
		free(ss);
		goto out_decref;
	}

	buf = (char *)json_string_value(method);
	if (!buf)
		goto out_decref;

	if (!strncasecmp(buf, "mining.notify", 13)) {
		if (parse_notify(pool, params))
			pool->stratum_notify = ret = true;
		else
			pool->stratum_notify = ret = false;
		goto out_decref;
	}

	if (!strncasecmp(buf, "mining.set_difficulty", 21)) {
		ret = parse_diff(pool, params);
		goto out_decref;
	}
#ifdef USE_XTRANONCE
	if (!strncasecmp(buf, "mining.set_extranonce", 21)) {
		ret = parse_extranonce(pool, params);
		goto out_decref;
	}
#endif
	if (!strncasecmp(buf, "client.reconnect", 16)) {
		ret = parse_reconnect(pool, params);
		goto out_decref;
	}

	if (!strncasecmp(buf, "client.get_version", 18)) {
		ret =  send_version(pool, val);
		goto out_decref;
	}

	if (!strncasecmp(buf, "client.show_message", 19)) {
		ret = show_message(pool, params);
		goto out_decref;
	}

	if (!strncasecmp(buf, "mining.ping", 11)) {
		applog(LOG_INFO, "Pool %d ping", pool->pool_no);
		ret = send_pong(pool, val);
		goto out_decref;
	}

	if (!strncasecmp(buf, "mining.set_version_mask", 23)) {
		ret = parse_vmask(pool, params);
		goto out_decref;
	}
	applog(LOG_INFO, "Unknown JSON-RPC from pool %d: %s", pool->pool_no, s);
out_decref:
	json_decref(val);
out:
	return ret;
}
#ifdef USE_XTRANONCE
bool subscribe_extranonce(struct pool *pool)
{
	json_t *val = NULL, *res_val, *err_val;
	char s[RBUFSIZE], *sret = NULL;
	json_error_t err;
	bool ret = false;

	sprintf(s, "{\"id\": %d, \"method\": \"mining.extranonce.subscribe\", \"params\": []}",
		swork_id++);

	if (!stratum_send(pool, s, strlen(s)))
		return ret;

	/* Parse all data in the queue and anything left should be the response */
	while (42) {
		if (!socket_full(pool, DEFAULT_SOCKWAIT / 30)) {
			applog(LOG_DEBUG, "Timed out waiting for response extranonce.subscribe");
			/* some pool doesnt send anything, so this is normal */
			ret = true;
			goto out;
		}

		sret = recv_line(pool);
		if (!sret)
			return ret;
		if (parse_method(pool, sret))
			free(sret);
		else
			break;
	}

	val = JSON_LOADS(sret, &err);
	free(sret);
	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");

	if (!res_val || json_is_false(res_val) || (err_val && !json_is_null(err_val)))  {
		char *ss;

		if (err_val) {
			ss = __json_array_string(err_val, 1);
			if (!ss)
				ss = (char *)json_string_value(err_val);
			if (ss && (strcmp(ss, "Method 'subscribe' not found for service 'mining.extranonce'") == 0)) {
				applog(LOG_INFO, "Cannot subscribe to mining.extranonce for pool %d", pool->pool_no);
				ret = true;
				goto out;
			}
			if (ss && (strcmp(ss, "Unrecognized request provided") == 0)) {
				applog(LOG_INFO, "Cannot subscribe to mining.extranonce for pool %d", pool->pool_no);
				ret = true;
				goto out;
			}
			ss = json_dumps(err_val, JSON_INDENT(3));
		}
		else
			ss = strdup("(unknown reason)");
		applog(LOG_INFO, "Pool %d JSON extranonce subscribe failed: %s", pool->pool_no, ss);
		free(ss);

		goto out;
	}

	ret = true;
	applog(LOG_INFO, "Stratum extranonce subscribe for pool %d", pool->pool_no);

out:
	json_decref(val);
	return ret;
}
#endif

bool auth_stratum(struct pool *pool)
{
	json_t *val = NULL, *res_val, *err_val;
	char s[RBUFSIZE], *sret = NULL;
	json_error_t err;
	bool ret = false;

	sprintf(s, "{\"id\": %d, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}",
		swork_id++, pool->rpc_user, pool->rpc_pass);

	if (!stratum_send(pool, s, strlen(s)))
		return ret;

	/* Parse all data in the queue and anything left should be auth */
	while (42) {
		sret = recv_line(pool);
		if (!sret)
			return ret;
		if (parse_method(pool, sret))
			free(sret);
		else
			break;
	}

	val = JSON_LOADS(sret, &err);
	free(sret);
	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");

	if (!res_val || json_is_false(res_val) || (err_val && !json_is_null(err_val)))  {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");
		applog(LOG_INFO, "pool %d JSON stratum auth failed: %s", pool->pool_no, ss);
		free(ss);

		suspend_stratum(pool);

		goto out;
	}

	ret = true;
	applog(LOG_INFO, "Stratum authorisation success for pool %d", pool->pool_no);
	pool->probed = true;
	successful_connect = true;

	if (opt_suggest_diff) {
		sprintf(s, "{\"id\": %d, \"method\": \"mining.suggest_difficulty\", \"params\": [%d]}",
			swork_id++, opt_suggest_diff);
		stratum_send(pool, s, strlen(s));
	}
out:
	json_decref(val);
	return ret;
}

static int recv_byte(int sockd)
{
	char c;

	if (recv(sockd, &c, 1, 0) != -1)
		return c;

	return -1;
}

static bool http_negotiate(struct pool *pool, int sockd, bool http0)
{
	char buf[1024];
	int i, len;

	if (http0) {
		snprintf(buf, 1024, "CONNECT %s:%s HTTP/1.0\r\n\r\n",
			pool->sockaddr_url, pool->stratum_port);
	} else {
		snprintf(buf, 1024, "CONNECT %s:%s HTTP/1.1\r\nHost: %s:%s\r\n\r\n",
			pool->sockaddr_url, pool->stratum_port, pool->sockaddr_url,
			pool->stratum_port);
	}
	applog(LOG_DEBUG, "Sending proxy %s:%s - %s",
		pool->sockaddr_proxy_url, pool->sockaddr_proxy_port, buf);
	send(sockd, buf, strlen(buf), 0);
	len = recv(sockd, buf, 12, 0);
	if (len <= 0) {
		applog(LOG_WARNING, "Couldn't read from proxy %s:%s after sending CONNECT",
		       pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
		return false;
	}
	buf[len] = '\0';
	applog(LOG_DEBUG, "Received from proxy %s:%s - %s",
	       pool->sockaddr_proxy_url, pool->sockaddr_proxy_port, buf);
	if (strcmp(buf, "HTTP/1.1 200") && strcmp(buf, "HTTP/1.0 200")) {
		applog(LOG_WARNING, "HTTP Error from proxy %s:%s - %s",
		       pool->sockaddr_proxy_url, pool->sockaddr_proxy_port, buf);
		return false;
	}

	/* Ignore unwanted headers till we get desired response */
	for (i = 0; i < 4; i++) {
		buf[i] = recv_byte(sockd);
		if (buf[i] == (char)-1) {
			applog(LOG_WARNING, "Couldn't read HTTP byte from proxy %s:%s",
			pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
			return false;
		}
	}
	while (strncmp(buf, "\r\n\r\n", 4)) {
		for (i = 0; i < 3; i++)
			buf[i] = buf[i + 1];
		buf[3] = recv_byte(sockd);
		if (buf[3] == (char)-1) {
			applog(LOG_WARNING, "Couldn't read HTTP byte from proxy %s:%s",
			pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
			return false;
		}
	}

	applog(LOG_DEBUG, "Success negotiating with %s:%s HTTP proxy",
	       pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
	return true;
}

static bool socks5_negotiate(struct pool *pool, int sockd)
{
	unsigned char atyp, uclen;
	unsigned short port;
	char buf[515];
	int i, len;

	buf[0] = 0x05;
	buf[1] = 0x01;
	buf[2] = 0x00;
	applog(LOG_DEBUG, "Attempting to negotiate with %s:%s SOCKS5 proxy",
	       pool->sockaddr_proxy_url, pool->sockaddr_proxy_port );
	send(sockd, buf, 3, 0);
	if (recv_byte(sockd) != 0x05 || recv_byte(sockd) != buf[2]) {
		applog(LOG_WARNING, "Bad response from %s:%s SOCKS5 server",
		       pool->sockaddr_proxy_url, pool->sockaddr_proxy_port );
		return false;
	}

	buf[0] = 0x05;
	buf[1] = 0x01;
	buf[2] = 0x00;
	buf[3] = 0x03;
	len = (strlen(pool->sockaddr_url));
	if (len > 255)
		len = 255;
	uclen = len;
	buf[4] = (uclen & 0xff);
	cg_memcpy(buf + 5, pool->sockaddr_url, len);
	port = atoi(pool->stratum_port);
	buf[5 + len] = (port >> 8);
	buf[6 + len] = (port & 0xff);
	send(sockd, buf, (7 + len), 0);
	if (recv_byte(sockd) != 0x05 || recv_byte(sockd) != 0x00) {
		applog(LOG_WARNING, "Bad response from %s:%s SOCKS5 server",
			pool->sockaddr_proxy_url, pool->sockaddr_proxy_port );
		return false;
	}

	recv_byte(sockd);
	atyp = recv_byte(sockd);
	if (atyp == 0x01) {
		for (i = 0; i < 4; i++)
			recv_byte(sockd);
	} else if (atyp == 0x03) {
		len = recv_byte(sockd);
		for (i = 0; i < len; i++)
			recv_byte(sockd);
	} else {
		applog(LOG_WARNING, "Bad response from %s:%s SOCKS5 server",
			pool->sockaddr_proxy_url, pool->sockaddr_proxy_port );
		return false;
	}
	for (i = 0; i < 2; i++)
		recv_byte(sockd);

	applog(LOG_DEBUG, "Success negotiating with %s:%s SOCKS5 proxy",
	       pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
	return true;
}

static bool socks4_negotiate(struct pool *pool, int sockd, bool socks4a)
{
	unsigned short port;
	in_addr_t inp;
	char buf[515];
	int i, len;

	buf[0] = 0x04;
	buf[1] = 0x01;
	port = atoi(pool->stratum_port);
	buf[2] = port >> 8;
	buf[3] = port & 0xff;
	sprintf(&buf[8], "CGMINER");

	/* See if we've been given an IP address directly to avoid needing to
	 * resolve it. */
	inp = inet_addr(pool->sockaddr_url);
	inp = ntohl(inp);
	if ((int)inp != -1)
		socks4a = false;
	else {
		/* Try to extract the IP address ourselves first */
		struct addrinfo servinfobase, *servinfo, hints;

		servinfo = &servinfobase;
		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family = AF_INET; /* IPV4 only */
		if (!getaddrinfo(pool->sockaddr_url, NULL, &hints, &servinfo)) {
			struct sockaddr_in *saddr_in = (struct sockaddr_in *)servinfo->ai_addr;

			inp = ntohl(saddr_in->sin_addr.s_addr);
			socks4a = false;
			freeaddrinfo(servinfo);
		}
	}

	if (!socks4a) {
		if ((int)inp == -1) {
			applog(LOG_WARNING, "Invalid IP address specified for socks4 proxy: %s",
			       pool->sockaddr_url);
			return false;
		}
		buf[4] = (inp >> 24) & 0xFF;
		buf[5] = (inp >> 16) & 0xFF;
		buf[6] = (inp >>  8) & 0xFF;
		buf[7] = (inp >>  0) & 0xFF;
		send(sockd, buf, 16, 0);
	} else {
		/* This appears to not be working but hopefully most will be
		 * able to resolve IP addresses themselves. */
		buf[4] = 0;
		buf[5] = 0;
		buf[6] = 0;
		buf[7] = 1;
		len = strlen(pool->sockaddr_url);
		if (len > 255)
			len = 255;
		cg_memcpy(&buf[16], pool->sockaddr_url, len);
		len += 16;
		buf[len++] = '\0';
		send(sockd, buf, len, 0);
	}

	if (recv_byte(sockd) != 0x00 || recv_byte(sockd) != 0x5a) {
		applog(LOG_WARNING, "Bad response from %s:%s SOCKS4 server",
		       pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
		return false;
	}

	for (i = 0; i < 6; i++)
		recv_byte(sockd);

	return true;
}

static void noblock_socket(SOCKETTYPE fd)
{
#ifndef WIN32
	int flags = fcntl(fd, F_GETFL, 0);

	fcntl(fd, F_SETFL, O_NONBLOCK | flags);
#else
	u_long flags = 1;

	ioctlsocket(fd, FIONBIO, &flags);
#endif
}

static void block_socket(SOCKETTYPE fd)
{
#ifndef WIN32
	int flags = fcntl(fd, F_GETFL, 0);

	fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#else
	u_long flags = 0;

	ioctlsocket(fd, FIONBIO, &flags);
#endif
}

static bool sock_connecting(void)
{
#ifndef WIN32
	return errno == EINPROGRESS;
#else
	return WSAGetLastError() == WSAEWOULDBLOCK;
#endif
}
static bool setup_stratum_socket(struct pool *pool)
{
	struct addrinfo *servinfo, hints, *p;
	char *sockaddr_url, *sockaddr_port;
	int sockd;

	mutex_lock(&pool->stratum_lock);
	pool->stratum_active = false;
	if (pool->sock)
		CLOSESOCKET(pool->sock);
	pool->sock = 0;
	mutex_unlock(&pool->stratum_lock);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (!pool->rpc_proxy && opt_socks_proxy) {
		pool->rpc_proxy = opt_socks_proxy;
		extract_sockaddr(pool->rpc_proxy, &pool->sockaddr_proxy_url, &pool->sockaddr_proxy_port);
		pool->rpc_proxytype = PROXY_SOCKS5;
	}

	if (pool->rpc_proxy) {
		sockaddr_url = pool->sockaddr_proxy_url;
		sockaddr_port = pool->sockaddr_proxy_port;
	} else {
		sockaddr_url = pool->sockaddr_url;
		sockaddr_port = pool->stratum_port;
	}
	if (getaddrinfo(sockaddr_url, sockaddr_port, &hints, &servinfo) != 0) {
		if (!pool->probed) {
			applog(LOG_WARNING, "Failed to resolve (?wrong URL) %s:%s",
			       sockaddr_url, sockaddr_port);
			pool->probed = true;
		} else {
			applog(LOG_INFO, "Failed to getaddrinfo for %s:%s",
			       sockaddr_url, sockaddr_port);
		}
		return false;
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		sockd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockd == -1) {
			applog(LOG_DEBUG, "Failed socket");
			continue;
		}

		/* Iterate non blocking over entries returned by getaddrinfo
		 * to cope with round robin DNS entries, finding the first one
		 * we can connect to quickly. */
		noblock_socket(sockd);
		if (connect(sockd, p->ai_addr, p->ai_addrlen) == -1) {
			struct timeval tv_timeout = {2, 0};
			int selret;
			fd_set rw;

			if (!sock_connecting()) {
				CLOSESOCKET(sockd);
				applog(LOG_DEBUG, "Failed sock connect");
				continue;
			}
retry:
			FD_ZERO(&rw);
			FD_SET(sockd, &rw);
			selret = select(sockd + 1, NULL, &rw, NULL, &tv_timeout);
			if  (selret > 0 && FD_ISSET(sockd, &rw)) {
				socklen_t len;
				int err, n;

				len = sizeof(err);
				n = getsockopt(sockd, SOL_SOCKET, SO_ERROR, (void *)&err, &len);
				if (!n && !err) {
					applog(LOG_DEBUG, "Succeeded delayed connect");
					block_socket(sockd);
					break;
				}
			}
			if (selret < 0 && interrupted())
				goto retry;
			CLOSESOCKET(sockd);
			applog(LOG_DEBUG, "Select timeout/failed connect");
			continue;
		}
		applog(LOG_WARNING, "Succeeded immediate connect");
		block_socket(sockd);

		break;
	}
	if (p == NULL) {
		applog(LOG_INFO, "Failed to connect to stratum on %s:%s",
		       sockaddr_url, sockaddr_port);
		freeaddrinfo(servinfo);
		return false;
	}
	freeaddrinfo(servinfo);

	if (pool->rpc_proxy) {
		switch (pool->rpc_proxytype) {
			case PROXY_HTTP_1_0:
				if (!http_negotiate(pool, sockd, true))
					return false;
				break;
			case PROXY_HTTP:
				if (!http_negotiate(pool, sockd, false))
					return false;
				break;
			case PROXY_SOCKS5:
			case PROXY_SOCKS5H:
				if (!socks5_negotiate(pool, sockd))
					return false;
				break;
			case PROXY_SOCKS4:
				if (!socks4_negotiate(pool, sockd, false))
					return false;
				break;
			case PROXY_SOCKS4A:
				if (!socks4_negotiate(pool, sockd, true))
					return false;
				break;
			default:
				applog(LOG_WARNING, "Unsupported proxy type for %s:%s",
				       pool->sockaddr_proxy_url, pool->sockaddr_proxy_port);
				return false;
				break;
		}
	}

	if (!pool->sockbuf) {
		pool->sockbuf = cgcalloc(RBUFSIZE, 1);
		pool->sockbuf_size = RBUFSIZE;
	}

	pool->sock = sockd;
	keep_sockalive(sockd);
	return true;
}

static char *get_sessionid(json_t *val)
{
	char *ret = NULL;
	json_t *arr_val;
	int arrsize, i;

	arr_val = json_array_get(val, 0);
	if (!arr_val || !json_is_array(arr_val))
		goto out;
	arrsize = json_array_size(arr_val);
	for (i = 0; i < arrsize; i++) {
		json_t *arr = json_array_get(arr_val, i);
		char *notify;

		if (!arr | !json_is_array(arr))
			break;
		notify = __json_array_string(arr, 0);
		if (!notify)
			continue;
		if (!strncasecmp(notify, "mining.notify", 13)) {
			ret = json_array_string(arr, 1);
			break;
		}
	}
out:
	return ret;
}

void suspend_stratum(struct pool *pool)
{
	applog(LOG_INFO, "Closing socket for stratum pool %d", pool->pool_no);

	mutex_lock(&pool->stratum_lock);
	__suspend_stratum(pool);
	mutex_unlock(&pool->stratum_lock);
}

bool initiate_stratum(struct pool *pool)
{
	bool ret = false, recvd = false, noresume = false, sockd = false;
	char s[RBUFSIZE], *sret = NULL, *nonce1, *sessionid, *tmp;
	json_t *val = NULL, *res_val, *err_val;
	json_error_t err;
	int n2size;

resend:
	if (!setup_stratum_socket(pool)) {
		sockd = false;
		goto out;
	}

	sockd = true;

	if (recvd) {
		/* Get rid of any crap lying around if we're resending */
		clear_sock(pool);
	}

	/* Attempt to configure stratum protocol feature set first. */
#ifdef USE_GEKKO
	configure_stratum_mining(pool);
	if (!pool->sock) {
		//repair damage done by configure_stratum_mining
		if (!setup_stratum_socket(pool)) {
			sockd = false;
			goto out;
		}

		sockd = true;

		if (recvd) {
			/* Get rid of any crap lying around if we're resending */
			clear_sock(pool);
		}
	}
#else
	if (!configure_stratum_mining(pool))
		goto out;
#endif

	if (recvd) {
		sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": []}", swork_id++);
	} else {
		if (pool->sessionid)
			sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": [\""PACKAGE"/"VERSION""STRATUM_USER_AGENT"\", \"%s\"]}", swork_id++, pool->sessionid);
		else
			sprintf(s, "{\"id\": %d, \"method\": \"mining.subscribe\", \"params\": [\""PACKAGE"/"VERSION""STRATUM_USER_AGENT"\"]}", swork_id++);
	}

	if (__stratum_send(pool, s, strlen(s)) != SEND_OK) {
		applog(LOG_DEBUG, "Failed to send s in initiate_stratum");
		goto out;
	}

	if (!socket_full(pool, DEFAULT_SOCKWAIT)) {
		applog(LOG_DEBUG, "Timed out waiting for response in initiate_stratum");
		goto out;
	}
rereceive:
	sret = recv_line(pool);
	if (!sret)
		goto out;

	recvd = true;

	val = JSON_LOADS(sret, &err);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
		goto out;
	}

	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");

	if (!res_val) {
		/* Check for a method just in case */
		json_t *method_val = json_object_get(val, "method");

		if (method_val && parse_method(pool, sret)) {
			free(sret);
			sret = NULL;
			goto rereceive;
		}
	}

	if (!res_val || json_is_null(res_val) ||
	    (err_val && !json_is_null(err_val))) {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");

		applog(LOG_INFO, "JSON-RPC decode of message %s failed: %s", sret, ss);

		free(ss);

		goto out;
	}

	sessionid = get_sessionid(res_val);
	if (!sessionid)
		applog(LOG_DEBUG, "Failed to get sessionid in initiate_stratum");
	nonce1 = json_array_string(res_val, 1);
	if (!valid_hex(nonce1)) {
		applog(LOG_INFO, "Failed to get valid nonce1 in initiate_stratum");
		free(sessionid);
		free(nonce1);
		goto out;
	}
	n2size = json_integer_value(json_array_get(res_val, 2));
	if (n2size < 2 || n2size > 16) {
		applog(LOG_INFO, "Failed to get valid n2size in initiate_stratum");
		free(sessionid);
		free(nonce1);
		goto out;
	}

	if (sessionid && pool->sessionid && !strcmp(sessionid, pool->sessionid)) {
		applog(LOG_NOTICE, "Pool %d successfully negotiated resume with the same session ID",
		       pool->pool_no);
	}

	cg_wlock(&pool->data_lock);
	tmp = pool->sessionid;
	pool->sessionid = sessionid;
	free(tmp);
	tmp = pool->nonce1;
	pool->nonce1 = nonce1;
	free(tmp);
	pool->n1_len = strlen(nonce1) / 2;
	free(pool->nonce1bin);
	pool->nonce1bin = cgcalloc(pool->n1_len, 1);
	hex2bin(pool->nonce1bin, pool->nonce1, pool->n1_len);
	pool->n2size = n2size;
	cg_wunlock(&pool->data_lock);

	if (sessionid)
		applog(LOG_DEBUG, "Pool %d stratum session id: %s", pool->pool_no, pool->sessionid);

	ret = true;
out:
	if (ret) {
		if (!pool->stratum_url)
			pool->stratum_url = pool->sockaddr_url;
		pool->stratum_active = true;
		pool->next_diff = pool->diff_after = 0;
		pool->sdiff = 1;
		if (opt_protocol) {
			applog(LOG_DEBUG, "Pool %d confirmed mining.subscribe with extranonce1 %s extran2size %d",
			       pool->pool_no, pool->nonce1, pool->n2size);
		}
	} else {
		if (recvd && !noresume) {
			/* Reset the sessionid used for stratum resuming in case the pool
			* does not support it, or does not know how to respond to the
			* presence of the sessionid parameter. */
			cg_wlock(&pool->data_lock);
			free(pool->sessionid);
			free(pool->nonce1);
			pool->sessionid = pool->nonce1 = NULL;
			cg_wunlock(&pool->data_lock);

			applog(LOG_DEBUG, "Failed to resume stratum, trying afresh");
			noresume = true;
			json_decref(val);
			goto resend;
		}
		applog(LOG_DEBUG, "Initiate stratum failed");
		if (sockd)
			suspend_stratum(pool);
	}

	json_decref(val);
	free(sret);
	return ret;
}

bool restart_stratum(struct pool *pool)
{
	bool ret = false;

	if (pool->stratum_active)
		suspend_stratum(pool);
	if (!initiate_stratum(pool))
#ifdef USE_XTRANONCE
		goto out;
	if (pool->extranonce_subscribe && !subscribe_extranonce(pool))
#endif
		goto out;
	if (!auth_stratum(pool))
		goto out;
	ret = true;
out:
	if (!ret)
		pool_died(pool);
	else
		stratum_resumed(pool);
	return ret;
}

void dev_error(struct cgpu_info *dev, enum dev_reason reason)
{
	dev->device_last_not_well = time(NULL);
	dev->device_not_well_reason = reason;

	switch (reason) {
		case REASON_THREAD_FAIL_INIT:
			dev->thread_fail_init_count++;
			break;
		case REASON_THREAD_ZERO_HASH:
			dev->thread_zero_hash_count++;
			break;
		case REASON_THREAD_FAIL_QUEUE:
			dev->thread_fail_queue_count++;
			break;
		case REASON_DEV_SICK_IDLE_60:
			dev->dev_sick_idle_60_count++;
			break;
		case REASON_DEV_DEAD_IDLE_600:
			dev->dev_dead_idle_600_count++;
			break;
		case REASON_DEV_NOSTART:
			dev->dev_nostart_count++;
			break;
		case REASON_DEV_OVER_HEAT:
			dev->dev_over_heat_count++;
			break;
		case REASON_DEV_THERMAL_CUTOFF:
			dev->dev_thermal_cutoff_count++;
			break;
		case REASON_DEV_COMMS_ERROR:
			dev->dev_comms_error_count++;
			break;
		case REASON_DEV_THROTTLE:
			dev->dev_throttle_count++;
			break;
	}
}

/* Realloc an existing string to fit an extra string s, appending s to it. */
void *realloc_strcat(char *ptr, char *s)
{
	size_t old = 0, len = strlen(s);
	char *ret;

	if (!len)
		return ptr;
	if (ptr)
		old = strlen(ptr);

	len += old + 1;
	ret = cgmalloc(len);

	if (ptr) {
		sprintf(ret, "%s%s", ptr, s);
		free(ptr);
	} else
		sprintf(ret, "%s", s);
	return ret;
}

/* Make a text readable version of a string using 0xNN for < ' ' or > '~'
 * Including 0x00 at the end
 * You must free the result yourself */
void *str_text(char *ptr)
{
	unsigned char *uptr;
	char *ret, *txt;

	if (ptr == NULL) {
		ret = strdup("(null)");

		if (unlikely(!ret))
			quithere(1, "Failed to malloc null");
	}

	uptr = (unsigned char *)ptr;

	ret = txt = cgmalloc(strlen(ptr) * 4 + 5); // Guaranteed >= needed

	do {
		if (*uptr < ' ' || *uptr > '~') {
			sprintf(txt, "0x%02x", *uptr);
			txt += 4;
		} else
			*(txt++) = *uptr;
	} while (*(uptr++));

	*txt = '\0';

	return ret;
}

void RenameThread(const char* name)
{
	char buf[16];

	snprintf(buf, sizeof(buf), "cg@%s", name);
#if defined(PR_SET_NAME)
	// Only the first 15 characters are used (16 - NUL terminator)
	prctl(PR_SET_NAME, buf, 0, 0, 0);
#elif (defined(__FreeBSD__) || defined(__OpenBSD__))
	pthread_set_name_np(pthread_self(), buf);
#elif defined(MAC_OSX)
	pthread_setname_np(buf);
#else
	// Prevent warnings
	(void)buf;
#endif
}

/* cgminer specific wrappers for true unnamed semaphore usage on platforms
 * that support them and for apple which does not. We use a single byte across
 * a pipe to emulate semaphore behaviour there. */
#ifdef __APPLE__
void _cgsem_init(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
	int flags, fd, i;

	if (pipe(cgsem->pipefd) == -1)
		quitfrom(1, file, func, line, "Failed pipe errno=%d", errno);

	/* Make the pipes FD_CLOEXEC to allow them to close should we call
	 * execv on restart. */
	for (i = 0; i < 2; i++) {
		fd = cgsem->pipefd[i];
		flags = fcntl(fd, F_GETFD, 0);
		flags |= FD_CLOEXEC;
		if (fcntl(fd, F_SETFD, flags) == -1)
			quitfrom(1, file, func, line, "Failed to fcntl errno=%d", errno);
	}
}

void _cgsem_post(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
	const char buf = 1;
	int ret;

retry:
	ret = write(cgsem->pipefd[1], &buf, 1);
	if (unlikely(ret == 0))
		applog(LOG_WARNING, "Failed to write errno=%d" IN_FMT_FFL, errno, file, func, line);
	else if (unlikely(ret < 0 && interrupted()))
		goto retry;
}

void _cgsem_wait(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
	char buf;
	int ret;
retry:
	ret = read(cgsem->pipefd[0], &buf, 1);
	if (unlikely(ret == 0))
		applog(LOG_WARNING, "Failed to read errno=%d" IN_FMT_FFL, errno, file, func, line);
	else if (unlikely(ret < 0 && interrupted()))
		goto retry;
}

void cgsem_destroy(cgsem_t *cgsem)
{
	close(cgsem->pipefd[1]);
	close(cgsem->pipefd[0]);
}

/* This is similar to sem_timedwait but takes a millisecond value */
int _cgsem_mswait(cgsem_t *cgsem, int ms, const char *file, const char *func, const int line)
{
	struct timeval timeout;
	int ret, fd;
	fd_set rd;
	char buf;

retry:
	fd = cgsem->pipefd[0];
	FD_ZERO(&rd);
	FD_SET(fd, &rd);
	ms_to_timeval(&timeout, ms);
	ret = select(fd + 1, &rd, NULL, NULL, &timeout);

	if (ret > 0) {
		ret = read(fd, &buf, 1);
		return 0;
	}
	if (likely(!ret))
		return ETIMEDOUT;
	if (interrupted())
		goto retry;
	quitfrom(1, file, func, line, "Failed to sem_timedwait errno=%d cgsem=0x%p", errno, cgsem);
	/* We don't reach here */
	return 0;
}

/* Reset semaphore count back to zero */
void cgsem_reset(cgsem_t *cgsem)
{
	int ret, fd;
	fd_set rd;
	char buf;

	fd = cgsem->pipefd[0];
	FD_ZERO(&rd);
	FD_SET(fd, &rd);
	do {
		struct timeval timeout = {0, 0};

		ret = select(fd + 1, &rd, NULL, NULL, &timeout);
		if (ret > 0)
			ret = read(fd, &buf, 1);
		else if (unlikely(ret < 0 && interrupted()))
			ret = 1;
	} while (ret > 0);
}
#else
void _cgsem_init(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
	int ret;
	if ((ret = sem_init(cgsem, 0, 0)))
		quitfrom(1, file, func, line, "Failed to sem_init ret=%d errno=%d", ret, errno);
}

void _cgsem_post(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
	if (unlikely(sem_post(cgsem)))
		quitfrom(1, file, func, line, "Failed to sem_post errno=%d cgsem=0x%p", errno, cgsem);
}

void _cgsem_wait(cgsem_t *cgsem, const char *file, const char *func, const int line)
{
retry:
	if (unlikely(sem_wait(cgsem))) {
		if (interrupted())
			goto retry;
		quitfrom(1, file, func, line, "Failed to sem_wait errno=%d cgsem=0x%p", errno, cgsem);
	}
}

int _cgsem_mswait(cgsem_t *cgsem, int ms, const char *file, const char *func, const int line)
{
	struct timespec abs_timeout, tdiff;
	int ret;

	cgcond_time(&abs_timeout);
	ms_to_timespec(&tdiff, ms);
	timeraddspec(&abs_timeout, &tdiff);
retry:
	ret = sem_timedwait(cgsem, &abs_timeout);

	if (ret) {
		if (likely(sock_timeout()))
			return ETIMEDOUT;
		if (interrupted())
			goto retry;
		quitfrom(1, file, func, line, "Failed to sem_timedwait errno=%d cgsem=0x%p", errno, cgsem);
	}
	return 0;
}

void cgsem_reset(cgsem_t *cgsem)
{
	int ret;

	do {
		ret = sem_trywait(cgsem);
		if (unlikely(ret < 0 && interrupted()))
			ret = 0;
	} while (!ret);
}

void cgsem_destroy(cgsem_t *cgsem)
{
	sem_destroy(cgsem);
}
#endif

/* Provide a completion_timeout helper function for unreliable functions that
 * may die due to driver issues etc that time out if the function fails and
 * can then reliably return. */
struct cg_completion {
	cgsem_t cgsem;
	void (*fn)(void *fnarg);
	void *fnarg;
};

void *completion_thread(void *arg)
{
	struct cg_completion *cgc = (struct cg_completion *)arg;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	cgc->fn(cgc->fnarg);
	cgsem_post(&cgc->cgsem);

	return NULL;
}

bool cg_completion_timeout(void *fn, void *fnarg, int timeout)
{
	struct cg_completion *cgc;
	pthread_t pthread;
	bool ret = false;

	cgc = cgmalloc(sizeof(struct cg_completion));
	cgsem_init(&cgc->cgsem);
	cgc->fn = fn;
	cgc->fnarg = fnarg;

	pthread_create(&pthread, NULL, completion_thread, (void *)cgc);

	ret = cgsem_mswait(&cgc->cgsem, timeout);
	if (!ret) {
		pthread_join(pthread, NULL);
		free(cgc);
	} else
		pthread_cancel(pthread);
	return !ret;
}

void _cg_memcpy(void *dest, const void *src, unsigned int n, const char *file, const char *func, const int line)
{
	if (unlikely(n < 1 || n > (1ul << 31))) {
		applog(LOG_ERR, "ERR: Asked to memcpy %u bytes from %s %s():%d",
		       n, file, func, line);
		return;
	}
	if (unlikely(!dest)) {
		applog(LOG_ERR, "ERR: Asked to memcpy %u bytes to NULL from %s %s():%d",
		       n, file, func, line);
		return;
	}
	if (unlikely(!src)) {
		applog(LOG_ERR, "ERR: Asked to memcpy %u bytes from NULL from %s %s():%d",
		       n, file, func, line);
		return;
	}
	memcpy(dest, src, n);
}
