org 0x7C00

start:
  cli
  xor ax, ax
  mov ds, ax
  mov es, ax
  mov ss, ax
  mov sp, 0x7C00
  mov si, msg
  call print
  cli
  hlt

print:
  lodsb
  or al, al
  jz done
  mov ah, 0x0E
  int 0x10
  jmp print
done:
  ret

msg db "MyOS boot stub", 0
