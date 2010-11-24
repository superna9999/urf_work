/**
 * This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief Decode URF  to a BMP file
 * @file urf_decode.c
 * @author Neil 'Superna' Armstrong <superna9999@gmail.com> (C) 2010
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define PROGRAM "urftobmp"

#ifdef URF_DEBUG
#define dprintf(format, ...) fprintf(stderr, "DEBUG: (" PROGRAM ") " format, __VA_ARGS__)
#else
#define dprintf(format, ...)
#endif

#define iprintf(format, ...) fprintf(stderr, "INFO: (" PROGRAM ") " format, __VA_ARGS__)

void die(char * str)
{
    printf("CRIT: (" PROGRAM ") die(%s) [%m]\n", str);
    exit(1);
}

//------------- BMP ---------------
#define MAGIC_POS   0
struct bmpfile_magic {
  unsigned char magic[2];
} __attribute__((__packed__));
 
#define HEADER_POS   2
struct bmpfile_header {
  uint32_t filesz;
  uint16_t creator1;
  uint16_t creator2;
  uint32_t bmp_offset;
} __attribute__((__packed__));

#define DIB_POS 14
typedef struct {
  uint32_t header_sz;
  int32_t width;
  int32_t height;
  uint16_t nplanes;
  uint16_t bitspp;
  uint32_t compress_type;
  uint32_t bmp_bytesz;
  int32_t hres;
  int32_t vres;
  uint32_t ncolors;
  uint32_t nimpcolors;
} __attribute__((__packed__)) BITMAPINFOHEADER;

typedef enum {
  BI_RGB = 0,
  BI_RLE8,
  BI_RLE4,
  BI_BITFIELDS,
  BI_JPEG,
  BI_PNG,
} bmp_compression_method_t;

struct bmp_info
{
    void * data;
    uint8_t * bitmap;
    unsigned width;
    unsigned height;
    unsigned stride_bytes;
    unsigned pixel_bytes;
    unsigned line_bytes;
    unsigned file_size;
    unsigned bitmap_size;
    unsigned bitmap_offset;
    unsigned bpp;
};

int create_bmp_file(unsigned width, unsigned height, struct bmp_info * info, int bpp)
{
    int pixel_bytes = 0;
    unsigned line_size;
    unsigned data_size;
    unsigned raw_size;
    unsigned raw_pos = DIB_POS + sizeof(BITMAPINFOHEADER);
    uint8_t * data;
    struct bmpfile_magic * magic = NULL;
    struct bmpfile_header * header = NULL;
    BITMAPINFOHEADER * dib = NULL;

    switch(bpp)
    {
        case 24:
            pixel_bytes = 3;
            // 4bytes stride alignment
            line_size = width*pixel_bytes;
            line_size = (line_size/4 + (line_size%4?1:0))*4;
            break;
        default:
            printf("TODO: Other bpp handling...\n");
            return -1;
    }

    raw_size = line_size*height;
    data_size = raw_size +
                     sizeof(struct bmpfile_magic) +
                     sizeof(struct bmpfile_header) +
                     sizeof(BITMAPINFOHEADER);
    data = malloc(data_size);

    if(data == NULL)
    {
        printf("BMP allocation error... (%m)\n");
        return -1;
    }

    //Pointers
    magic = (struct bmpfile_magic *)&data[MAGIC_POS];
    header = (struct bmpfile_header *)&data[HEADER_POS];
    dib = (BITMAPINFOHEADER *)&data[DIB_POS];

    //Filling
    magic->magic[0] = 'B';
    magic->magic[1] = 'M';

    header->filesz = data_size;
    header->creator1 = 0;
    header->creator2 = 0;
    header->bmp_offset = raw_pos;

    dib->header_sz = 40;
    dib->width = width;
    dib->height = height;
    dib->nplanes = 1;
    dib->bitspp = bpp;
    dib->compress_type = BI_RGB;
    dib->bmp_bytesz = raw_size;
    dib->hres = 0;
    dib->vres = 0;
    dib->ncolors = 0;
    dib->nimpcolors = 0;

    // Blank it
    memset(data+raw_pos, 0xFF, raw_size);

    info->data = data;
    info->bitmap = (data+raw_pos);
    info->width = width;
    info->height = height;
    info->stride_bytes = line_size;
    info->pixel_bytes = pixel_bytes;
    info->line_bytes = (width*info->pixel_bytes);
    info->file_size = data_size;
    info->bitmap_size = raw_size;
    info->bitmap_offset = raw_pos;
    info->bpp = bpp;

    return 0;
}

void bmp_set_line(struct bmp_info * info, int line_n, uint8_t line[])
{
    dprintf("bmp_set_line(%d)\n", line_n);

    if(line_n > info->height)
    {
        dprintf("Bad line %d\n", line_n);
        return;
    }

    memcpy(&info->bitmap[(info->height-line_n-1)*info->stride_bytes],
           line, info->line_bytes);
}

// Data are in network endianness
struct urf_file_header {
    char unirast[8];
    uint32_t page_count;
} __attribute__((__packed__));

struct urf_page_header {
    uint8_t bpp;
    uint8_t colorspace;
    uint8_t duplex;
    uint8_t quality;
    uint32_t unknown0;
    uint32_t unknown1;
    uint32_t width;
    uint32_t height;
    uint32_t dot_per_inch;
    uint32_t unknown2;
    uint32_t unknown3;
} __attribute__((__packed__));

int decode_raster(int fd, int width, int height, int bpp, struct bmp_info * bmp)
{
    // We should be at raster start
    int i, j;
    int cur_line = 0;
    int pos = 0;
    uint8_t line_repeat_byte = 0;
    unsigned line_repeat = 0;
    int8_t packbit_code = 0;
    int pixel_size = (bpp/8);
    uint8_t * pixel_container;
    uint8_t * line_container;

    pixel_container = malloc(pixel_size);
    line_container = malloc(pixel_size*width);

    do
    {
        if(read(fd, &line_repeat_byte, 1) < 1)
        {
            dprintf("l%06d : line_repeat EOF at %lu\n", cur_line, lseek(fd, 0, SEEK_CUR));
            return 1;
        }

        line_repeat = (unsigned)line_repeat_byte + 1;

        dprintf("l%06d : next actions for %d lines\n", cur_line, line_repeat);

        // Start of line
        pos = 0;

        do
        {
            if(read(fd, &packbit_code, 1) < 1)
            {
                dprintf("p%06dl%06d : packbit_code EOF at %lu\n", pos, cur_line, lseek(fd, 0, SEEK_CUR));
                return 1;
            }

            dprintf("p%06dl%06d: Raster code %02X='%d'.\n", pos, cur_line, (uint8_t)packbit_code, packbit_code);

            if(packbit_code == -128)
            {
                dprintf("\tp%06dl%06d : blank rest of line.\n", pos, cur_line);
                memset((line_container+(pos*pixel_size)), 0xFF, (pixel_size*(width-pos)));
                pos = width;
                break;
            }
            else if(packbit_code >= 0 && packbit_code <= 127)
            {
                int n = (packbit_code+1);

                //Read pixel
                if(read(fd, pixel_container, pixel_size) < pixel_size)
                {
                    dprintf("p%06dl%06d : pixel repeat EOF at %lu\n", pos, cur_line, lseek(fd, 0, SEEK_CUR));
                    return 1;
                }

                dprintf("\tp%06dl%06d : Repeat pixel '", pos, cur_line);
                for(j = 0 ; j < pixel_size ; ++j)
                    dprintf("%02X ", pixel_container[j]);
                dprintf("' for %d times.\n", n);

                for(i = 0 ; i < n ; ++i)
                {
                    //for(j = pixel_size-1 ; j >= 0 ; --j)
                    for(j = 0 ; j < pixel_size ; ++j)
                        line_container[pixel_size*pos + j] = pixel_container[(pixel_size-j-1)];
                    ++pos;
                    if(pos >= width)
                        break;
                }

                if(i < n && pos >= width)
                {
                    dprintf("\tp%06dl%06d : Forced end of line for pixel repeat.\n", pos, cur_line);
                }
                
                if(pos >= width)
                    break;
            }
            else if(packbit_code > -128 && packbit_code < 0)
            {
                int n = (-(int)packbit_code)+1;

                dprintf("\tp%06dl%06d : Copy %d verbatim pixels.\n", pos, cur_line, n);

                for(i = 0 ; i < n ; ++i)
                {
                    if(read(fd, pixel_container, pixel_size) < pixel_size)
                    {
                        dprintf("p%06dl%06d : literal_pixel EOF at %lu\n", pos, cur_line, lseek(fd, 0, SEEK_CUR));
                        return 1;
                    }
                    //Invert pixels, should be programmable
                    for(j = 0 ; j < pixel_size ; ++j)
                        line_container[pixel_size*pos + j] = pixel_container[(pixel_size-j-1)];
                    ++pos;
                    if(pos >= width)
                        break;
                }

                if(i < n && pos >= width)
                {
                    dprintf("\tp%06dl%06d : Forced end of line for pixel copy.\n", pos, cur_line);
                }
                
                if(pos >= width)
                    break;
            }
        }
        while(pos < width);

        dprintf("\tl%06d : End Of line, drawing %d times.\n", cur_line, line_repeat);

        // write lines
        for(i = 0 ; i < line_repeat ; ++i)
        {
            bmp_set_line(bmp, cur_line, line_container);
            ++cur_line;
        }
    }
    while(cur_line < height);
}

#define FORMAT_BMP  "page%04d.bmp"

int main(int argc, char **argv)
{
    int fd, page, fd_bmp, ret;
    struct urf_file_header head, head_orig;
    struct urf_page_header page_header, page_header_orig;
    struct bmp_info bmp;
    char bmpfile[255];

    if((fd = open(argv[1], O_RDONLY)) == -1) die("Unable to open unirast file");

    lseek(fd, 0, SEEK_SET);

    if(read(fd, &head_orig, sizeof(head)) == -1) die("Unable to read file header");

    //Transform
    memcpy(head.unirast, head_orig.unirast, sizeof(head.unirast));
    head.page_count = ntohl(head_orig.page_count);

    if(head.unirast[7])
        head.unirast[7] = 0;

    if(strncmp(head.unirast, "UNIRAST", 7) != 0) die("Bad File Header");

    iprintf("%s file, with %d page(s).\n", head.unirast, head.page_count);

    for(page = 0 ; page < head.page_count ; ++page)
    {
        if(read(fd, &page_header_orig, sizeof(page_header_orig)) == -1) die("Unable to read page header");

        //Transform
        page_header.bpp = page_header_orig.bpp;
        page_header.colorspace = page_header_orig.colorspace;
        page_header.duplex = page_header_orig.duplex;
        page_header.quality = page_header_orig.quality;
        page_header.unknown0 = 0;
        page_header.unknown1 = 0;
        page_header.width = ntohl(page_header_orig.width);
        page_header.height = ntohl(page_header_orig.height);
        page_header.dot_per_inch = ntohl(page_header_orig.dot_per_inch);
        page_header.unknown2 = 0;
        page_header.unknown3 = 0;

        iprintf("Page %d :\n", page);
        iprintf("Bits Per Pixel : %d\n", page_header.bpp);
        iprintf("Colorspace : %d\n", page_header.colorspace);
        iprintf("Duplex Mode : %d\n", page_header.duplex);
        iprintf("Quality : %d\n", page_header.quality);
        iprintf("Size : %dx%d pixels\n", page_header.width, page_header.height);
        iprintf("Dots per Inches : %d\n", page_header.dot_per_inch);

        if(create_bmp_file(page_header.width, page_header.height, &bmp, page_header.bpp) != 0) die("Unable to create BMP file");
        sprintf(bmpfile, FORMAT_BMP, page);

		iprintf("BMP File '%s'\n", bmpfile);

        decode_raster(fd, page_header.width, page_header.height, page_header.bpp, &bmp);

        if((fd_bmp = open(bmpfile, O_CREAT|O_TRUNC|O_WRONLY, 0666)) == -1) die("Unable to open BMP file for writing");
        if(fd_bmp >= 0)
        {
            if(write(fd_bmp, bmp.data, bmp.file_size) == -1) die("Unable to write BMP file");
            close(fd_bmp);
        }
        free(bmp.data);
        memset(&bmp, 0, sizeof(bmp));
    }

    return 0;
}
