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

int decode_raster(int fd, int width, int height, int bpp)
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

        printf("%06dx%06d: Raster code '%d' for %d lines.\n", pos, lines, packbit_code, line_repeat);

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

                pos += n;

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
                    read(fd, pixel_container, pixel_size);
                pos += n;

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

int main(int argc, char **argv)
{
    int fd, page;
    struct urf_file_header head, head_orig;
    struct urf_page_header page_header, page_header_orig;

    fd = open(argv[1], O_RDONLY);

    read(fd, &head_orig, sizeof(head));

    //Transform
    memcpy(head.unirast, head_orig.unirast, sizeof(head.unirast));
    head.page_count = ntohl(head_orig.page_count);

    if(head.unirast[7])
        head.unirast[7] = 0;
    printf("%s file, with %d page(s).\n", head.unirast, head.page_count);

    for(page = 0 ; page < head.page_count ; ++page)
    {
        read(fd, &page_header_orig, sizeof(page_header_orig));

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

        decode_raster(fd, page_header.width, page_header.height, page_header.bpp);
    }

    return 0;
}
