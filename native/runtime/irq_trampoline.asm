; An interrupt taken between two instructions of the game's code (async_irq.c):
; the watcher thread points the suspended game thread here and leaves the
; interrupted instruction's address in khdays_irq_return. Every
; register, the flags and the x87/SSE state are kept, as the ARM9 banks and
; restores what an interrupt handler may touch.
.686
.xmm
.model flat

EXTERN _khdays_irq_async:PROC
EXTERN _khdays_irq_return:DWORD

.code
PUBLIC _khdays_irq_trampoline
_khdays_irq_trampoline PROC
    push _khdays_irq_return      ; where to return: the interrupted instruction
    pushfd
    pushad
    mov ebp, esp
    sub esp, 512
    and esp, 0FFFFFFF0h
    fxsave [esp]
    cld
    call _khdays_irq_async
    fxrstor [esp]
    mov esp, ebp
    popad
    popfd
    ret
_khdays_irq_trampoline ENDP

END
