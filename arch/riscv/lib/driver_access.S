#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/csr.h>

	.altmacro
	.macro fixup op reg addr lbl
	LOCAL _epc
_epc:
	\op \reg, \addr
	.section __ex_table,"a"
	.balign RISCV_SZPTR
	RISCV_PTR _epc, \lbl
	.previous
	.endm

/*
	a0: driver register address
	a1: correct value
	a2: shift bits
	if register value is not equal a1
	return 0
*/
ENTRY(readl_fixup)

	fixup lw	t0, (a0), 2f
	fence	i, r
	srl	t0, t0, a2
	bne	t0, a1, error
	li	a0, 1
	ret

error:
	li	a0, 0
	ret

ENDPROC(readl_fixup)

	.section .fixup,"ax"
	.balign 4
	/* Fixup code for readl_fixup */
2:
	li	a0, 0
	ret
	.previous
