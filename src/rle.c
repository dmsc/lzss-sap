#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct bf
{
    int bit;
    int len;
    uint8_t buf[16];
};

static void init(struct bf *x)
{
    x->bit = 256;
    x->len = 0;
}

static void bflush(struct bf *x)
{
    putchar( x->bit >> 1 );
    if( x->len )
        fwrite(x->buf, x->len, 1, stdout);
    init(x);
}


static void add_bit(struct bf *x, int bit)
{
    if( x->bit & 1 )
        bflush(x);
    x->bit = (x->bit >> 1) | (bit ? 256 : 0);
}

static void add_byte(struct bf *x, int byte)
{
    x->buf[x->len] = byte;
    x->len ++;
}

int main()
{
    struct bf b;
    uint8_t buf[9], old[9];
    memset(old, 255, 9);
    init(&b);
    while( 1 == fread(buf, 9, 1, stdin) )
    {
        for(int i=0; i<9; i++)
        {
            if( buf[i] != old[i] )
            {
                old[i] = buf[i];
                add_bit(&b,1);
                add_byte(&b,buf[i]);
            }
            else
                add_bit(&b,0);
        }
    }
    bflush(&b);
}
