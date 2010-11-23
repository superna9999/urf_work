/**
 * @brief Decode URF 
 * @file urf_decode.c
 * @author Neil 'Superna' Armstrong (c) 2010
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

void die(char * str)
{
    printf("die(%s) [%m]\n", str);
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
    info->file_size = data_size;
    info->bitmap_size = raw_size;
    info->bitmap_offset = raw_pos;
    info->bpp = bpp;

    return 0;
}

void bmp_set_pixel(struct bmp_info * info, int x, int y, unsigned bpp, uint8_t pixel[])
{
    int i;

    printf("bmp_set_pixel(%d, %d, [", x, y);

    for(i = 0 ; i < (bpp/8) ; ++i)
        printf("%02X ", pixel[i]);
    printf("]\n");

    if(bpp != info->bpp)
    {
        printf("Bad BPP... %d\n", bpp);
        return;
    }

    if(x > info->width)
    {
        printf("Bad pixel width %d\n", x);
        return;
    }
    if(y > info->height)
    {
        printf("Bad pixel height %d\n", y);
        return;
    }

    switch(bpp)
    {
        case 24:
        {
            info->bitmap[(info->height-y)*info->stride_bytes + x*info->pixel_bytes + 0] = pixel[2];
            info->bitmap[(info->height-y)*info->stride_bytes + x*info->pixel_bytes + 1] = pixel[1];
            info->bitmap[(info->height-y)*info->stride_bytes + x*info->pixel_bytes + 2] = pixel[0];
        } break;
        //TODO: Manage other bpp
    }
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
    int lines = 0;
    int pos = 0;
    uint8_t line_repeat = 0;
    int8_t packbit_code = 0;
    int pixel_size = (bpp/8);
    uint8_t * pixel_container;

    pixel_container = malloc(pixel_size);

    do
    {
        read(fd, &line_repeat, 1);
        read(fd, &packbit_code, 1);

        printf("%06dx%06d: Raster code %02X='%d' for %02X=%d lines.\n", pos, lines, packbit_code, packbit_code, line_repeat, line_repeat);

        if(packbit_code == -128)
        {
            if(pos)
            {   printf("\t%06dx%06d : Go to next line.\n", pos, lines, line_repeat);
                pos = 0;
                ++lines;
                --line_repeat;
            }
            printf("\t%06dx%06d : Fill %d blank lines.\n", pos, lines, line_repeat);
            lines+=line_repeat;
        }
        else if(packbit_code >= 0 && packbit_code <= 127)
        {
            int n = (packbit_code+1);

            for(i = 0 ; i < line_repeat ; ++i)
            {
                //Read pixel
                read(fd, pixel_container, pixel_size);

                printf("\t%06dx%06d : Repeat pixel '", pos, lines);
                for(j = 0 ; j < pixel_size ; ++j)
                    printf("%02X ", pixel_container[j]);
                printf("' for %d times.\n", n);

                for(j = 0 ; j < n ; ++j)
                {
                    bmp_set_pixel(bmp, pos, lines, bpp, pixel_container);
                    ++pos;
                }

                if(pos == width)
                {
                    ++lines;
                    pos = 0;
                    printf("\t%06dx%06d : New Line\n", pos, lines);
                }
                else if(pos > width)
                {
                    ++lines;
                    pos -= width;
                    printf("\t%06dx%06d : New Line with offset\n", pos, lines);
                }
            }
        }
        else if(packbit_code > -128 && packbit_code < 0)
        {
            int n = (-(int)packbit_code)+1;

            for(j = 0 ; j < line_repeat ; ++j)
            {
                printf("\t%06dx%06d : Load %d pixels.\n", pos, lines, n);

                for(i = 0 ; i < n ; ++i)
                {
                    read(fd, pixel_container, pixel_size);
                    bmp_set_pixel(bmp, pos, lines, bpp, pixel_container);
                    ++pos;
                }

                if(pos == width)
                {
                    ++lines;
                    printf("\t%06dx%06d : New Line\n", pos, lines);
                }
                else if(pos > width)
                {
                    ++lines;
                    pos -= width;
                    printf("\t%06dx%06d : New Line with offset\n", pos, lines);
                }
            }
        }
    }
    while(lines < height);
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
    printf("%s file, with %d page(s).\n", head.unirast, head.page_count);

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

        printf("Page %d :\n", page);
        printf("\tBits Per Pixel : %d\n"
               "\tColorspace : %d\n"
               "\tDuplex Mode : %d\n"
               "\tQuality : %d\n"
               "\tSize : %dx%d pixels\n"
               "\tDots per Inches : %d\n",
                page_header.bpp,
                page_header.colorspace,
                page_header.duplex,
                page_header.quality,
                page_header.width,
                page_header.height,
                page_header.dot_per_inch);

        if(create_bmp_file(page_header.width, page_header.height, &bmp, page_header.bpp) != 0) die("Unable to create BMP file");
        sprintf(bmpfile, FORMAT_BMP, page);

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
