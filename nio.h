/* Network Implementation.
 *
 * This library is free software; you can redistribute it and/or modify
 */

#ifndef __NIO_H_
#define __NIO_H_

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------- define ----------------------------------- */

#define	NIO_OK 0
#define	NIO_ERR -1

#define	NIO_INV -1

#define	NIO_DISCONNECT 1

#define nio_close(_f) close(_f)

/* -------------------------------- api functions ---------------------------- */

int nio_tcp_connect(char *err, const char *addr, int port);
int nio_tcp_nonblock_connect(char *err, const char *addr, int port);
int nio_tcp_server(char *err, const char *addr, int port, int backlog);
int nio_tcp6_server(char *err, const char *addr, int port, int backlog);
int nio_tcp_accept(char *err, int fd, char *ip, size_t iplen, int *port);
int nio_tcp_read(char *err, int fd, char *buf, int count);
int nio_tcp_nonblock_read(char *err, int fd, char *buf, int count, int *len);
int nio_tcp_write(char *err, int fd, char *buf, int count);
int nio_tcp_nonblock_write(char *err, int fd, char *buf, int count);

int nio_enable_tcp_nonblock(char *err, int fd);
int nio_disable_tcp_nonblock(char *err, int fd);
int nio_enable_tcp_linger(char *err, int fd);
int nio_enable_tcp_reuseaddr(char *err, int fd);
int nio_disable_tcp_reuseaddr(char *err, int fd);
int nio_enable_tcp_nodelay(char *err, int fd);
int nio_disable_tcp_nodelay(char *err, int fd);
int nio_enable_tcp_keepalive(char *err, int fd);
int nio_disable_tcp_keepalive(char *err, int fd);
int nio_enable_keepalive(char *err, int fd, int interval);

int nio_set_send_buffer(char *err, int fd, int size);
int nio_set_recv_buffer(char *err, int fd, int size);

#ifdef __cplusplus
}
#endif

#endif /* __NIO_H_ */
