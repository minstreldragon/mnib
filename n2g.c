/*
 * n2g.c - Converts nibbler data to G64 image
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


BYTE *find_sync(BYTE *pointer, int pos)
{
    /* first find a Sync byte $ff */
    for (; (*pointer != 0xff) && (pos < 0x2000); pointer++, pos++);
    if (pos >= 0x2000) return (NULL);

    /* now find end of Sync */
    for (; (*pointer == 0xff) && (pos < 0x2000); pointer++, pos++);
    if (pos >= 0x2000) return (NULL);

    return (pointer);
}


int is_sector_zero(BYTE *data)
{
    if ((data[0] == 0x52)
        && ((data[2] & 0x0f) == 0x05)
        && ((data[3] & 0xfc) == 0x28))
         return (1);
    else return (0);
}


DWORD extract_track(BYTE *mnib_track, BYTE *gcr_track)
{
/*
    int raw_track_size[4] = { 0x1875, 0x1a14, 0x1bef, 0x1e0e };
*/
    BYTE *sync_pos, *last_sync_pos;
    BYTE *start_pos;
    BYTE *first_sync_pos;
    BYTE *sector_zero_pos;
    BYTE *max_len_pos;
    int block_len, max_block_len;
    int sector_zero_len;
    int syncs;
    int i;
    int cyclepos;


    sector_zero_pos = NULL;
    sector_zero_len = 0;
    max_len_pos = mnib_track;
    max_block_len = 0;
    syncs = 0;
    cyclepos = 0;

    for (sync_pos = mnib_track; sync_pos != NULL;)
    {
        last_sync_pos = sync_pos;
        start_pos = mnib_track;
        syncs++; /* count number of syncs in track */
        printf(".");

        /* find start of next block */
        sync_pos = find_sync(sync_pos, sync_pos-mnib_track);

        /* if we can't find beginning repeated data we have a problem... */
        if (sync_pos == NULL) return (0);

        /* check if sector 0 header was found */
        if (is_sector_zero(sync_pos))
        {
            printf("0");
            sector_zero_pos = sync_pos;
            sector_zero_len = sync_pos - last_sync_pos;
        }

        /* check if the last chunk of data had maximal length */
        block_len = sync_pos - last_sync_pos;
        if (block_len > max_block_len) printf("?");
        max_len_pos  = (block_len > max_block_len) ? sync_pos  : max_len_pos;
        max_block_len = (block_len > max_block_len) ? block_len : max_block_len;

        /* check if we are still in first disk rotation */
        if ((sync_pos-mnib_track) < 0x1780) continue;

        /* we are possibly already in the second rotation, check for repeat */
        while(sync_pos != NULL)
        {
            printf("!");

            for (i = 0; i < 7; i++)
            {
                if (start_pos[i] != sync_pos[i]) break;
            }
            if (i != 7) break; /* break out of while loop */
            if (cyclepos == 0)
            {
                cyclepos = (sync_pos - start_pos);
                printf("cycle: %d\n", cyclepos);
            }
            sync_pos  = find_sync(sync_pos, sync_pos-mnib_track);
            start_pos = find_sync(start_pos, start_pos-mnib_track);

            /* check if next header is completely available */
            if ((sync_pos-mnib_track+10) > 0x2000) sync_pos = NULL;
        }
    }

    if ((sector_zero_len != 0) && ((sector_zero_len + 0x40) >= max_block_len))
    {
        max_len_pos = sector_zero_pos;
    }

    printf("Startpos:  %d\n", max_len_pos-mnib_track);
    printf("Cyclepos:  %d\n", cyclepos);


    /* find start of sync */
    sync_pos = max_len_pos;
    do
    {
        sync_pos--;
        if (sync_pos < mnib_track) sync_pos += cyclepos;
    } while (*sync_pos == 0xff);
    sync_pos++;
    if (sync_pos >= mnib_track+cyclepos) sync_pos = mnib_track;
    max_len_pos = sync_pos;


    /* here comes the actual copy loop */
    for (sync_pos = max_len_pos; sync_pos < mnib_track+cyclepos; )
        *gcr_track++ = *sync_pos++;

    for (sync_pos = mnib_track; sync_pos < max_len_pos; )
        *gcr_track++ = *sync_pos++;

    return (cyclepos);
}


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


void SetFileExtension(char *str, char *ext)
{
        // sets the file extension of String *str to *ext
        int i, len;

        for (i=0, len = strlen(str); (i < len) && (str[i] != '.'); i++);
        str[i] = '\0';
        strcat(str, ext);
}


void usage(void)
{
    fprintf(stderr, "Wrong number of arguments.\n"
    "Usage: n2g data [d64image]\n\n");
    exit (-1);
}


