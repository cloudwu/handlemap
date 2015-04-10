/*
The MIT License (MIT)

Copyright (c) 2015 codingnow.com

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


Use following command to build the test:
	gcc -Wall -O2 -DHANDLEMAP_TEST handlemap.c -lpthread
*/

#include "handlemap.h"
#include "simplelock.h"

#include <stdlib.h>
#include <string.h>

// must be pow of 2
#define INIT_SLOTS 16

struct handleslot {
	handleid id;
	int ref;
	void * ud;
};

struct handlemap {
	handleid lastid;
	struct rwlock lock;
	int cap;
	int n;
	struct handleslot * slot;
};

struct handlemap * 
handlemap_init() {
	struct handlemap * m = (struct handlemap *)malloc(sizeof(*m));
	if (m == NULL)
		return NULL;
	m->lastid = 0;
	rwlock_init(&m->lock);
	m->cap = INIT_SLOTS;
	m->n = 0;
	m->slot = (struct handleslot *)calloc(m->cap, sizeof(*m->slot));
	if (m->slot == NULL) {
		free(m);
		return NULL;
	}
	return m;
}

void
handlemap_exit(struct handlemap *m) {
	if (m) {
		free(m->slot);
		free(m);
	}
}

static struct handlemap *
expand_map(struct handlemap *m) {
	int i,cap = m->cap;
	struct handleslot * nslot;
	nslot = (struct handleslot *)calloc(cap * 2, sizeof(*nslot));
	if (nslot == NULL) {
		return NULL;
	}
	for (i=0;i<cap;i++) {
		struct handleslot * os = &m->slot[i];
		struct handleslot * ns = &nslot[os->id & (cap * 2 -1)];
		*ns = *os;
	}
	free(m->slot);
	m->slot = nslot;
	m->cap = cap * 2;
	return m;
}

handleid
handlemap_new(struct handlemap *m, void *ud) {
	int i;
	if (ud == NULL)
		return 0;
	rwlock_wlock(&m->lock);
	if (m->n >= m->cap * 3 / 4) {
		if (expand_map(m) == NULL) {
			// memory overflow
			rwlock_wunlock(&m->lock);
			return 0;
		}
	}
	
	for (i=0;;i++) {
		struct handleslot *slot;
		handleid id = ++m->lastid;
		if (id == 0) {
			// 0 is reserved for invalid id
			id = ++m->lastid;
		}
		slot = &m->slot[id & (m->cap - 1)];
		if (slot->id)
			continue;
		slot->id = id;
		slot->ref = 1;
		slot->ud = ud;
		++m->n;

		rwlock_wunlock(&m->lock);
		return id;
	}
}

static void *
release_ref(struct handlemap *m, handleid id) {
	struct handleslot * slot;
	void * ud = NULL;
	if (id == 0)
		return NULL;
	rwlock_rlock(&m->lock);
	slot = &m->slot[id & (m->cap - 1)];
	if (slot->id != id) {
		rwlock_runlock(&m->lock);
		return NULL;
	}
	if (atom_dec(&slot->ref) <= 0) {
		ud = slot->ud;
	}
	rwlock_runlock(&m->lock);
	return ud;
}

static void *
try_delete(struct handlemap *m, handleid id) {
	struct handleslot * slot;
	void * ud;
	if (id == 0)
		return NULL;
	rwlock_wlock(&m->lock);
	slot = &m->slot[id & (m->cap - 1)];
	if (slot->id != id) {
		rwlock_wunlock(&m->lock);
		return NULL;
	}
	if (slot->ref > 0) {
		rwlock_wunlock(&m->lock);
		return NULL;
	}
	ud = slot->ud;
	slot->id = 0;
	--m->n;
	rwlock_wunlock(&m->lock);
	return ud;
}

void *
handlemap_grab(struct handlemap *m, handleid id) {
	struct handleslot * slot;
	void * ud;
	if (id == 0)
		return NULL;
	rwlock_rlock(&m->lock);
	slot = &m->slot[id & (m->cap - 1)];
	if (slot->id != id) {
		rwlock_runlock(&m->lock);
		return NULL;
	}
	atom_inc(&slot->ref);
	ud = slot->ud;
	rwlock_runlock(&m->lock);
	return ud;
}

void *
handlemap_release(struct handlemap *m, handleid id) {
	if (release_ref(m, id)) {
		return try_delete(m, id);
	} else {
		return NULL;
	}
}

#ifdef HANDLEMAP_TEST

#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#define HANDLE_N 1000

static handleid pool[HANDLE_N];

static void
grab(struct handlemap *m, int thread) {
	int i;
	for (i=0;i<HANDLE_N;i++) {
		int r = random() % (i+1);
		handleid id = pool[r];
		void *ptr = handlemap_grab(m, id);
		printf("thread %d: grab %d, id = %u, ptr = %p\n", thread, r, id, ptr);
		usleep(50);
		if (ptr) {
			ptr = handlemap_release(m, id);
			if (ptr) {
				printf("thread %d: release %d, id = %u, ptr = %p\n", thread, r, id, ptr);
			}
		}
	}
}

static void *
grab1(void *p) {
	grab(p, 1);
	return NULL;
}

static void *
grab2(void *p) {
	grab(p, 2);
	return NULL;
}

static void *
create(void *p) {
	struct handlemap *m = p;
	int i;
	for (i=0;i<HANDLE_N;i++) {
		pool[i] = handlemap_new(m, (void *)((intptr_t)i+1));
		printf("create %d id=%u\n",i,pool[i]);
		usleep(50);
		int r = random() % (i+1);
		handleid id = pool[r];
		void *ptr = handlemap_release(m, id);
		if (ptr) {
			printf("release %d, id = %u, ptr = %p\n", r, id, ptr);
		} else {
			printf("release %d failed, id= %u\n", r,id);
		}
	}
	for (i=0;i<HANDLE_N;i++) {
		handleid id = pool[i];
		void *ptr = handlemap_release(m, id);
		if (ptr) {
			printf("clear %d, id = %u, ptr = %p\n", i, id, ptr);
		}
	}
	return NULL;
}

static void
create_thread(pthread_t *thread, void *(*start_routine) (void *), void *arg) {
	if (pthread_create(thread,NULL, start_routine, arg)) {
		fprintf(stderr, "Create thread failed");
		exit(1);
	}
}

static void
test(struct handlemap *m) {
	int i;
	pthread_t pid[3];
	create_thread(&pid[0], create, m);
	create_thread(&pid[1], grab1, m);
	create_thread(&pid[2], grab2, m);

	for (i=0;i<3;i++) {
		pthread_join(pid[i], NULL); 
	}
}

int
main() {
	struct handlemap * m = handlemap_init();
	test(m);
	handlemap_exit(m);

	return 0;
}

#endif
