/* Network Implementation.
 *
 * This library is free software; you can redistribute it and/or modify
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "nio.h"

/* -------------------------------- define ----------------------------------- */

#define	NIO_ERR_LEN	256

#define NIO_CONNECT_NONE 0
#define NIO_CONNECT_NONBLOCK 1

/* -------------------------------- private ---------------------------------- */

static void _nio_error(char *err, const char *fmt, ...);

static int _nio_enable_tcp_v6only(char *err, int fd);
static int _nio_enable_tcp_nonblock(char *err, int fd, int enable);
static int _nio_enable_tcp_reuseaddr(char *err, int fd, int enable);
static int _nio_enable_tcp_nodelay(char *err, int fd, int enable);
static int _nio_enable_tcp_keepalive(char *err, int fd, int enable);

static int _nio_tcp_listen(char *err, int fd, struct sockaddr *sa, socklen_t len, int backlog);
static int _nio_tcp_generic_connect(char *err, const char *addr, int port, int flags);
static int _nio_tcp_generic_server(char *err, const char *addr, int port, int family, int backlog);
static int _nio_tcp_generic_accept(char *err, int fd, struct sockaddr *sa, socklen_t *len);

/* -------------------------------- private implementation ------------------- */

static void _nio_error(char *err, const char *fmt, ...) {
	va_list ap;
	if( !err )
		return;
	va_start(ap,fmt);
	vsnprintf(err,NIO_ERR_LEN-1,fmt,ap);
	va_end(ap);
}

static int _nio_enable_tcp_v6only(char *err, int fd) {
	int enable = 1;
	if( NIO_ERR == setsockopt(fd,IPPROTO_IPV6,IPV6_V6ONLY,&enable,sizeof(enable)) ) {
		_nio_error(err,"setsockopt: %s",strerror(errno));
		return NIO_ERR;
	}
	return NIO_OK;
}

static int _nio_enable_tcp_nonblock(char *err, int fd, int enable) {
	int flags;
	if( NIO_ERR == (flags = fcntl(fd,F_GETFL)) ) {
		_nio_error(err,"fcntl(F_GETFL): %s",strerror(errno));
		return NIO_ERR;
	}
	if( enable )
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;
	if( NIO_ERR == fcntl(fd,F_SETFL,flags) ) {
		_nio_error(err,"fcntl(F_SETFL,O_NONBLOCK): %s",strerror(errno));
		return NIO_ERR;
	}
	return NIO_OK;
}

static int _nio_enable_tcp_reuseaddr(char *err, int fd, int enable) {
	if( NIO_ERR == setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(enable)) ) {
		_nio_error(err,"setsockopt SO_REUSEADDR: %s",strerror(errno));
		return NIO_ERR;
	}
	return NIO_OK;
}

static int _nio_enable_tcp_nodelay(char *err, int fd, int enable) {
	if( NIO_ERR == setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&enable,sizeof(enable)) ) {
		_nio_error(err,"setsockopt TCP_NODELAY: %s",strerror(errno));
		return NIO_ERR;
	}
	return NIO_OK;
}

static int _nio_enable_tcp_keepalive(char *err, int fd, int enable) {
	if( NIO_ERR == setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,&enable,sizeof(enable)) ) {
		_nio_error(err,"setsockopt SO_KEEPALIVE: %s",strerror(errno));
		return NIO_ERR;
	}
	return NIO_OK;
}

static int _nio_tcp_listen(char *err, int fd, struct sockaddr *sa, socklen_t len, int backlog) {
	if( NIO_ERR == bind(fd,sa,len) ) {
		_nio_error(err,"bind: %s",strerror(errno));
		return NIO_ERR;
	}
	if( NIO_ERR == listen(fd,backlog) ) {
		_nio_error(err,"listen: %s",strerror(errno));
		return NIO_ERR;
	}
	return NIO_OK;
}

