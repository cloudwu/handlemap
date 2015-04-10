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

#ifdef _MSC_VER

#include <windows.h>

#define THREAD_FUNC DWORD WINAPI

static void
usleep(int dummy) {
	Sleep(0);
}

#else

#include <pthread.h>
#include <unistd.h>

#define THREAD_FUNC void *

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

static THREAD_FUNC
grab1(void *p) {
	grab(p, 1);
	return 0;
}

static THREAD_FUNC
grab2(void *p) {
	grab(p, 2);
	return 0;
}

static THREAD_FUNC
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
	for (i=0;i<HANDLE_N;i++) {
		handleid id = pool[i];
		void *ptr = handlemap_release(m, id);
		if (ptr) {
			printf("clear %d, id = %u, ptr = %p\n", i, id, ptr);
		}
	}
	return 0;
}

#ifdef _MSC_VER

static void
test(struct handlemap *m) {
	int i;
	HANDLE  hThreadArray[3];
	hThreadArray[0] = CreateThread(NULL, 0, create, m, 0, NULL);
	hThreadArray[1] = CreateThread(NULL, 0, grab1, m, 0, NULL);
	hThreadArray[2] = CreateThread(NULL, 0, grab2, m, 0, NULL);

	WaitForMultipleObjects(3, hThreadArray, TRUE, INFINITE);
}

#else

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

#endif

int
main() {
	struct handlemap * m = handlemap_init();
	test(m);
	handlemap_exit(m);

	return 0;
}
