Atari SAP Type R File Compressor and Player
===========================================

This is a compressor for Atari POKEY music in SAP Type R format with a player
for the Atari 8-bit computers.


The SAP Type R file format is simply a dump of the POKEY registers to write in
each frame, and can be produced by many emulators. By compressing each register
write with the LZSS format, a smaller file can be produced, and can be played
by decompressing in real time in the Atari.


The **LZSS** compression is an LZ77 variant optimized for small memory and file
sizes, and it is very fast to compress and decompress in old hardware.


LZSS compressor: `bin/lzss`
---------------------------

Program to compress a type R SAP (SAP-R) file into the lzss format.

Usage: `bin/lzss [options] <input_file> <output_file>`

If `output_file` is omitted, write to standard output, and if `input_file` is
also omitted, read from standard input.

Options:
 - `-8     	` Sets default 8 bit match size, this is the same as `-b 8 -o 4 -m 2`.
 - `-2     	` Sets default 12 bit match size, this is the same as `-b 12 -o 7 -m 2`.
 - `-6     	` Sets default 16 bit match size, this is the same as `-b 16 -o 8 -m 1`.
 - `-o BITS	` Sets match offset bits (default is 4 bits).
 - `-l BITS	` Sets match length bits (default is 4 bits).
 - `-b BITS	` Sets match total bits (=offset+length) (default is 8 bits).
 - `-m NUM 	` Sets minimum match length (default is 2 bits).
 - `-e          ` Don't force a literal at end of stream. Normally, the encoder
                  enforces that the last encoded byte of at least one channel
                  is a literal, this allows the decoder to find the end of the
                  song. With this option, the compressed file will be smaller
                  but the decoder won't detect the end correctly.
 - `-t          ` Trim the SAP-R data before compressing, removes silences at start and
                  the end and detects looping at the end of the song.
 - `-x          ` Reverts to old format version, use for compatibility with old players.
 - `-v     	` Shows match length/offset statistics.
 - `-q     	` Don't show per stream compression.
 - `-h     	` Shows command line help.

The compressed files can be played with the included assembly player sources,
there are three sources included:

 - `asm/playlzs.asm` : This player support the `-8` compression option, it uses
   one byte for each match, with 16 bytes of buffer and a maximum of 17 bytes
   for each match.

   It uses the smallest amount of memory (16 * 9 bytes of RAM are needed) but
   does not compresses well.

 - `asm/playlzs12.asm` : This player support the `-2` compression option, it
   uses 12 bits for each match, with 128 bytes of buffer and a maximum of 33
   bytes for each match.

   This uses a larger buffer (128 * 9 bytes of RAM), so it compress almost as
   good as the 16 bit variant.

 - `asm/playlzs16.asm` : This player support the `-6` compression option, it
   uses 16 bits (two bytes) for each match, with 256 bytes of buffer and a
   maximum of 256 bytes for each match.

   This player is faster than the above, as each match is two full bytes, but
   uses the largest amount of buffer (256 * 9 bytes of RAM). It tends to
   compress better than all the other, but your mileage may vary depending on
   the specific SAP file.


Other tools included
--------------------

There are two other tools included in the repository:

- `bin/lz4s`

  This is a compressor for a modified LZ4 compression format. It uses more RAM
  and compresses worst than the LZSS compressor in most files, it is only
  included as a reference.

  Tee LZ4 format performs better with larger buffer sizes (more than 1kB), that
  would imply using more than 8kB of RAM in the player.


- `bin/split`

  This simple program just splits a SAP Type R file into one file for the data
  of each POKEY register, allowing to try external compressors on each stream.


