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
 * @brief Decode URF  to a TIFF file
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
#include "tiffio.h"

#define PROGRAM "urftotiff"

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

//------------- TIFF ---------------

struct tiff_info
{
    TIFF * tif;
    unsigned width;
    unsigned height;
    unsigned pixel_bytes;
    unsigned line_bytes;
    unsigned stride_bytes;
    unsigned bpp;
};

int create_tiff_file(char * filename, unsigned width, unsigned height, struct tiff_info * info, int bpp, unsigned dpi)
{
    info->tif = TIFFOpen(filename, "wb");

    if(info->tif == NULL) die("TIFF open error");

    TIFFSetField(info->tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(info->tif, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(info->tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(info->tif, TIFFTAG_SAMPLESPERPIXEL, bpp/8);
    TIFFSetField(info->tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(info->tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(info->tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(info->tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(info->tif, (uint32_t)-1));

    TIFFSetField(info->tif, TIFFTAG_XRESOLUTION, (float)dpi);
    TIFFSetField(info->tif, TIFFTAG_YRESOLUTION, (float)dpi);
    TIFFSetField(info->tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);

    TIFFSetField(info->tif, TIFFTAG_COMPRESSION, COMPRESSION_PACKBITS);

    info->width = width;
    info->height = height;
    info->pixel_bytes = bpp/8;
    info->line_bytes = (width*info->pixel_bytes);
    info->bpp = bpp;

    return 0;
}

int close_tiff_file(struct tiff_info * info)
{
    TIFFClose(info->tif);
    return 0;
}

void tiff_set_line(struct tiff_info * info, int line_n, uint8_t line[])
{
    dprintf("tiff_set_line(%d)\n", line_n);

    if(line_n > info->height)
    {
        dprintf("Bad line %d\n", line_n);
        return;
    }
    
    TIFFWriteScanline(info->tif, line, line_n, 0);
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

int decode_raster(int fd, int width, int height, int bpp, struct tiff_info * tiff)
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
                        line_container[pixel_size*pos + j] = pixel_container[j];
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
                        line_container[pixel_size*pos + j] = pixel_container[j];
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
            tiff_set_line(tiff, cur_line, line_container);
            ++cur_line;
        }
    }
    while(cur_line < height);
}

#define FORMAT_TIFF  "page%04d.tiff"

int main(int argc, char **argv)
{
    int fd, page, fd_tiff, ret;
    struct urf_file_header head, head_orig;
    struct urf_page_header page_header, page_header_orig;
    struct tiff_info tiff;
    char tifffile[255];

    if((fd = open(argv[1], O_RDONLY)) == -1) die("Unable to open unirast file");

    lseek(fd, 0, SEEK_SET);

    if(read(fd, &head_orig, sizeof(head)) == -1) die("Unable to read file header");

    //Transform
    memcpy(head.unirast, head_orig.unirast, sizeof(head.unirast));
    head.page_count = ntohl(head_orig.page_count);

    if(head.unirast[7])
        head.unirast[7] = 0;
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

        sprintf(tifffile, FORMAT_TIFF, page);

        iprintf("TIFF File '%s'\n", tifffile);

        if(create_tiff_file(tifffile, page_header.width, page_header.height, &tiff, page_header.bpp, page_header.dot_per_inch) != 0) die("Unable to create TIFF file");

        decode_raster(fd, page_header.width, page_header.height, page_header.bpp, &tiff);

        close_tiff_file(&tiff);

        memset(&tiff, 0, sizeof(tiff));
    }

    return 0;
}
