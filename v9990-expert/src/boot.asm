; ASCII8 cartridge, bank 0. C runtime is copied into fast internal RAM.
    org 04000h
    db "AB"
    dw init
    dw 0,0,0,0,0,0
init:
    ; Let the disk system finish allocating its workspace before launch.
    call 0138h
    rrca
    rrca
    and 3
    ld c,a
    ld b,0
    ld hl,0FCC1h
    add hl,bc
    ld a,(hl)
    and 080h
    jr z,slot_ready
    ld hl,0FCC5h
    add hl,bc
    ld a,(hl)
    and 00Ch
    or 080h
slot_ready:
    or c
    ld (0FEDBh),a
    ld a,0F7h
    ld (0FEDAh),a
    ld hl,start
    ld (0FEDCh),hl
    ld a,0C9h
    ld (0FEDEh),a
    ret
start:
    di
    ld sp,0D800h
    xor a
    ld (06000h),a
    ; Use the same RAM slot as page 3. BIOS slot tables describe expansion.
    in a,(0A8h)
    rlca
    rlca
    and 3
    ld c,a
    ld b,0
    ld hl,0FCC1h
    add hl,bc
    ld a,(hl)
    and 080h
    jr z,primary_ram
    ld hl,0FCC5h
    add hl,bc
    ld a,(hl)
    and 0C0h
    rrca
    rrca
    rrca
    rrca
    or 080h
primary_ram:
    or c
    ld h,080h
    call 0024h
    di
    ; 18KiB runtime -> 8000..C7FF; page-3 data stays below disk workspace.
    ld a,1
    ld de,08000h
copy_bank:
    ld (06800h),a
    ld hl,06000h
    ld bc,02000h
    ldir
    inc a
    cp 3
    jr nz,copy_bank
    ld (06800h),a
    ld hl,06000h
    ld bc,00800h
    ldir
    ; Only call turbo R CPU switching BIOS when supported.
    ld a,(0002Dh)
    cp 3
    jr c,no_turbo
    ld a,082h
    call 0180h
no_turbo:
    di
    ld sp,0D800h
    jp ENTRY_POINT
    defs 02000h-($-04000h),0FFh
