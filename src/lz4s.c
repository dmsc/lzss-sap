/*
 * Atari SAP-R File Compressor
 * ---------------------------
 *
 * This implementa an optimal (modified) LZ4 compressor for the SAP-R music
 * files.
 *
 * (c) 2020 DMSC
 * Code under MIT license, see LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


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
    int len;
    uint8_t buf[65536];
    int bnum;
    int bpos;
    int hpos;
    int total;
    FILE *out;
};

static void init(struct bf *x)
{
    x->total = 0;
    x->len = 0;
    x->bnum = 0;
    x->bpos = -1;
    x->hpos = -1;
}

static void bflush(struct bf *x)
{
    if( x->len )
        fwrite(x->buf, x->len, 1, x->out);
    x->total += x->len;
    x->len = 0;
    x->bnum = 0;
    x->bpos = -1;
    x->hpos = -1;
}


static void add_bit(struct bf *x, int bit)
{
    if( x->bpos < 0 )
    {
        // Adds a new byte holding bits
        x->bpos = x->len;
        x->bnum = 0;
        x->len++;
        x->buf[x->bpos] = 0;
    }
    if( bit )
        x->buf[x->bpos] |= 1 << x->bnum;
    x->bnum++;
    if( x->bnum == 8 )
    {
        x->bpos = -1;
        x->bnum = 0;
    }
}

static void add_byte(struct bf *x, int byte)
{
    x->buf[x->len] = byte;
    x->len ++;
}

static void add_hbyte(struct bf *x, int hbyte)
{
    if( x->hpos < 0 )
    {
        // Adds a new byte holding half-bytes
        x->hpos = x->len;
        x->len++;
        x->buf[x->hpos] = hbyte & 0x0F;
    }
    else
    {
        // Fixes last h-byte
        x->buf[x->hpos] |= hbyte << 4;
        x->hpos = -1;
    }
}

///////////////////////////////////////////////////////
// LZ4S compression functions
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

static int bits_moff = 8;       // Number of bits used for OFFSET
static int min_mlen = 2;        // Minimum match length
static int max_mlen = 255;      // Maximum match length (unlimited in LZ4)
static int max_llen = 255;      // Maximum literal length (unlimited in LZ4)

#define max_off (1<<bits_moff)  // Maximum offset

// Struct for LZ4 optimal parsing
struct lzop
{
    const uint8_t *data;// The data to compress
    int size;           // Data size
    int *bits;          // Number of bits needed to code from position
    int *mlen;          // Match/literal length at position, >0 match, <0 literal.
    int *mpos;          // Best match offset at position
    int in_literal;     // Inside match during encoding
};

static void lzop_init(struct lzop *lz, const uint8_t *data, int size)
{
    lz->data = data;
    lz->size = size;
    lz->bits = malloc(sizeof(int) * (size + 1));
    lz->mlen = malloc(sizeof(int) * (size + 1));
    lz->mpos = malloc(sizeof(int) * (size + 1));
    lz->in_literal = 0;
}

static void lzop_free(struct lzop *lz)
{
    free(lz->bits);
    free(lz->mlen);
    free(lz->mpos);
}

// Returns maximal match length (and match position) at pos.
static int match(const uint8_t *data, int pos, int size, int *mpos)
{
    int mxlen = -max(-max_mlen, pos - size);
    int mlen = 0;
    for(int i=max(pos-max_off,0); i<pos; i++)
    {
        int ml = get_mlen(data + pos, data + i, mxlen);
        if( ml > mlen )
        {
            mlen = ml;
            *mpos = pos - i;
            if( mlen >= mxlen )
                return mlen;
        }
    }
    return mlen;
}

// Returns the cost of writing this length
static int mlen_cost(int l)
{
    int n = 0;
    if( l > max_mlen )
        return 1<<30; // Infinite cost
    if( l < 15 )
        return n;
    l -= 15;
    while( l > 255 )
    {
        l -= 255;
        n++;
    }
    return 8*(n+1);
}

// Returns the *extra* cost of writing this length
static int llen_cost(int l)
{
    if( l >= max_llen )
        return 24; // Encode a "bad match"
    if( l == 1 )
        return 8;
    l -= 15;
    while( l > 0 )
        l -= 255;
    return l ? 0 : 8;
}


static void lzop_backfill(struct lzop *lz)
{
    if(lz->size <= 0)
        return;

    // Initialize last positions of the array
    lz->bits[lz->size-1] = 8;
    lz->mlen[lz->size-1] = -1;
    lz->bits[lz->size] = 0;
    lz->mlen[lz->size] = 0;

    // Go backwards in file storing best parsing
    for(int pos = lz->size - 2; pos>=0; pos--)
    {
        // Get best match at this position
        int mp = 0;
        int ml = match(lz->data , pos, lz->size, &mp);

        // Init "no-match" case
        int llen = lz->mlen[pos+1] > 0 ? 1 : 1 - lz->mlen[pos+1];
        int best = lz->bits[pos+1] + 8 + llen_cost(llen);

        // Check all posible match lengths, store best
        lz->bits[pos] = best;
        lz->mpos[pos] = mp;
        lz->mlen[pos] = -llen;
        for(int l=min_mlen; l<=ml; l++)
        {
            int b = lz->bits[pos+l] + (bits_moff>8?16:8) + mlen_cost(l-2);
            if( lz->mlen[pos+l] > 0 )
                b += 8;
            if( b <= best )
            {
                best = b;
                lz->bits[pos] = best;
                lz->mlen[pos] = l;
                lz->mpos[pos] = mp;
            }
        }
    }
}

static void encode_len(struct bf *b, int len, int max)
{
    add_hbyte(b, len < 15 ? len : 15);
    if( max < 16 || len < 15 )
        return;
    if( max >= 256 )
    {
        len -= 15;
        max -= 15;
    }
    add_byte(b, len < 255 ? len : 255);
    while( len >= 255 && max > 255 )
    {
        len -= 255;
        max -= 255;
        add_byte(b, len);
    }
}

static int lzop_encode(struct bf *b, struct lzop *lz, int pos, int lpos)
{
    if( pos <= lpos )
    {
        if( lz->in_literal )
        {
//            fprintf(stderr,"L.: %02x\n", lz->data[pos]);
            add_byte(b, lz->data[pos]);
        }
        return lpos;
    }

    int mlen = lz->mlen[pos];
    int mpos = lz->mpos[pos];

    // Encode best from filled table
    if( mlen < min_mlen )
    {
        // No match, just encode the byte
        mlen = -mlen;
        if( mlen > max_llen )
            mlen = max_llen;
//        fprintf(stderr,"L[%d]: %02x\n", mlen, lz->data[pos]);
        if( lz->in_literal )
        {
            // Already on literal - encode a zero length match to terminate
            add_hbyte(b, 15);
            add_byte(b, 0);
        }
        // Encode new literal count
        encode_len(b, mlen, max_llen);
        // And first literal
        add_byte(b, lz->data[pos]);
        lz->in_literal = 1;
    }
    else
    {
        int code_pos = (pos - 1 - mpos) & (max_off - 1);
//        fprintf(stderr,"M(%d): %02x : %02x\n", mlen, code_pos, mlen);
        if( !lz->in_literal )
        {
            // Already on match - encode a zero length literal
            add_hbyte(b, 0);
        }
        encode_len(b, mlen-2, max_mlen);
        if( bits_moff )
            add_byte(b,code_pos & 0xFF );
        if( bits_moff > 8 )
            add_byte(b,code_pos >> 8 );

        lz->in_literal = 0;
    }
    return pos + mlen - 1;
}

static const char *prog_name;
static void cmd_error(const char *msg)
{
    fprintf(stderr,"%s: error, %s\n"
            "Try '%s -h' for help.\n", prog_name, msg, prog_name);
    exit(1);
}

///////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    struct bf b;
    uint8_t buf[9], *data[9];
    char header_line[128];
    int lpos[9];
    int show_stats = 1;

    prog_name = argv[0];
    int opt;
    while( -1 != (opt = getopt(argc, argv, "hqvo:l:m:")) )
    {
        switch(opt)
        {
            case 'o':
                bits_moff = atoi(optarg);
                break;
            case 'l':
                max_llen = atoi(optarg);
                break;
            case 'm':
                max_mlen = atoi(optarg);
                break;
            case 'v':
                show_stats = 2;
                break;
            case 'q':
                show_stats = 0;
                break;
            case 'h':
            default:
                fprintf(stderr,
                       "LZ4S SAP Type-R compressor - by dmsc.\n"
                       "\n"
                       "Usage: %s [options] <input_file> <output_file>\n"
                       "\n"
                       "If output_file is omitted, write to standard output, and if\n"
                       "input_file is also omitted, read from standard input.\n"
                       "\n"
                       "Options:\n"
                       "  -o BITS  Sets match offset bits (default = %d).\n"
                       "  -l NUM   Sets max literal run length (default = %d).\n"
                       "  -m NUM   Sets max match run length (default = %d).\n"
                       "  -v       Shows match length/offset statistics.\n"
                       "  -q       Don't show per stream compression.\n"
                       "  -h       Shows this help.\n",
                       prog_name, bits_moff, max_llen, max_mlen);
                exit(EXIT_FAILURE);
        }
    }

    // Check option values
    if( bits_moff < 0 || bits_moff > 16 )
        cmd_error("match offset bits should be from 0 to 16");
    if( max_mlen < 1 || max_mlen > 65536 )
        cmd_error("max match run length should be from 1 to 65536");
    if( max_llen < 1 || max_llen > 65536 )
        cmd_error("max literal run length should be from 1 to 65536");

    if( optind < argc-2 )
        cmd_error("too many arguments: one input file and one output file expected");
    FILE *input_file = stdin;
    if( optind < argc )
    {
        input_file = fopen(argv[optind], "rb");
        if( !input_file )
        {
            fprintf(stderr, "%s: can't open input file '%s': %s\n",
                    prog_name, argv[optind], strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    // Set stdin and stdout as binary files
    set_binary();

    // Max size of each bufer: 128k
    for(int i=0; i<9; i++)
    {
        data[i] = malloc(128*1024); // calloc(128,1024);
        lpos[i] = -1;
    }

    // Skip SAP header
    long pos = ftell(input_file);
    while( 0 != fgets(header_line, 80, input_file) )
    {
        size_t ln = strlen(header_line);
        if( ln < 1 || header_line[ln-1] != '\n' )
            break;
        pos = ftell(input_file);
        if( (ln == 2 && header_line[ln-2] == '\r') || (ln == 1) )
            break;
    }

    fseek(input_file, pos, SEEK_SET);
    // Read all data
    int sz;
    for( sz = 0;  1 == fread(buf, 9, 1, input_file) && sz < (128*1024); sz++ )
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
    // Close file
    if( input_file != stdin )
        fclose(input_file);

    // Open output file if needed
    FILE *output_file = stdout;
    if( optind < argc-1 )
    {
        output_file = fopen(argv[optind+1], "wb");
        if( !output_file )
        {
            fprintf(stderr, "%s: can't open output file '%s': %s\n",
                    prog_name, argv[optind+1], strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    b.out = output_file;
    // Check for empty streams and warn
    int chn_skip[9];;
    init(&b);
    for(int i=8; i>=0; i--)
    {
        const uint8_t *p = data[i], s = *p;
        int n = 0;
        for(int j=0; j<sz; j++)
            if( *p++ != s )
                n++;
        if( i != 0 && !n )
        {
            if( show_stats )
                fprintf(stderr,"Skipping channel #%d, set with $%02x.\n", i, s);
            add_bit(&b,1);
            add_byte(&b,s);
            chn_skip[i] = 1;
        }
        else
        {
            if( i )
                add_bit(&b,0);
            chn_skip[i] = 0;
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
    }
    bflush(&b);

    // Init LZ states
    struct lzop lz[9];
    for(int i=0; i<9; i++)
        if( !chn_skip[i] )
        {
            lzop_init(&lz[i], data[i], sz);
            lzop_backfill(&lz[i]);
        }

    // Compress
    init(&b);
    for(int pos = 0; pos < sz; pos++)
        for(int i=8; i>=0; i--)
            if( !chn_skip[i] )
                lpos[i] = lzop_encode(&b, &lz[i], pos, lpos[i]);
    bflush(&b);
    // Close file
    if( output_file != stdout )
        fclose(output_file);
    else
        fflush(stdout);


    // Show stats
    fprintf(stderr,"LZ4S: max offset= %d,\tmax mlen= %d,\tmax llen= %d,\t",
            max_off, max_mlen, max_llen);
    fprintf(stderr,"ratio: %5d / %d = %5.2f%%\n", b.total, 9*sz, (100.0*b.total) / (9.0*sz));
    if( show_stats )
        for(int i=0; i<9; i++)
            if( !chn_skip[i] )
                fprintf(stderr," Stream #%d: %d bits,\t%5.2f%%,\t%5.2f%% of output\n", i,
                        lz[i].bits[0], (100.0*lz[i].bits[0]) / (8.0*sz),
                        (100.0*lz[i].bits[0])/(8.0*b.total) );

    // Free memory
    for(int i=0; i<9; i++)
    {
        free(data[i]);
        if( !chn_skip[i] )
            lzop_free(&lz[i]);
    }
    return 0;
}

