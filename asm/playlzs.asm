;
; LLZSS Compressed SAP player
; ---------------------------
;

    org $80

cur_chan    .ds     1
chn_copy    .ds     9
chn_pos     .ds     9

bit_data    .byte   1

get_byte
    lda song_data
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
        ins     'test.lzs'
song_end


start
    ldx #(bit_data - cur_chan - 1)
    ; Y is current position in buffer - init to 0
    ldy #0
clear
    sty cur_chan, x
    dex
    bpl clear

sap_loop:
    ldx  #0

    ; Loop through all "channels", one for each POKEY register
chn_loop:
    stx cur_chan

    lda chn_copy, x    ; Get status of this stream
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
    and #$F0
    ora cur_chan       ; Add channel to position

    sta chn_pos, x     ; Store "position"

                        ; And start copying first byte
do_copy_byte:
    dec chn_copy, x     ; Decrease match length, increase match position
    lda chn_pos, x
;    clc                ; NOTE: here C is always clear because the CPX at the end of the channel loop,
;                       ;       and the CMP at the end of the SAP loop
    adc #$10
    sta chn_pos, x

    ; Now, read old data, jump to data store
    tax
    lda buffer, x
    ldx cur_chan

store:
    sta POKEY, x        ; Store to output and buffer
    sta buffer, y

    iny
    inx
    cpx #$09
    bne chn_loop        ; Next channel

    tya                 ; Increment buffer pos to channel 0 again (9 + 6 + C = 16)
;   sec                 ; Here C = 1 from CPX above
    adc #$6
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

