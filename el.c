/* Event Loop Implementation.
 *
 * This library is free software; you can redistribute it and/or modify
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <unistd.h>

#include "el.h"

/* -------------------------------- struct ----------------------------------- */

typedef struct epHandle {
	int fd;
	struct epoll_event *events;
} epHandle;

/* -------------------------------- define ----------------------------------- */

#define	EL_INV -1

#define EL_CLOSE(_f) \
	do { if(EL_INV != _f) { close(_f); _f = EL_INV; } } while(0)
#define EL_FREE(_p) \
	do { if(_p) { free(_p); _p = NULL; } } while(0)

/* -------------------------------- private ---------------------------------- */

static int _el_epoll_create(elHandle *el);
static void _el_epoll_destroy(elHandle *el);
static int _el_epoll_add(elHandle *el, int fd, int mask);
static void _el_epoll_del(elHandle *el, int fd, int mask);
static int _el_epoll(elHandle *el, struct timeval *ptv);

static void _el_file_clear(elHandle *el);

static void _el_time_get(long *sc, long *ms);
static void _el_time_add(long *sc, long *ms, long msc);
static void _el_time_clear(elHandle *el);
static void *_el_time_search(elHandle *el);
static int _el_time_process(elHandle *el);

static int _el_process(elHandle *el);

/* -------------------------------- private implementation ------------------- */

static int _el_epoll_create(elHandle *el) {
	epHandle *ep = calloc(1,sizeof(*ep));
	if( !ep )
		goto err;
	ep->events = calloc(el->size,sizeof(*ep->events));
	if( !ep->events )
		goto err;
	ep->fd = epoll_create(el->size);
	if( EL_ERR == ep->fd )
		goto err;
	el->data = ep;
	return EL_OK;
err:
	if( ep ) {
		EL_FREE(ep->events);
		EL_FREE(ep);
	}
	return EL_ERR;
}

static void _el_epoll_destroy(elHandle *el) {
	epHandle *ep = el->data;
	EL_CLOSE(ep->fd);
	EL_FREE(ep->events);
	EL_FREE(ep);
}

static int _el_epoll_add(elHandle *el, int fd, int mask) {
	epHandle *ep = el->data;
	struct epoll_event ee = {0};
	int op = EL_NONE == el->files[fd].mask ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

	el->files[fd].mask |= mask;

	if( EL_READABLE & el->files[fd].mask )
		ee.events |= EPOLLIN;
	if( EL_WRITABLE & el->files[fd].mask )
		ee.events |= EPOLLOUT;
	ee.data.fd = fd;

	if( EL_ERR == epoll_ctl(ep->fd,op,fd,&ee) )
		return EL_ERR;
	return EL_OK;
}

static void _el_epoll_del(elHandle *el, int fd, int mask) {
	epHandle *ep = el->data;
	struct epoll_event ee = {0};

	el->files[fd].mask &= ~mask;

	if( EL_READABLE & el->files[fd].mask )
		ee.events |= EPOLLIN;
	if( EL_WRITABLE & el->files[fd].mask )
		ee.events |= EPOLLOUT;
	ee.data.fd = fd;

	if( EL_NONE != el->files[fd].mask )
		epoll_ctl(ep->fd,EPOLL_CTL_MOD,fd,&ee);
	else
		epoll_ctl(ep->fd,EPOLL_CTL_DEL,fd,&ee);
}

static int _el_epoll(elHandle *el, struct timeval *ptv) {
	epHandle *ep = el->data;
	int numevents, i;

	numevents = epoll_wait(ep->fd,ep->events,el->size,
		ptv ? (ptv->tv_sec * 1000 + ptv->tv_usec / 1000) : -1);
	if( EL_ERR != numevents ) {
		for( i = 0; numevents > i; ++i ) {
			struct epoll_event *ee = ep->events + i;
			int mask = 0;

			if( EPOLLIN & ee->events )
				mask |= EL_READABLE;
			if( EPOLLOUT & ee->events )
				mask |= EL_WRITABLE;
			if( EPOLLERR & ee->events )
				mask |= EL_WRITABLE;
			if( EPOLLHUP & ee->events )
				mask |= EL_WRITABLE;

			el->trigs[i].fd = ee->data.fd;
			el->trigs[i].mask = mask;
		}
	}
	return numevents;
}

static void _el_file_clear(elHandle *el) {
	int i;
	for( i = 0; el->size > i; ++i ) {
		if( el->files[i].free_proc )
			el->files[i].free_proc(el,el->files[i].data);
	}
}

static void _el_time_get(long *sc, long *ms) {
	struct timeval tv;
	gettimeofday(&tv,NULL);
	*sc = tv.tv_sec;
	*ms = tv.tv_usec / 1000;
}

static void _el_time_add(long *sc, long *ms, long msc) {
	long cur_sc, cur_ms, now_sc, now_ms;

	_el_time_get(&cur_sc,&cur_ms);
	now_sc = cur_sc + msc / 1000;
	now_ms = cur_ms + msc % 1000;
	if( 1000 <= now_ms ) {
		now_sc++;
		now_ms -= 1000;
	}
	*sc = now_sc;
	*ms = now_ms;
}

static void _el_time_clear(elHandle *el) {
	elTimeEvent *te = el->times, *tp;
	while( te ) {
		tp = te;
		if( tp->free_proc )
			tp->free_proc(el,tp->data);
		EL_FREE(tp);
		te = te->next;
	}
}

