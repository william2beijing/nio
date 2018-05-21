/* Event Loop Implementation.
 *
 * This library is free software; you can redistribute it and/or modify
 */

#ifndef __EL_H_
#define __EL_H_

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------- struct ----------------------------------- */

struct elHandle;

typedef void (*el_file_proc)(struct elHandle *el, int fd, void *data, int mask);
typedef void (*el_time_proc)(struct elHandle *el, long id, void *data);
typedef void (*el_free_proc)(struct elHandle *el, void *data);

typedef struct elFileEvent {
	int mask;
	el_file_proc rfile_proc;
	el_file_proc wfile_proc;
	el_free_proc free_proc;
	void *data;
} elFileEvent;

typedef struct elTimeEvent {
	long sc;
	long ms;
	long id;
	el_time_proc time_proc;
	el_free_proc free_proc;
	void *data;
	struct elTimeEvent *prev;
	struct elTimeEvent *next;
} elTimeEvent;

typedef struct elTrigEvent {
    int fd;
    int mask;
} elTrigEvent;

typedef struct elHandle {
	int size;
	int stop;
	long num;
	long wait;
	elFileEvent *files;
	elTrigEvent *trigs;
	elTimeEvent *times;
	void *data;
} elHandle;

/* -------------------------------- define ----------------------------------- */

#define EL_OK 0
#define EL_ERR -1

#define EL_NONE 0
#define EL_READABLE 1
#define EL_WRITABLE 2
#define EL_FREEABLE 4
#define EL_ALLABLE (EL_READABLE|EL_WRITABLE|EL_FREEABLE)

/* -------------------------------- api functions ---------------------------- */

elHandle *el_create(int size, long ms);
void el_destroy(elHandle *el);
int el_file_add(elHandle *el, int fd, int mask,
		el_file_proc file_proc, void *data,
		el_free_proc free_proc);
void el_file_del(elHandle *el, int fd, int mask);
int el_file_get(elHandle *el, int fd);
long el_time_add(elHandle *el, long ms,
		el_time_proc time_proc, void *data,
		el_free_proc free_proc);
void el_time_del(elHandle *el, long id);
void el_main(elHandle *el);

#ifdef __cplusplus
}
#endif

#endif /* __EL_H_ */
