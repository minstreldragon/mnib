/*
 * n2d.c - Converts nibbler data to D64 image
 *
 * Written by
 *  Markus Brenner (markus@brenner.de)
 * Based on d64tog64.c code by
 *  Andreas Boose  (boose@unixserv.rz.fh-hannover.de)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#define VERSION 0.13

#define BYTE unsigned char
#define DWORD unsigned int
#define MAX_TRACKS_1541 42
#define MAXSTRLEN 80
#define HEADLONG 8

#define BLOCKSONDISK 683
#define BLOCKSEXTRA 85
#define MAXBLOCKSONDISK (BLOCKSONDISK+BLOCKSEXTRA)

#define OK				1
#define NOT_FOUND_ERROR	4
#define CHECKSUM_ERROR	5

static char sector_map_1541[43] =
{
    0,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21,     /*  1 - 10 */
    21, 21, 21, 21, 21, 21, 21, 19, 19, 19,     /* 11 - 20 */
    19, 19, 19, 19, 18, 18, 18, 18, 18, 18,     /* 21 - 30 */
    17, 17, 17, 17, 17,                         /* 31 - 35 */
    17, 17, 17, 17, 17, 17, 17          /* Tracks 36 - 42 are non-standard. */
};


static int speed_map_1541[42] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                  3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 1, 1,
                                  1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0 };


static BYTE GCR_conv_data[16] = { 0x0a, 0x0b, 0x12, 0x13,
                                  0x0e, 0x0f, 0x16, 0x17,
                                  0x09, 0x19, 0x1a, 0x1b,
                                  0x0d, 0x1d, 0x1e, 0x15 };

static int GCR_decode[32] = { -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
                              -1, 0x8, 0x0, 0x1,  -1, 0xc, 0x4, 0x5,
                              -1,  -1, 0x2, 0x3,  -1, 0xf, 0x6, 0x7,
                              -1, 0x9, 0xa, 0xb,  -1, 0xd, 0xe,  -1 };
                             

static void convert_4bytes_to_GCR(BYTE *buffer, BYTE *ptr)
{
    *ptr = GCR_conv_data[(*buffer) >> 4] << 3;
    *ptr |= GCR_conv_data[(*buffer) & 0x0f] >> 2;
    ptr++;

    *ptr = GCR_conv_data[(*buffer) & 0x0f] << 6;
    buffer++;
    *ptr |= GCR_conv_data[(*buffer) >> 4] << 1;
    *ptr |= GCR_conv_data[(*buffer) & 0x0f] >> 4;
    ptr++;

    *ptr = GCR_conv_data[(*buffer) & 0x0f] << 4;
    buffer++;
    *ptr |= GCR_conv_data[(*buffer) >> 4] >> 1;
    ptr++;

    *ptr = GCR_conv_data[(*buffer) >> 4] << 7;
    *ptr |= GCR_conv_data[(*buffer) & 0x0f] << 2;
    buffer++;
    *ptr |= GCR_conv_data[(*buffer) >> 4] >> 3;
    ptr++;

    *ptr = GCR_conv_data[(*buffer) >> 4] << 5;
    *ptr |= GCR_conv_data[(*buffer) & 0x0f];
}


int convert_4bytes_from_GCR(BYTE *gcr, BYTE *plain)
{
    int hnibble, lnibble;

    if((hnibble = GCR_decode[gcr[0] >> 3]) < 0) return -1;
    if((lnibble = GCR_decode[((gcr[0] << 2) | (gcr[1] >> 6)) & 0x1f]) < 0) return -1;
    *plain++ = hnibble << 4 | lnibble;
    if((hnibble = GCR_decode[(gcr[1] >> 1) & 0x1f]) < 0) return -1;
    if((lnibble = GCR_decode[((gcr[1] << 4) | (gcr[2] >> 4)) & 0x1f]) < 0) return -1;
    *plain++ = hnibble << 4 | lnibble;
    if((hnibble = GCR_decode[((gcr[2] << 1) | (gcr[3] >> 7)) & 0x1f]) < 0) return -1;
    if((lnibble = GCR_decode[(gcr[3] >> 2) & 0x1f]) < 0) return -1;
    *plain++ = hnibble << 4 | lnibble;
    if((hnibble = GCR_decode[((gcr[3] << 3) | (gcr[4] >> 5)) & 0x1f]) < 0) return -1;
    if((lnibble = GCR_decode[gcr[4] & 0x1f]) < 0) return -1;
    *plain++ = hnibble << 4 | lnibble;

    return(1);
}



/*
int read_GCR_id(BYTE *sixpack, BYTE *id)
{
    BYTE buf[4];

    if (convert_4bytes_from_GCR(sixpack+5,buf))
    {
        id[0]=buf[1];
        id[1]=buf[0];
        id[2]='\0';
        printf("Disk ID: %s (0x%02x 0x%02x)\n", id, id[0], id[1]);
        return(1);
    }
    else
    {
        printf("WARNING: Couldn't read disk ID!\n");
        id[0]=id[1]=0;
        id[2]='\0';
        return(0);
    }
}
*/



