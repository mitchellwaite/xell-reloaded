#define CACHELINE_SIZE 128
#define CACHELINE_SIZE_LOG2 7

/*
 * Each loop touches ceil((addr & 127 + len) / 128) lines. It used to
 * branch with bge, which touched one line past the end of the range.
 */

.globl dcache_flush
dcache_flush:
		/* round down address */
	li %r5, CACHELINE_SIZE - 1
	andc %r5, %r3, %r5
		/* increase size by alignment change */
	rldicl %r3, %r3, 0, 64-CACHELINE_SIZE_LOG2
	add %r4, %r4, %r3

1:
	dcbst 0, %r5
	addic %r5, %r5, CACHELINE_SIZE /* also clears carry */
	subic. %r4, %r4, CACHELINE_SIZE
	bgt 1b
	sync
	blr

.globl dcache_inv
dcache_inv:
		/* round down address */
	li %r5, CACHELINE_SIZE - 1
	andc %r5, %r3, %r5
		/* increase size by alignment change */
	rldicl %r3, %r3, 0, 64-CACHELINE_SIZE_LOG2
	add %r4, %r4, %r3

1:
	dcbf 0, %r5
	addic %r5, %r5, CACHELINE_SIZE /* also clears carry */
	subic. %r4, %r4, CACHELINE_SIZE
	bgt 1b
	sync
	blr

.globl flush_code
flush_code:
		/* round down address */
	li %r5, CACHELINE_SIZE - 1
	andc %r5, %r3, %r5
		/* increase size by alignment change */
	rldicl %r3, %r3, 0, 64-CACHELINE_SIZE_LOG2
	add %r4, %r4, %r3

1:
	dcbst 0, %r5
	icbi 0, %r5
	addic %r5, %r5, CACHELINE_SIZE /* also clears carry */
	subic. %r4, %r4, CACHELINE_SIZE
	bgt 1b
	sync
	isync
	blr