static void *_el_time_search(elHandle *el) {
	elTimeEvent *te = el->times, *nearest = NULL;
	while( te ) {
		if( !nearest || te->sc < nearest->sc ||
			(te->sc == nearest->sc && te->ms < nearest->ms) )
			nearest = te;
		te = te->next;
	}
	return nearest;
}

static int _el_time_process(elHandle *el) {
	elTimeEvent *te = el->times, *tp = NULL;
	int processed = 0;

	while( te ) {
		long sc = 0, ms = 0;

		_el_time_get(&sc,&ms);

		if( sc > te->sc ||
			(sc == te->sc && ms >= te->ms) ) {
			if( te->prev )
				te->prev->next = te->next;
			else
				el->times = te->next;
			if( te->next )
				te->next->prev = te->prev;
			tp = te;
		}

		if( tp ) {
			if( tp->time_proc )
				tp->time_proc(el,tp->id,tp->data);
			if( tp->free_proc )
				tp->free_proc(el,tp->data);
			EL_FREE(tp);
			processed++;
		}
		te = te->next;
	}
	return processed;
}

static int _el_process(elHandle *el) {
	elTimeEvent *te;
	struct timeval tv, *ptv = NULL;
	int processed, i;

	te = _el_time_search(el);
	if( te ) {
		long sc, ms, tms;

		_el_time_get(&sc,&ms);

		tms = (te->sc - sc) * 1000 + te->ms - ms;
		if( 0 < tms ) {
			tv.tv_sec = tms / 1000;
			tv.tv_usec = (tms % 1000) * 1000;
		} else {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
		}
		ptv = &tv;
	} else {
		if( 0 < el->wait ) {
			tv.tv_sec = 0;
			tv.tv_usec = el->wait * 1000;
			ptv = &tv;
		}
	}

	processed = _el_epoll(el,ptv);
	for( i = 0; processed > i; ++i ) {
		elFileEvent *fe = &el->files[el->trigs[i].fd];
		int mask = el->trigs[i].mask;
		int fd = el->trigs[i].fd;

		if( EL_READABLE & mask & fe->mask )
			fe->rfile_proc(el,fd,fe->data,mask);
		if( EL_WRITABLE & mask & fe->mask )
			fe->wfile_proc(el,fd,fe->data,mask);
	}
	return processed += _el_time_process(el);
}

/* -------------------------------- api implementation ----------------------- */

elHandle *el_create(int size, long ms) {
	elHandle *el = calloc(1,sizeof(*el));
	if( !el )
		goto err;
	el->files = calloc(size,sizeof(*el->files));
	if( !el->files )
		goto err;
	el->trigs = calloc(size,sizeof(*el->trigs));
	if( !el->trigs )
		goto err;
	el->size = size;
	el->wait = ms;

	if( EL_OK == _el_epoll_create(el) )
		return el;
err:
	if( el ) {
		EL_FREE(el->trigs);
		EL_FREE(el->files);
		EL_FREE(el);
	}
	return NULL;
}

void el_destroy(elHandle *el) {
	_el_epoll_destroy(el);
	_el_time_clear(el);
	_el_file_clear(el);
	EL_FREE(el->trigs);
	EL_FREE(el->files);
	EL_FREE(el);
}

int el_file_add(elHandle *el, int fd, int mask,
		el_file_proc file_proc, void *data,
		el_free_proc free_proc) {
	if( el->size <= fd )
		return EL_ERR;
	if( EL_READABLE & mask )
		el->files[fd].rfile_proc = file_proc;
	if( EL_WRITABLE & mask )
		el->files[fd].wfile_proc = file_proc;
	if( EL_FREEABLE & mask )
		el->files[fd].free_proc = free_proc;
	el->files[fd].data = data;
	return _el_epoll_add(el,fd,mask);
}

void el_file_del(elHandle *el, int fd, int mask) {
	int fmask;
	if( el->size <= fd )
		return;
	fmask = el->files[fd].mask;
	_el_epoll_del(el,fd,mask);
	if( EL_FREEABLE & mask & fmask ) {
		el->files[fd].free_proc(el,el->files[fd].data);
		el->files[fd].free_proc = NULL;
	}
}

int el_file_get(elHandle *el, int fd) {
	if( el->size <= fd )
		return EL_NONE;
	return el->files[fd].mask;
}

long el_time_add(elHandle *el, long ms,
		el_time_proc time_proc, void *data,
		el_free_proc free_proc) {
	elTimeEvent *te = calloc(1,sizeof(*te));
	if( !te )
		return EL_ERR;

	el->num++;
	if( 0 > el->num )
		el->num = 1;

	_el_time_add(&te->sc,&te->ms,ms);

	te->id = el->num;
	te->time_proc = time_proc;
	te->free_proc = free_proc;
	te->data = data;

	if( !el->times ) {
		el->times = te;
		te->prev = te->next = NULL;
	} else {
		te->prev = NULL;
		te->next = el->times;
		el->times->prev = te;
		el->times = te;
	}
	return te->id;
}

void el_time_del(elHandle *el, long id) {
	elTimeEvent *te = el->times;
	while( te ) {
		if( te->id == id ) {
			if( te->prev )
				te->prev->next = te->next;
			else
				el->times = te->next;
			if( te->next )
				te->next->prev = te->prev;
			if( te->free_proc )
				te->free_proc(el,te->data);
			EL_FREE(te);
			return;
		}
		te = te->next;
	}
}

void el_main(elHandle *el) {
	while( !el->stop )
		_el_process(el);
}
