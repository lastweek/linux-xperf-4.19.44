# Crossing Performance (xperf)

Author: Yizhou Shan <ys@purdue.edu>

## Purpose

We try to measure the user/kernel space crossing overhead.
By crossing, we meant the pure crossing overhead excluding all general
kernel assembly glue code.
In this repo, we use x86 page fault exception as our wheel to get that.

## Mechanism

### User to kernel (u2k)

At a high-level, the flow is:
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

Enable/Disable: change `xperf_idtentry` back to `idtentry` for both `page_fault` and `async_page_fault`.

Note: u2k hack is safe because we don't probe user virtual address directly in assembly.
Userspace accessing is done via `copy_from_user()`.

### Kernel to user (k2u)

At a high-level, the flow is:
  - Kernel save TSC into user stack
  - Kernel IRET
  - Cross to user, get TSC, and calculate latency

This is relatively simpiler than measuring u2k because we can safely use kernel stack.
The approach:
  - Save scratch %rax, %rdx, %rcx into kernel stack
  - Check if MAGIC match
  - rdtsc
  - save to user stack
  - restore scratch registers

Enable/Disable: There is a `xperf_return_kernel_tsc` code block at `entry_64.S`.

Note: k2u hack is NOT safe because we probe user virtual address directly in assembly,
i.e., `movq    %rax, (%rcx)` in our hack. During my experiments, sometimes it will crash,
but not always.

## Misc

- For VM scenario, the page fault entry point is `async_page_fault`, not the `page_fault`.

## Files changed

- `arch/x86/entry/entry_64.S`: assembly TSC code
- `arch/x86/mm/fault.c`: print
- `xperf/xperf.c`: userspace test code

## HOWTO Run

FAT NOTE:
- Enabling k2u code might bring crash
- It's not safe to disable KPTI
- Switch back to normal kernel after testing

Steps:
- Copy your current kernel's .config into this repo
- make oldconfig
- Disable `CONFIG_PAGE_TABLE_ISOLATION`
- Compile kernel and install, reboot
- Run `xperf/xperf.c` for k2u cycles
- Check dmesg for u2k cycles

## Numbers

CPU: Xeon E5-v3, @2.4GHz

|Type| U2K Cycles| K2U Cycles|
|---| ---|---|
| VM| ~800| TODO|
|Bare-metal| ~500| TODO|
