#define _GNU_SOURCE

#include <sys/utsname.h>
#include <math.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <linux/unistd.h>
#include <assert.h>
#include <sched.h>

#define USER_KERNEL_CROSSING_PERF_MAGIC	0x19940619

static inline void die(const char * str, ...)
{
	va_list args;
	va_start(args, str);
	vfprintf(stderr, str, args);
	fputc('\n', stderr);
	exit(1);
}

static int pin_cpu(int cpu_id)
{
	int ret;

	cpu_set_t cpu_set;
	CPU_ZERO(&cpu_set);
	CPU_SET(cpu_id, &cpu_set);

	ret = sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
	return ret;
}

static void getcpu(int *cpu, int *node)
{
	int ret;
	ret = syscall(SYS_getcpu, cpu, node, NULL);
}

#define PAGE_SIZE 4096

#define NR_PAGES 1000ULL

static __attribute__((always_inline)) inline unsigned long current_stack_pointer(void)
{
	unsigned long sp;
	asm volatile (
		"movq %%rsp, %0\n"
		: "=r" (sp)
	);
	return sp;
}

/**
 * rdtsc() - returns the current TSC without ordering constraints
 *
 * rdtsc() returns the result of RDTSC as a 64-bit integer.  The
 * only ordering constraint it supplies is the ordering implied by
 * "asm volatile": it will put the RDTSC in the place you expect.  The
 * CPU can and will speculatively execute that RDTSC, though, so the
 * results can be non-monotonic if compared on different CPUs.
 */
static __attribute__((always_inline)) inline unsigned long rdtsc(void)
{
	unsigned long low, high;
	asm volatile("rdtsc" : "=a" (low), "=d" (high));
	return ((low) | (high) << 32);
}

static int run(void)
{
	void *foo;
	long nr_size, i;
	struct timeval ts, te, result;
	unsigned long sp;
	unsigned long cushion[10];

	nr_size = NR_PAGES * PAGE_SIZE;
	foo = mmap(NULL, nr_size, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	if (!foo)
		die("fail to malloc");
	printf("Range: [%#lx - %#lx]\n", foo, foo + NR_PAGES * PAGE_SIZE);

	sp = current_stack_pointer();
	printf("%#lx %p\n", sp, cushion);

	/*
	 * User stack:
	 *
	 *   | ..            |
	 *   | 8B magic      |
	 *   | 8B user tsc   |
	 *   | 8B kernel tsc | <-- sp
	 */ 
	*(unsigned long *)(sp + 16) = USER_KERNEL_CROSSING_PERF_MAGIC;

	gettimeofday(&ts, NULL);
	for (i = 0; i < NR_PAGES; i++) {
		int *bar, cut;

		*(unsigned long *)(sp + 8) = rdtsc();
		asm volatile("": : :"memory");

		bar = foo + PAGE_SIZE * i;
		*bar = 100;

		printf("%d %ld %ld\n", i,
			*(unsigned long *)(sp + 8),
			rdtsc());
	}
	return 0;
}

int main(void)
{
	int i, cpu, node;

	pin_cpu(23);
	getcpu(&cpu, &node);
	printf("cpu: %d, node: %d\n", cpu, node);

	run();
}