static int _nio_tcp_generic_connect(char *err, const char *addr, int port, int flags) {
	struct addrinfo hints, *serinfo, *p;
	char sport[6]; sport[0] = '\0';  /* strlen("65535") + 1; */
	int c = -1, r;

	snprintf(sport,sizeof(sport)-1,"%d",port);
	memset(&hints,0,sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if( (r = getaddrinfo(addr,sport,&hints,&serinfo)) ) {
		_nio_error(err,"%s",gai_strerror(r));
		return NIO_ERR;
	}

	for( p = serinfo; p; p = p->ai_next ) {
		if( NIO_ERR == (c = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) )
			continue;
		if( NIO_ERR == nio_enable_tcp_reuseaddr(err,c) )
			goto err;
		if( (NIO_CONNECT_NONBLOCK & flags) && NIO_ERR == nio_enable_tcp_nonblock(err,c) )
			goto err;
		if( NIO_ERR == connect(c,p->ai_addr,p->ai_addrlen) ) {
			if( (NIO_CONNECT_NONBLOCK & flags) && EINPROGRESS == errno )
				goto end;
			nio_close(c);
			continue;
		}
		goto end;
	}
	if( !p )
		_nio_error(err,"creating socket: %s",strerror(errno));
err:
	if( NIO_ERR != c )
		nio_close(c);
end:
	freeaddrinfo(serinfo);
	return c;
}

static int _nio_tcp_generic_server(char *err, const char *addr, int port, int family, int backlog) {
	struct addrinfo hints, *serinfo, *p;
	char sport[6]; sport[0] = '\0';  /* strlen("65535") */
	int s = -1, r;

	snprintf(sport,sizeof(sport)-1,"%d",port);
	memset(&hints,0,sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;    /* No effect if bindaddr != NULL */

	if( (r = getaddrinfo(addr,sport,&hints,&serinfo)) ) {
		_nio_error(err,"%s",gai_strerror(r));
		return NIO_ERR;
	}

	for( p = serinfo; p; p = p->ai_next ) {
		if( NIO_ERR == (s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) )
			continue;
		if( AF_INET6 == family && NIO_ERR == _nio_enable_tcp_v6only(err,s) )
			goto err;
		if( NIO_ERR == nio_enable_tcp_linger(err,s) )
			goto err;
		if( NIO_ERR == nio_enable_tcp_reuseaddr(err,s) )
			goto err;
		if( NIO_ERR == _nio_tcp_listen(err,s,p->ai_addr,p->ai_addrlen,backlog) )
			goto err;
        goto end;
	}
	if( !p )
		_nio_error(err,"unable to bind socket");
err:
	if( NIO_ERR != s )
		nio_close(s);
end:
	freeaddrinfo(serinfo);
	return s;
}

static int _nio_tcp_generic_accept(char *err, int fd, struct sockaddr *sa, socklen_t *len) {
	int c;
	while( 1 ) {
		c = accept(fd,sa,len);
		if( NIO_ERR == c ) {
			if( EINTR == errno ) {
				continue;
			} else {
				_nio_error(err,"accept: %s",strerror(errno));
				return NIO_ERR;
			}
		}
		break;
	}
	return c;
}

/* -------------------------------- api implementation ----------------------- */

int nio_tcp_connect(char *err, const char *addr, int port) {
	return _nio_tcp_generic_connect(err,addr,port,NIO_CONNECT_NONE);
}

int nio_tcp_nonblock_connect(char *err, const char *addr, int port) {
	return _nio_tcp_generic_connect(err,addr,port,NIO_CONNECT_NONBLOCK);
}

int nio_tcp_server(char *err, const char *addr, int port, int backlog) {
	return _nio_tcp_generic_server(err,addr,port,AF_INET,backlog);
}

int nio_tcp6_server(char *err, const char *addr, int port, int backlog) {
	return _nio_tcp_generic_server(err,addr,port,AF_INET6,backlog);
}

int nio_tcp_accept(char *err, int fd, char *ip, size_t iplen, int *port) {
	int c;
	struct sockaddr_storage sa;
	socklen_t salen = sizeof(sa);

	if( NIO_ERR == (c = _nio_tcp_generic_accept(err,fd,(struct sockaddr*)&sa,&salen)) )
		return NIO_ERR;

	if( AF_INET == sa.ss_family ) {
		struct sockaddr_in *s = (struct sockaddr_in *)&sa;
		if( ip )
			inet_ntop(AF_INET,(void*)&(s->sin_addr),ip,iplen);
		if( port )
			*port = ntohs(s->sin_port);
	} else {
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
		if( ip )
			inet_ntop(AF_INET6,(void*)&(s->sin6_addr),ip,iplen);
		if( port )
			*port = ntohs(s->sin6_port);
	}
	return c;
}

int nio_tcp_read(char *err, int fd, char *buf, int count) {
	int byte = 0, totlen = 0;

	while( totlen != count ) {
		byte = read(fd,buf+totlen,count-totlen);
		if( NIO_ERR == byte || !byte ) {
			_nio_error(err,"read: %s",strerror(errno));
			return NIO_ERR == byte ? byte : totlen;
		}
		totlen += byte;
		buf += byte;
	}
	return totlen;
}

int nio_tcp_nonblock_read(char *err, int fd, char *buf, int count, int *len) {
	int byte, result;

	while( 1 ) {
		byte = read(fd,buf+*len,count-*len);
		switch( byte ) {
		case NIO_ERR:
			switch( errno ) {
			case EAGAIN:
				if( count == *len ) {
					result = NIO_OK;
					goto end;
				}
			case EINTR:
				break;
			default:
				result = NIO_ERR;
				goto end;
			}
			byte = 0;
			break;
		case 0:
			result = NIO_DISCONNECT;
			goto end;
		}
		*len += byte;
	}
end:
	if( NIO_OK != result )
		_nio_error(err,"read: %s",strerror(errno));
	return result;
}

int nio_tcp_write(char *err, int fd, char *buf, int count) {
	int byte = 0, totlen = 0;

	while( totlen != count ) {
		byte = write(fd,buf+totlen,count-totlen);
		if( NIO_ERR == byte || !byte ) {
			_nio_error(err,"write: %s",strerror(errno));
			return NIO_ERR == byte ? byte : totlen;
		}
		totlen += byte;
		buf += byte;
	}
	return totlen;
}

int nio_tcp_nonblock_write(char *err, int fd, char *buf, int count) {
	int byte, result, totlen = 0;

	while( count > totlen ) {
		byte = write(fd,buf+totlen,count-totlen);
		switch( byte ) {
		case NIO_ERR:
			switch( errno ) {
			case EAGAIN:
			case EINTR:
				break;
			default:
				result = NIO_ERR;
				goto end;
			}
			byte = 0;
			break;
		case 0:
			result = NIO_DISCONNECT;
			goto end;
		}
		totlen += byte;
	}
	result = NIO_OK;
end:
	if( NIO_OK != result )
		_nio_error(err,"write: %s",strerror(errno));
	return result;
}

int nio_enable_tcp_nonblock(char *err, int fd) {
	return _nio_enable_tcp_nonblock(err,fd,1);
}

int nio_disable_tcp_nonblock(char *err, int fd) {
	return _nio_enable_tcp_nonblock(err,fd,0);
}

int nio_enable_tcp_linger(char *err, int fd) {
	struct linger l;
	l.l_onoff = 1;
	l.l_linger = 0;
	if( NIO_ERR == setsockopt(fd,SOL_SOCKET,SO_LINGER,&l,sizeof(l)) ) {
		_nio_error(err,"setsockopt SO_LINGER: %s",strerror(errno));
		return NIO_ERR;
	}
	return NIO_OK;
}

int nio_enable_tcp_reuseaddr(char *err, int fd) {
	return _nio_enable_tcp_reuseaddr(err,fd,1);
}

int nio_disable_tcp_reuseaddr(char *err, int fd) {
	return _nio_enable_tcp_reuseaddr(err,fd,0);
}

int nio_enable_tcp_nodelay(char *err, int fd) {
	return _nio_enable_tcp_nodelay(err,fd,1);
}

int nio_disable_tcp_nodelay(char *err, int fd) {
	return _nio_enable_tcp_nodelay(err,fd,0);
}

int nio_enable_tcp_keepalive(char *err, int fd) {
	return _nio_enable_tcp_keepalive(err,fd,1);
}

int nio_disable_tcp_keepalive(char *err, int fd) {
	return _nio_enable_tcp_keepalive(err,fd,0);
}

int nio_enable_keepalive(char *err, int fd, int interval) {
	int val = 1;
	if( NIO_ERR == setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,&val,sizeof(val)) ) {
		_nio_error(err,"setsockopt SO_KEEPALIVE: %s",strerror(errno));
		return NIO_ERR;
	}
#if defined(__linux__)
	val = interval;
	if( NIO_ERR == setsockopt(fd,IPPROTO_TCP,TCP_KEEPIDLE,&val,sizeof(val)) ) {
		_nio_error(err,"setsockopt TCP_KEEPIDLE: %s\n",strerror(errno));
		return NIO_ERR;
	}
	val = interval/3;
	if( 0 == val )
		val = 1;
	if( NIO_ERR == setsockopt(fd,IPPROTO_TCP,TCP_KEEPINTVL,&val,sizeof(val)) ) {
		_nio_error(err,"setsockopt TCP_KEEPINTVL: %s\n",strerror(errno));
		return NIO_ERR;
	}
	val = 3;
	if( NIO_ERR == setsockopt(fd,IPPROTO_TCP,TCP_KEEPCNT,&val,sizeof(val)) ) {
		_nio_error(err,"setsockopt TCP_KEEPCNT: %s\n",strerror(errno));
		return NIO_ERR;
	}
#else
    ((void)interval);
#endif
	return NIO_OK;
}

int nio_set_send_buffer(char *err, int fd, int size) {
	if( NIO_ERR == setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&size,sizeof(size)) ) {
		_nio_error(err,"setsockopt SO_SNDBUF: %s",strerror(errno));
		return NIO_ERR;
	}
	return NIO_OK;
}

int nio_set_recv_buffer(char *err, int fd, int size) {
	if( NIO_ERR == setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&size,sizeof(size)) ) {
		_nio_error(err,"setsockopt SO_RCVBUF: %s",strerror(errno));
		return NIO_ERR;
	}
	return NIO_OK;
}
