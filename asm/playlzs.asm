;
; LLZSS Compressed SAP player
; ---------------------------
;

    org $80

cur_chan    .ds     1
cur_pos     .ds     1
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
    .ds 9 * 16

    org $2100

song_data
        ins     'test.lzs'
song_end


start
    ldx #(bit_data - cur_chan - 1)
    lda #0
clear
    sta cur_chan, x
    dex
    bpl clear

sap_loop:
    ldx  #0

    ; Loop through all "channels", one for each POKEY register
chn_loop:
    txa                 ; Get channel buffer address, used to store old channel data
    asl
    asl
    asl
    asl
    sta cur_chan

    lda chn_copy, x    ; Get status of this stream
    bne do_copy_byte   ; If > 0 we are copying bytes

    ; We are decoding a new match/literal
    lsr bit_data       ; Get next bit
    bne got_bit
    jsr get_byte       ; Not enough bits, refill!
    ror                 ; Extract a new bit and add a 1 at the high bit (from C set above)
    sta bit_data       ;
got_bit:
    jsr get_byte       ; Always read a byte, it could mean "match size/offset" or "literal byte"
    bcs store          ; Bit = 1 is "literal", bit = 0 is "match"

    sta chn_pos, x     ; Store "position" (no need to AND #$0F, it will be done above)
    adc #$20           ; Adds 2 to match length (C = 0 from above)
    ror
    lsr
    lsr
    lsr
    sta  chn_copy, x    ; Store in "copy length"

                        ; And start copying first byte
do_copy_byte:
    dec chn_copy, x    ; Decrease match length, increase match position
    inc chn_pos, x
    lda chn_pos, x
    and #$0F
    ora cur_chan
    tay

    ; Now, read old data, jump to data store
    lda buffer, y

store:
    sta POKEY, x
    pha
    lda cur_pos        ; Store into buffer, get current position + channel into Y
    and #$0F
    ora cur_chan
    tay
    pla
    sta buffer, y

    inx
    cpx #$09
    bne chn_loop       ; Next channel

    inc cur_pos
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

