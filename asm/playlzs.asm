;
; LZSS Compressed SAP player for 8 match bits
; -------------------------------------------
;
; (c) 2020 DMSC
; Code under MIT license, see LICENSE file.
;
; This player uses:
;  Match length: 4 bits  (2 to 17)
;  Match offset: 4 bits  (1 to 16)
;  Min length: 2
;  Total match bits: 12 bits
;
; Compress using:
;  lzss -b 8 -o 4 input.rsap test.lz8
;
; Assemble this file with MADS assembler, the compressed song is expected in
; the `test.lz8` file at assembly time.
;
; The player needs 16 bytes of buffer for each pokey register stored, for a
; full SAP file this is 144 bytes.
;
    org $80

cur_pos     .ds     1
chn_copy    .ds     9
chn_pos     .ds     9

bit_data    .byte   1

get_byte
    lda song_data+1
    inc get_byte+1
    bne skip
    inc get_byte+2
skip
    rts


POKEY = $D200

    org $2000
buffer
    .ds 256

    org $2100

song_data
        ins     'test.lz8'
song_end


start
    ldy #0
    ldx #9
clear
    lsr song_data
    tya
    ror                 ; A = 0 or 128
    sta chn_copy-1, x
    jsr get_byte
    sta POKEY-1, x
    sta buffer + $F0, x
    dex
    bne clear

    ; Y is current position in buffer - init to pos 0 at channel 9
    ldy #$09
sap_loop:
    ldx #8

    ; Loop through all "channels", one for each POKEY register
chn_loop:
    sty cur_pos

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

    pha                ; Save A
    and #$0F
    adc #$2            ; Adds 2 to match length (C = 0 from above)
    sta chn_copy, x    ; Store in "copy length"

    pla                ; Restore A, get match position
    eor cur_pos
    and #$F0
    eor cur_pos        ; Add channel to position

    sta chn_pos, x     ; Store "position"

                        ; And start copying first byte
do_copy_byte:
    dec chn_copy, x     ; Decrease match length, increase match position
    lda chn_pos, x
    clc
    adc #$10
    sta chn_pos, x

    ; Now, read old data, jump to data store
    tay
    lda buffer, y
    ldy cur_pos

store:
    sta POKEY, x        ; Store to output and buffer
    sta buffer, y

skip_chn:
    dey
    dex
    bpl chn_loop        ; Next channel

    tya                 ; Increment buffer pos to channel 0 again
    clc
    adc #$19
    tay

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