static int convert_GCR_sector(BYTE *gcr_track, BYTE *d64_sector, int track,
                                  int sector, BYTE diskID1, BYTE diskID2)
{
    int i;
    BYTE header[10];
    BYTE chksum;
    BYTE *gcr_ptr, *gcr_end;
    BYTE *sectordata;


    if (track > 40) return (0); /* only 40 tracks stored in sixpack */

    if (sector == 0) printf("(%02d) ", track); /* screen info */
    gcr_ptr = gcr_track;
    gcr_end= gcr_track+0x2000;

    do
    {
        for (; *gcr_ptr != 0xff; gcr_ptr++) /* find sync */
            if (gcr_ptr >= gcr_end) return (0);
        for (; *gcr_ptr == 0xff; gcr_ptr++);
            if (gcr_ptr >= gcr_end) return (0);

        convert_4bytes_from_GCR(gcr_ptr, header);
        convert_4bytes_from_GCR(gcr_ptr+5, header+4);
    } while ((header[0]!=0x08) || (header[2]!=sector) || (header[3]!=track));

    if ((header[5]==diskID1) || (header[4]==diskID2))
        printf(".");
    else
        printf("-");

    for (; *gcr_ptr != 0xff; gcr_ptr++) /* find sync */
        if (gcr_ptr >= gcr_end) return (0);
    for (; *gcr_ptr == 0xff; gcr_ptr++);
        if (gcr_ptr >= gcr_end) return (0);
    

    for (i = 0, sectordata = d64_sector; i < 65; i++)
    {
        convert_4bytes_from_GCR(gcr_ptr, sectordata);
        gcr_ptr += 5;
        sectordata += 4;
    }

    for (i = 1, chksum = 0; i < 258; i++)
        chksum ^= d64_sector[i];
    if (chksum  != 0)
	{
        printf("C");
		return (CHECKSUM_ERROR);
	}

    return (OK);
}




static int write_dword(FILE *fd, DWORD *buf, int num)
{
    int i;
    BYTE *tmpbuf;

    tmpbuf = malloc(num);

    for (i = 0; i < (num / 4); i++) {
        tmpbuf[i * 4] = buf[i] & 0xff;
        tmpbuf[i * 4 + 1] = (buf[i] >> 8) & 0xff;
        tmpbuf[i * 4 + 2] = (buf[i] >> 16) & 0xff;
        tmpbuf[i * 4 + 3] = (buf[i] >> 24) & 0xff;
    }

    if (fwrite((char *)tmpbuf, num, 1, fd) < 1) {
        free(tmpbuf);
        return -1;
    }
    free(tmpbuf);
    return 0;
}




void usage(void)
{
    fprintf(stderr, "Wrong number of arguments.\n"
    "Usage: g2d g64image [d64image]\n\n");
    exit (-1);
}


int main(int argc, char **argv)
{
    FILE *fdg64, *fdd64;
    char g64name[1024], d64name[1024];
    BYTE *sixpack_data;
    int track, sector;
    BYTE gcr_header[12], id[3];
    DWORD gcr_track_p[MAX_TRACKS_1541 * 2];
    DWORD gcr_speed_p[MAX_TRACKS_1541 * 2];
    BYTE gcr_track[0x2000], rawdata[260];
    BYTE errorinfo[MAXBLOCKSONDISK];
	BYTE errorcode;
	int save_errorinfo;
    BYTE *gcrptr;
	unsigned long blockindex;
	

#if defined DJGPP
    _fmode = O_BINARY;
#endif


    fprintf(stdout,
"\ng2d is a small stand-alone converter to convert a G64 disk image to\n"
"a standard D64 disk image.  Copyright 1999 Markus Brenner.\n"
"This is free software, covered by the GNU General Public License.\n"
"Version %.2f\n\n", VERSION);

    id[0]=id[1]=id[2] = '\0';


    if (argc == 2)
    {
        char *dot;
        strcpy(g64name, argv[1]);
        strcpy(d64name, g64name);
        dot = strrchr(d64name, '.');
        if (dot != NULL)
            strcpy(dot, ".d64");
        else
            strcat(d64name, ".d64");
    }
    else if (argc == 3)
    {
        strcpy(g64name, argv[1]);
        strcpy(d64name, argv[2]);
    }
    else usage();


    fdg64 = fopen(g64name, "rb");
    if (fdg64 == NULL) {
        fprintf(stderr, "Cannot open G64 image %s.\n", g64name);
        exit (-1);
    }

    fdd64 = fopen(d64name, "wb");
    if (fdd64 == NULL) {
        fprintf(stderr, "Cannot open D64 image %s.\n", d64name);
        exit (-1);
    }

    fseek(fdg64, 0x100, SEEK_SET);

    blockindex = 0;
    save_errorinfo = 0;
    for (track = 0; track < 35; track++)
    {

	/* read in one track */
        if (fread(gcr_track, 0x2000, 1, fdg64) < 1)
        {
            fprintf(stderr, "Cannot read track from G64 image.\n");
            goto fail;
        }

        printf("\nTrack: %2d - Sector: ",track+1);

        for (sector = 0; sector < sector_map_1541[track + 1]; sector++)
        {
            BYTE chksum;
            int i;

            printf("%d",sector);


            errorcode = convert_GCR_sector(gcr_track, rawdata,
                                           track + 1, sector, id[0],
                                           id[1]);

            if ((errorcode != OK) && (errorcode != CHECKSUM_ERROR))
            {
                memset(rawdata, 0x01, 260);
                rawdata[0] = 7;
                rawdata[1] = 0x4b; /* Use Original Format Pattern */
                chksum = rawdata[1];
                for (i = 1; i < 256; i++)
                    chksum ^= rawdata[i + 1];
                rawdata[257] = chksum;
            }

            errorinfo[blockindex] = errorcode;	/* OK by default */
            if (errorcode != OK) save_errorinfo = 1;

            if (fwrite((char *) rawdata+1, 256, 1, fdd64) != 1) {
                fprintf(stderr, "Cannot write sector data.\n");
                goto fail;
            }

		    blockindex++;
        }
    }

    if (save_errorinfo)
    {
        if (fwrite((char *) errorinfo, BLOCKSONDISK, 1, fdd64) != 1) {
            fprintf(stderr, "Cannot write error information.\n");
            goto fail;
        }
    }

fail:
    fclose(fdd64);
    return -1;
}
