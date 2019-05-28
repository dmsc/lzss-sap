;
; LZSS Compressed SAP player for 12bit match lengths
; --------------------------------------------------
;
; This player uses:
;  Match length: 5 bits  (2 to 33)
;  Match offset: 7 bits  (1 to 128)
;  Min length: 2
;  Total match bits: 12 bits
;
; Compress using:
;  lzss -b 12 -o 7 input.rsap test.lz12
;
    org $80

chn_copy    .ds     9
chn_pos     .ds     9
bptr        .ds     2
cur_pos     .ds     1

bit_data    .byte   1
nib_data    .byte   1

get_byte
    lda song_data+1
    inc get_byte+1
    bne skip
    inc get_byte+2
skip
    rts


POKEY = $D200

    org $2000
buffers
    .ds 128 * 8

song_data
        ins     'test.lz12'
song_end


start
    ; Y is current position in buffer - init to 0
    ldy #0

    ldx #9
clear
    lsr song_data
    tya
    ror                 ; A = 0 or 128
    sta chn_copy-1, x
    bpl chn_no_skip
    ; Skip this channel, read just init value
    jsr get_byte
chn_no_skip
    sta POKEY-1, x
    dex
    bne clear

sap_loop:
    ldx #0
    stx bptr
    lda #>buffers
    sta bptr+1

    ; Loop through all "channels", one for each POKEY register
chn_loop:
    lda chn_copy, x    ; Get status of this stream
    bmi skip_chn       ; Negative - skip this channel
    bne do_copy_byte   ; If > 0 we are copying bytes

    ; We are decoding a new match/literal
    lsr bit_data       ; Get next bit
    bne got_bit
    jsr get_byte       ; Not enough bits, refill!
    ror                ; Extract a new bit and add a 1 at the high bit (from C set above)
    sta bit_data       ;
got_bit:
    jsr get_byte       ; Always read a byte, it could mean "match size/offset" or "literal byte"
    bcs store          ; Bit = 1 is "literal", bit = 0 is "match"

    lsr                ; Bits 1-7 are the position
    sta chn_pos, x     ; Store

    lda nib_data
    rol                ; Insert last bit into A
    bcs ok

    tay                ; Store last bit into Y

    jsr get_byte

    pha
    lsr
    lsr
    lsr
    lsr
    ora #$80
    sta nib_data
    tya
    ror
    pla
    rol
    jmp skip_nd
ok:
    sta nib_data       ; Clear nib_data for next round
skip_nd:
    and #$1F
    clc
    adc #$2            ; Adds 2 to match length (C = 0 from above)
    sta chn_copy, x    ; Store in "copy length"

                        ; And start copying first byte
do_copy_byte:
    dec chn_copy, x     ; Decrease match length, increase match position
    ldy chn_pos, x
    iny
    bpl oky
    ldy #0
oky sty chn_pos, x

    ; Now, read old data, jump to data store
    lda (bptr), y

store:
    ldy cur_pos
    sta POKEY, x        ; Store to output and buffer
    sta (bptr), y

    ; Increment channel buffer pointer
    lda bptr
    clc
    adc #$80
    sta bptr
    bcc skip_chn
    inc bptr+1

skip_chn:
    inx
    cpx #$09
    bne chn_loop        ; Next channel

    iny
    bpl okpos
    ldy #0
okpos
    sty cur_pos

    lda 20
delay
    cmp 20
    beq delay

    lda get_byte + 2
    cmp #>song_end
    bne sap_loop
    lda get_byte + 1
    cmp #<song_end
    bne sap_loop

end_loop
    rts


    run start

