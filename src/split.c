#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main()
{
    FILE *f[9];
    uint8_t buf[9];
    for(int i=0; i<9; i++)
    {
        char name[16] = "test.split.0";
        name[11] = '0' + i;
        f[i] = fopen(name, "wb");
    }

    // Skip SAP header
    char header_line[128];
    long pos = ftell(stdin);
    while( 0 != fgets(header_line, 80, stdin) )
    {
        size_t ln = strlen(header_line);
        if( ln < 1 || header_line[ln-1] != '\n' )
            break;
        pos = ftell(stdin);
    }
    fseek(stdin, pos, SEEK_SET);

    while( 1 == fread(buf, 9, 1, stdin) )
    {
        for(int i=0; i<9; i++)
            putc(buf[i], f[i]);
    }
    for(int i=0; i<9; i++)
        fclose(f[i]);
    return 0;
}
