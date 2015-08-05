#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define ARRAY_SIZE   0xff

// sequence number - use this to coordinate memory operation orders
// without mutexes (so that coordination between threads doesn't
// trigger epoch expirations)
atomic_int       g_seq;
atomic_uintptr_t g_array;

// release the cacheline on the variable we're waiting for
static inline void spin() {
	__asm__ __volatile__("pause" : : : "memory");
}

void *
worker_main(void *arg) {
	char n = (char)(uintptr_t)arg;

	// wait for the main thread to store the dynamically allocated
	// array in g_array
	while (atomic_load_explicit(&g_seq, memory_order_acquire) != 1)
		spin();

	// get the location of the array, write to it, and free it.
	char *array = (char *)atomic_load(&g_array);
	for (size_t i = 0; i < ARRAY_SIZE; i++)
		array[i] = n;
	free(array);

	// inform the main thread that we have free'd the array
	atomic_store_explicit(&g_seq, 2, memory_order_release);

	// wait for main thread to perform a use-after-free before we exit
	while (atomic_load_explicit(&g_seq, memory_order_acquire) != 3)
		spin();

	return NULL;
}

int
main(int argc, const char *argv[]) {
	pthread_t worker;

	char *array = calloc(ARRAY_SIZE, sizeof(*array));
	if (!array)
		return -1;

	pthread_create(&worker, NULL, worker_main, (void *)0x11);

	// pass the array to the worker thread, and increase the
	// sequence number, allowing the worker to progress into the
	// 'write + free' phase.
	atomic_thread_fence(memory_order_seq_cst);
	atomic_store(&g_array, (uintptr_t)array);
	atomic_store(&g_seq, 1);

	// wait for the worker to free the array
	while (atomic_load_explicit(&g_seq, memory_order_acquire) != 2)
		spin();

	// at this point, the array has been free'd in the worker
	// thread - perform a use-after-free
	array[0] = 0;

	// tell the worker it can exit
	atomic_store(&g_seq, 3);

	// wait for the worker to exit
	pthread_join(worker, NULL);

	return 0;
}
