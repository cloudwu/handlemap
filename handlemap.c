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

int
handlemap_list(struct handlemap *m, int n, handleid * result) {
	int i,t=0;
	rwlock_rlock(&m->lock);
	for (i=0;t < n && i<m->cap;i++) {
		struct handleslot *slot = &m->slot[i];
		if (slot->id == 0)
			continue;
		result[t] = slot->id;
		++t;
	}

	t=m->n;
	rwlock_runlock(&m->lock);
	return t;
}