int main(int argc, char **argv)
{
    FILE *fpin, *fpout;
    char inname[80], outname[80];
    BYTE *nibbler_data;
    int track, sector;
    BYTE gcr_header[12], id[3];
    DWORD gcr_track_p[MAX_TRACKS_1541 * 2];
    DWORD gcr_speed_p[MAX_TRACKS_1541 * 2];
    DWORD track_len;
    BYTE mnib_track[0x2000];
    BYTE gcr_track[7930];
    BYTE errorinfo[MAXBLOCKSONDISK];
    BYTE errorcode;
    int save_errorinfo;
    BYTE *gcrptr;
    unsigned long blockindex;
    int mnib_header[0x100];
	

#if defined DJGPP
    _fmode = O_BINARY;
#endif


    fprintf(stdout,
"\nn2g is a small stand-alone converter to convert mnib data to\n"
"a standard G64 disk image.  Copyright 2000 Markus Brenner.\n"
"This is free software, covered by the GNU General Public License.\n"
"Version %.2f\n\n", VERSION);

    id[0]=id[1]=id[2] = '\0';


    if (argc == 2)
    {
        strcpy(inname, argv[1]);
        strcpy(outname, inname);
    }
    else if (argc == 3)
    {
        strcpy(inname, argv[1]);
        strcpy(outname, argv[2]);
    }
    else usage();

/*
    SetFileExtension(inname, "NIB");
*/
    SetFileExtension(outname, ".G64");


    fpin = fopen(inname, "rb");
    if (fpin == NULL) {
        fprintf(stderr, "Cannot open mnib image %s.\n", inname);
        exit (-1);
    }

    fpout = fopen(outname, "wb");
    if (fpout == NULL) {
        fprintf(stderr, "Cannot open G64 image %s.\n", outname);
        exit (-1);
    }

    if (fread(mnib_header, 0x100, 1, fpin) < 1)
    {
        fprintf(stderr, "Cannot read header from mnib image.\n");
        goto fail;
    }

    fseek(fpout, 0, SEEK_SET);

    /* Create G64 header */
    strcpy((char *) gcr_header, "GCR-1541");
    gcr_header[8] = 0;
    gcr_header[9] = MAX_TRACKS_1541 * 2;
    gcr_header[10] = 7928 % 256;
    gcr_header[11] = 7928 / 256;

    if (fwrite((char *)gcr_header, sizeof(gcr_header), 1, fpout) != 1) {
        fprintf(stderr, "Cannot write G64 header.\n");
        goto fail;
    }

    /* Create index and speed tables */
    for (track = 0; track < MAX_TRACKS_1541; track++) {
        gcr_track_p[track * 2] = 12 + MAX_TRACKS_1541 * 16 + track * 7930;
        gcr_track_p[track * 2 + 1] = 0;
        gcr_speed_p[track * 2] = speed_map_1541[track];
        gcr_speed_p[track * 2 + 1] = 0;
    }

    if (write_dword(fpout, gcr_track_p, sizeof(gcr_track_p)) < 0) {
        fprintf(stderr, "Cannot write track header.\n");
        goto fail;
    }
    if (write_dword(fpout, gcr_speed_p, sizeof(gcr_speed_p)) < 0) {
        fprintf(stderr, "Cannot write speed header.\n");
        goto fail;
    }


    for (track = 0; track < MAX_TRACKS_1541; track++)
    {
        int raw_track_size[4] = { 6250, 6666, 7142, 7692 };

        memset(&gcr_track[2], 0xff, 7928);
        gcr_track[0] = raw_track_size[speed_map_1541[track]] % 256;
        gcr_track[1] = raw_track_size[speed_map_1541[track]] / 256;

        if (track < 35)
        {
	    /* read in one track */
            if (fread(mnib_track, 0x2000, 1, fpin) < 1)
            {
                fprintf(stderr, "Cannot read track from mnib image.\n");
                goto fail;
            }
    
            printf("\nTrack: %2d ",track+1);
            track_len = extract_track(mnib_track, gcr_track+2);
            gcr_track[0] = track_len % 256;
            gcr_track[1] = track_len / 256;
            if (fwrite((char *) gcr_track, sizeof(gcr_track), 1, fpout) != 1)
            {
                fprintf(stderr, "Cannot write track data.\n");
                goto fail;
            }
        }
        else
        {
            /* track > 35: write blank track */
            printf("\nTrack: %2d ",track+1);
            gcr_track[0] = 0 % 256;
            gcr_track[1] = 0 / 256;
            if (fwrite((char *) gcr_track, sizeof(gcr_track), 1, fpout) != 1)
            {
                fprintf(stderr, "Cannot write track data.\n");
                goto fail;
            }
        }

    
    }


fail:
    fclose(fpin);
    fclose(fpout);
    return -1;
}
