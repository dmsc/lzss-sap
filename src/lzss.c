#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
void set_binary(void)
{
  setmode(fileno(stdout),O_BINARY);
  setmode(fileno(stdin),O_BINARY);
}
#else
void set_binary(void)
{
}
#endif


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
    while( (x->bit & 1) == 0 )
        x->bit >>= 1;

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

// Struct for LZ optimal parsing
struct lzop
{
    const uint8_t *data;// The data to compress
    int size;           // Data size
    int *bits;          // Number of bits needed to code from position
    int *mlen;          // Best match length at position (0 == no match);
    int *mpos;          // Best match offset at position
};

static void lzop_init(struct lzop *lz, const uint8_t *data, int size)
{
    lz->data = data;
    lz->size = size;
    lz->bits = calloc(sizeof(int), size);
    lz->mlen = calloc(sizeof(int), size);
    lz->mpos = calloc(sizeof(int), size);
}

// Returns maximal match length (and match position) at pos.
static int match(const uint8_t *data, int pos, int size, int *mpos)
{
    int mxlen = -max(-max_len, pos - size);
    int mlen = 0;
    for(int i=max(pos-max_off,0); i<pos; i++)
    {
        int ml = get_mlen(data + pos, data + i, mxlen);
        if( ml > mlen )
        {
            mlen = ml;
            *mpos = pos - i;
        }
    }
    return mlen;
}

static void lzop_backfill(struct lzop *lz)
{
    if(lz->size)
        lz->bits[lz->size-1] = 9;

    // Go backwards in file storing best parsing
    for(int pos = lz->size - 2; pos>=0; pos--)
    {
        // Get best match at this position
        int mp = 0;
        int ml = match(lz->data , pos, lz->size, &mp);

        // Init "no-match" case
        int best = lz->bits[pos+1] + 9;

        // Check all posible match lengths, store best
        lz->bits[pos] = best;
        lz->mpos[pos] = mp;
        for(int l=ml; l>=min_len; l--)
        {
            int b = lz->bits[pos+l] + 9;
            if( b < best )
            {
                best = b;
                lz->bits[pos] = best;
                lz->mlen[pos] = l;
                lz->mpos[pos] = mp;
            }
        }
    }
}

static int lzop_encode(struct bf *b, struct lzop *lz, int pos, int lpos)
{
    if( pos <= lpos )
        return lpos;

    int mlen = lz->mlen[pos];
    int mpos = lz->mpos[pos];

    // Encode best from filled table
    if( mlen < min_len )
    {
        // No match, just encode the byte
        add_bit(b,1);
        add_byte(b, lz->data[pos]);
        stat_len[0] ++;
        return pos;
    }
    else
    {
        int code_pos = (pos - 1 - mpos) & 0x0F;
        int code_len = mlen - 2;
        add_bit(b,0);
        add_byte(b,(code_pos<<4) + code_len);
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
        data[i] = malloc(128*1024); // calloc(128,1024);
        lpos[i] = -1;
    }

    // Set stdin and stdout as binary files
    set_binary();

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
    int sz;
    for( sz = 0;  1 == fread(buf, 9, 1, stdin) && sz < (128*1024); sz++ )
    {
        for(int i=0; i<9; i++)
        {
            // Simplify patterns - rewrite silence as 0
            if( (i & 1) == 1 )
            {
                int vol  = buf[i] & 0x0F;
                int dist = buf[i] & 0xF0;
                if( vol == 0 )
                    buf[i] = 0;
                else if( dist & 0x10 )
                    buf[i] &= 0x1F;     // volume-only, ignore other bits
                else if( dist & 0x20 )
                    buf[i] &= 0xBF;     // no noise, ignore noise type bit
            }
            data[i][sz] = buf[i];
        }
    }
    // Check for empty streams and warn
    for(int i=0; i<9; i++)
    {
        const uint8_t *p = data[i], s = *p;
        int n = 0;
        for(int j=0; j<sz; j++)
            if( *p++ != s )
                n++;
        if( !n )
        {
            fprintf(stderr,"WARNING: stream #%d ", i);
            if( s == 0 )
                fprintf(stderr,"is empty");
            else
                fprintf(stderr,"contains only $%02X", s);
            fprintf(stderr, ", should not be included in output!\n");
        }
    }

    // Init LZ states
    struct lzop lz[9];
    for(int i=0; i<9; i++)
    {
        lzop_init(&lz[i], data[i], sz);
        lzop_backfill(&lz[i]);
    }

    // Compress
    init(&b);
    for(int pos = 0; pos < sz; pos++)
        for(int i=0; i<9; i++)
            lpos[i] = lzop_encode(&b, &lz[i], pos, lpos[i]);
    bflush(&b);
    fflush(stdout);

    // Show stats
    fprintf(stderr,"Ratio: %d / %d = %.2f%%\n", b.total, 9*sz, (100.0*b.total) / (9.0*sz));
    for(int i=0; i<9; i++)
        fprintf(stderr," Stream #%d: %d bits,\t%5.2f%%,\t%5.2f%% of output\n", i,
                lz[i].bits[0], (100.0*lz[i].bits[0]) / (8.0*sz),
                (100.0*lz[i].bits[0])/(8.0*b.total) );

    fprintf(stderr,"\nvalue\t  POS\t  LEN\n");
    for(int i=0; i<=max(max_len,max_off); i++)
    {
        fprintf(stderr,"%2d\t%5d\t%5d\n", i, stat_off[i], stat_len[i]);
    }
    return 0;
}

