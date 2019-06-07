This is the assembly we expect:
```
  rdtsc  				# U2K user tsc

  shl    $0x20,%rdx
  or     %rdx,%rax
  mov    %rax,(%r14)			# Save U2K user tsc to stack

  movl   $0x12345678,(%r12)		# pgfault
  
  rdtsc  				# K2U user tsc
```
