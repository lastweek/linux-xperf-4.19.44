# Crossing Performance (xperf)

Author: Yizhou Shan <ys@purdue.edu>

## Purpose

We try to measure the user/kernel space crossing overhead.
By crossing, we meant the pure crossing overhead excluding all general
kernel assembly glue code.
In this repo, we use x86 page fault exception as our wheel to get that.

## Mechanism

In a high-level, the flow is:
  - User save TSC into stack
  - User pgfault
  - Cross to kernel, get TSC, and calculate latency

But devil is in the details, especially this low-level assembly code.
There are several difficulties:
  - Once in kernel, we need to save TSC without corrupting any other
	  registers and memory content. Any corruption leads to panic etc.
	  The challenge is to find somewhere to save stuff.
	  Options are: kernel stack, user stack, per-cpu. Using user stack
	  is dangerous, because we can't use safe probe in this assembly (i.e., copy_from/to_user()).
	  Using kernel stack is not flexible because we need to manually
	  find a spot above pt_regs, and this subject to number of `call` invoked.
  - We need to ensure the measuring only applied to measure program,
	  but not all user program. We let user save a MAGIC on user stack.

The approach:
  - `entry_64.S`: Save rax/rdx into kernel stack, because they are known to be good
	  if the exceptions came from user space.
  - `entry_64.S`: Save TSC into a per-cpu area. With swapgs surrounded.
  - `entry_64.S`: Restore rax/rdx
  - `fault.c`: calculate latency, print if MAGIC match

## Misc

- For VM scenario, the page fault entry point is `async_page_fault`, not the `page_fault`.

## Files changed

- `arch/x86/entry/entry_64.S`: assembly TSC code
- `arch/x86/mm/fault.c`: print
- `xperf/xperf.c`: userspace test code

## HOWTO Run

- Copy your current kernel's .config into this repo
- make oldconfig
- Disable `CONFIG_PAGE_TABLE_ISOLATION`
- Compile kernel and install, reboot
- Run `xperf/xperf.c`
- Check dmesg

## Some Numbers

CPU: Xeon E5-v3, @2.4GHz

|Type|Cycles|
|---| ---|
| VM| ~800|
|Bare-metal| ~500|
