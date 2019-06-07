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
#define XPERF_RESERVED	0xdeadbeefbeefdead

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

#define NR_PAGES 1000000ULL
static unsigned long k2u_tsc[NR_PAGES];
static unsigned long u2k_tsc[NR_PAGES];

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
	unsigned long sp;
	unsigned long k2u_total, k2u_avg;
	unsigned long u2k_total, u2k_avg;
	unsigned long *u2k_u, *u2k_k, *k2u_k, *magic;
	unsigned long _u2k_u, _u2k_k, _k2u_k, _k2u_u;
	unsigned long DONT_TOUCHME_cushion[32];

	nr_size = NR_PAGES * PAGE_SIZE;
	foo = mmap(NULL, nr_size, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	if (!foo)
		die("fail to malloc");
	printf("INFO: mmap range: [%#lx - %#lx]\n", foo, foo + NR_PAGES * PAGE_SIZE);

	sp = current_stack_pointer();
	printf("INFO: rsp=%#lx coushion=%p\n", sp, DONT_TOUCHME_cushion);

	/*
	 * User stack:
	 *
	 *   | ..       |
	 *   | 8B magic | (filled by user)   +24
	 *   | 8B u2k_u | (filled by user)   +16
	 *   | 8B u2k_k | (filled by kernel) +8
	 *   | 8B k2u_k | (filled by kernel) <-- sp
	 */ 

	magic = (unsigned long *)(sp + 24);
	u2k_u = (unsigned long *)(sp + 16);
	u2k_k = (unsigned long *)(sp + 8);
	k2u_k = (unsigned long *)(sp);

	*magic = USER_KERNEL_CROSSING_PERF_MAGIC;
	*u2k_k = XPERF_RESERVED;
	*k2u_k = XPERF_RESERVED;

	for (i = 0; i < NR_PAGES; i++) {
		int *bar, cut;

		bar = foo + PAGE_SIZE * i;

		/*
		 * [U2K]
		 *
		 *          mfence
		 *          rdtsc	<- u2k_u
		 * (user)
		 * -------  pgfault  --------
		 * (kernel)
		 *          rdtsc	<- u2k_k
		 *          mfence
		 ***
		 * [K2U]
		 *          mfence
		 *          rdtsc	<- k2u_k
		 * (kernel)
		 * -------  IRET --------
		 * (user)
		 *          rdtsc	<- k2u_k
		 *          mfence
		 */

		/*
		 * Make sure rdtsc is not executed earlier,
		 * also inform compiler not to reorder.
		 */
		asm volatile("mfence": : :"memory");
		*u2k_u= rdtsc();
		asm volatile("": : :"memory");

		*bar = 0x12345678;

		/*
		 * The reserved spot is the TSC value right before IRET.
		 * Though there are ~6 instructions before IRET, should be fine.
		 * Please check retint_user at entry_64.S for details.
		 */
		asm volatile("": : :"memory");
		_k2u_u = rdtsc();
		asm volatile("mfence": : :"memory");

		_u2k_u = *u2k_u;
		_u2k_k = *u2k_k;
		_k2u_k = *k2u_k;

		u2k_tsc[i] = _u2k_k - _u2k_u;
		k2u_tsc[i] = _k2u_u - _k2u_k;

		if (0) {
			printf("u2k %18d - k2u %18d\n",  u2k_tsc[i], k2u_tsc[i]);
		}
	}

	for (i = 0, k2u_total = 0, u2k_total = 0; i < NR_PAGES; i++) {
		u2k_total += u2k_tsc[i];
		k2u_total += k2u_tsc[i];
	}
	u2k_avg = u2k_total/NR_PAGES;
	k2u_avg = k2u_total/NR_PAGES;

	printf("\nXPERF REPORT (Average of #%d run)\n"
	       "  [User to Kernel (u2k)] %10d (cycles)\n"
	       "  [Kernel to User (k2u)] %10d (cycles)\n",
		NR_PAGES, u2k_avg, k2u_avg);
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
