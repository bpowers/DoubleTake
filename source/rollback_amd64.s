.globl __dt_rollback_ctx
.type __dt_rollback_ctx,@function
__dt_rollback_ctx:
	push %rbp
	mov %rsp, %rbp

	// hold onto 4th arg (ucontext_t *)
	mov %rcx, %r8

	// musl's memcpy
	mov %rdi,%rax
	cmp $8,%rdx
	jc 1f
	test $7,%edi
	jz 1f
2:	movsb
	dec %rdx
	test $7,%edi
	jnz 2b
1:	mov %rdx,%rcx
	shr $3,%rcx
	rep
	movsq
	and $7,%edx
	jz 1f
2:	movsb
	dec %edx
	jnz 2b

	// call setcontext
1:	mov %r8, %rdi
	callq setcontext@PLT
