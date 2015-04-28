/*

Use following command to build the test:
	gcc handlemap.c test.c -lpthread

Or VC:
	cl handlemap.c test.c
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "handlemap.h"
#include "simplethread.h"

#ifdef _MSC_VER

#include <windows.h>

static void
usleep(int dummy) {
	Sleep(0);
}

#else

#include <pthread.h>
#include <unistd.h>

#endif

#define HANDLE_N 1000

static handleid pool[HANDLE_N];

static void
grab(struct handlemap *m, int thread) {
	int i;
	for (i=0;i<HANDLE_N;i++) {
		int r = rand() % (i+1);
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

static void
grab1(void *p) {
	grab(p, 1);
}

static void
grab2(void *p) {
	grab(p, 2);
}

static void
create(void *p) {
	struct handlemap *m = p;
	int i;
	for (i=0;i<HANDLE_N;i++) {
		pool[i] = handlemap_new(m, (void *)((intptr_t)i+1));
		printf("create %d id=%u\n",i,pool[i]);
		usleep(50);
		int r = rand() % (i+1);
		handleid id = pool[r];
		void *ptr = handlemap_release(m, id);
		if (ptr) {
			printf("release %d, id = %u, ptr = %p\n", r, id, ptr);
		} else {
			printf("release %d failed, id= %u\n", r,id);
		}
	}
}

static void
test(struct handlemap *m) {
	struct thread t[3] = {
		{ create, m },
		{ grab1, m },
		{ grab2, m },
	};

	thread_join(t,3);
}

int
main() {
	struct handlemap * m = handlemap_init();
	int i;
	test(m);
	handleid tmp[HANDLE_N];
	int n = handlemap_list(m, HANDLE_N, tmp);
	for (i=0;i<n;i++) {
		handleid id = tmp[i];
		void *ptr = handlemap_release(m, id);
		if (ptr) {
			printf("clear %d, id = %u, ptr = %p\n", i, id, ptr);
		}
	}

	handlemap_exit(m);

	return 0;
}
