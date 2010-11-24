all: urftobmp urftotiff

urftobmp:urftobmp.c

urftotiff:urftotiff.c
	$(CC) urftotiff.c -ltiff -o urftotiff
