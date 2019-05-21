#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////
// Bit encoding functions
struct bf
{
    int bit;
    int len;
    uint8_t buf[16];
    int total;
};

static void init(struct bf *x)
{
    x->total = 0;
    x->bit = 256;
    x->len = 0;
}

static void bflush(struct bf *x)
{
    putchar( x->bit >> 1 );
    if( x->len )
        fwrite(x->buf, x->len, 1, stdout);
    x->total += (1 + x->len);
    x->bit = 256;
    x->len = 0;
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

///////////////////////////////////////////////////////
// LZSS compression functions
static int max(int a, int b)
{
    return a>b ? a : b;
}

static int get_mlen(const uint8_t *a, const uint8_t *b, int max)
{
    for(int i=0; i<max; i++)
        if( a[i] != b[i] )
            return i;
    return max;
}

int hsh(const uint8_t *p)
{
    size_t x = (size_t)p;
    return 0xFF & (x ^ (x>>8) ^ (x>>16) ^ (x>>24));
}

// Statistics
static int stat_len[256+16];
static int stat_off[256+16];

static const int max_len = 17;   // Depends on encoding
static const int min_len =  2;
static const int max_off = 16;

static int match(const uint8_t *data, int pos, int size, int *mpos)
{
    int mxlen = -max(-max_len, pos - size);
    int mlen = 0;
    for(int i=max(pos-max_off,0); i<pos; i++)
    {
        int ml = get_mlen(data + pos, data + i, mxlen);
        if( ml >= mlen )
        {
            mlen = ml;
            *mpos = pos - i;
        }
    }
    return mlen;
}

static int lzcomp(struct bf *b, const uint8_t *data, int pos, int size, int lpos)
{
    if( pos <= lpos )
        return lpos;

    // Check if we have a match - and get max length of match
    int mpos = 0, xx = 0;
    int mlen = match(data, pos, size, &mpos);

#if 1   // TODO: does not seems to make file smaller!!
    // Try lazy matching now
    if( mlen >= 2 && match(data, pos + mlen, size, &xx) < min_len+1 )
    {
//    if(mlen >= 2)
//        fprintf(stderr,"(%02x) at %d, match %d\n", hsh(data), pos, mlen);
    int mk = -max(-mlen, -5);
    for(int k=1; k < mk && mlen >= 2 && mlen < max_len-1; k++)
    {
        int mp = 0;
        int ml = match(data, pos + k, size, &mp);
        if( ml > mlen + k )
        {
            // Found a better match
//            fprintf(stderr,"(%02x) at %d + %d, match %d better than %d\n",
//                    hsh(data), pos, k, ml, mlen);
            mlen = 0;
        }
    }
    }
#endif
    if( mlen < min_len )
    {
        // No match, just encode the byte
        add_bit(b,1);
        add_byte(b,data[pos]);
        stat_len[0] ++;
        return pos;
    }
    else
    {
        add_bit(b,0);
        add_byte(b,((pos-1-mpos) & 0x0F) + ((mlen - 2)<<4));
        stat_len[mlen] ++;
        stat_off[mpos] ++;
        return pos + mlen - 1;
    }
}

///////////////////////////////////////////////////////
int main()
{
    struct bf b;
    uint8_t buf[9], *data[9];
    char header_line[128];
    int lpos[9];

    // Max size of each bufer: 128k
    for(int i=0; i<9; i++)
    {
        data[i] = malloc(128*1024);
        lpos[i] = -1;
    }

    // Skip SAP header
    long pos = ftell(stdin);
    while( 0 != fgets(header_line, 80, stdin) )
    {
        size_t ln = strlen(header_line);
        if( ln < 1 || header_line[ln-1] != '\n' )
            break;
        pos = ftell(stdin);
    }

    fseek(stdin, pos, SEEK_SET);
    // Read all data
    int sz = 0;
    while( 1 == fread(buf, 9, 1, stdin) && sz < (128*1024) )
    {
        for(int i=0; i<9; i++)
        {
            // Simplify patterns - rewrite silence as 0
            if( (i & 1) == 1 && 0 == (buf[i] & 0x0f) )
                buf[i] = 0;
            data[i][sz] = buf[i];
        }
        sz++;
    }

    // Compress
    init(&b);
    for(int pos = 0; pos < sz; pos++)
    {
        for(int i=0; i<9; i++)
            lpos[i] = lzcomp(&b, data[i], pos, sz, lpos[i]);
    }
    bflush(&b);
    // Show stats
    fprintf(stderr,"Ratio: %d / %d = %.2f%%\n", b.total, 9*sz, (100.0*b.total) / (9.0*sz));
    fprintf(stderr,"value\t  POS\t  LEN\n");
    for(int i=0; i<=max(max_len,max_off); i++)
    {
        fprintf(stderr,"%2d\t%5d\t%5d\n", i, stat_off[i], stat_len[i]);
    }
    return 0;
}

