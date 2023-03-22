#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/csr.h>
#include <asm/asm-extable.h>

	.macro fixup op reg addr lbl
100:
	\op \reg, \addr
	_asm_extable	100b, \lbl
	.endm

ENTRY(readl_fixup)
	fixup lw	t0, (a0), 2f
	fence	i,r
	srl	t0, t0, a2
	bne	t0, a1, 2f
	li	a0, 1
	ret
2:
	li	a0, 0
	ret
ENDPROC(readl_fixup)
