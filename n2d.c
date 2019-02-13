/* n2d.c - Converts mnib nibbler data to D64 image

    (C) 2000,01 Markus Brenner <markus@brenner.de>

    Based on code by Andreas Boose <boose@unixserv.rz.fh-hannover.de>

    V 0.19   fixed usage message, only use 35 tracks for now
    V 0.20   improved disk error detection
    V 0.21   split program in n2d.c and gcr.h/gcr.c
    V 0.22   added halftrack-image support
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcr.h"


#define VERSION 0.22


void usage(void)
{
    fprintf(stderr, "Usage: n2d data [d64image]\n\n");
    exit (-1);
}


int main(int argc, char **argv)
{
    FILE *fp_nib, *fp_d64;
    char nibname[1024], d64name[1024];
    int track, sector;
    BYTE id[3];
    BYTE gcr_track[GCR_TRACK_LENGTH], rawdata[260];
    BYTE errorinfo[MAXBLOCKSONDISK];
    BYTE errorcode;
    int save_errorinfo;
    int blockindex;
    BYTE nib_header[0x100];
    int header_offset;
	

    fprintf(stdout,
"\ng2d is a small stand-alone converter to convert a G64 disk image to\n"
"a standard D64 disk image.  Copyright 1999 Markus Brenner.\n"
"This is free software, covered by the GNU General Public License.\n"
"Version %.2f\n\n", VERSION);

    if (argc == 2)
    {
        char *dot;
        strcpy(nibname, argv[1]);
        strcpy(d64name, nibname);
        dot = strrchr(d64name, '.');
        if (dot != NULL)
            strcpy(dot, ".d64");
        else
            strcat(d64name, ".d64");
    }
    else if (argc == 3)
    {
        strcpy(nibname, argv[1]);
        strcpy(d64name, argv[2]);
    }
    else usage();

    fp_nib = fopen(nibname, "rb");
    if (fp_nib == NULL)
    {
        fprintf(stderr, "Cannot open G64 image %s.\n", nibname);
        exit (-1);
    }

    fp_d64 = fopen(d64name, "wb");
    if (fp_d64 == NULL)
    {
        fprintf(stderr, "Cannot open D64 image %s.\n", d64name);
        exit (-1);
    }

    if (fread(nib_header, sizeof(BYTE), 0x0100, fp_nib) < 0x0100)
    {
        fprintf(stderr, "Cannot read nibble image header.\n");
        goto fail;
    }

    /* figure out the disk ID from Track 18, Sector 0 */
    id[0]=id[1]=id[2] = '\0';
    if (nib_header[0x10+17*2] == 18*2) /* normal nibble file */
        fseek(fp_nib, 17*GCR_TRACK_LENGTH+0x100, SEEK_SET);
    else /* halftrack nibble file */
        fseek(fp_nib, 2*17*GCR_TRACK_LENGTH+0x100, SEEK_SET);

    if (fread(gcr_track, sizeof(BYTE), GCR_TRACK_LENGTH, fp_nib) < GCR_TRACK_LENGTH)
    {
        fprintf(stderr, "Cannot read track from G64 image.\n");
        goto fail;
    }
    if (!extract_id(gcr_track, id))
    {
        fprintf(stderr, "Cannot find directory sector.\n");
        goto fail;
    }
    printf("ID: %2x %2x\n", id[0], id[1]);

    /* reset file pointer to first track */
    fseek(fp_nib, 0x100, SEEK_SET);

    blockindex = 0;
    save_errorinfo = 0;
    header_offset = 0x10; /* number of first nibble-track in nib image */
    for (track = 0; track < 35; track++)
    {
        /* Skip halftracks if present in image */
        if (nib_header[header_offset] < (track + 1) * 2)
        {
            fseek(fp_nib, GCR_TRACK_LENGTH, SEEK_CUR); 
            header_offset += 2;
        }
        header_offset += 2;

	/* read in one track */
        if (fread(gcr_track, sizeof(BYTE), GCR_TRACK_LENGTH, fp_nib) < GCR_TRACK_LENGTH)
        {
            fprintf(stderr, "Cannot read track from G64 image.\n");
            goto fail;
        }

        printf("\nTrack: %2d - Sector: ",track+1);

        for (sector = 0; sector < sector_map_1541[track + 1]; sector++)
        {
            printf("%d",sector);

            errorcode = convert_GCR_sector(gcr_track, rawdata,
                                           track + 1, sector, id);

            errorinfo[blockindex] = errorcode;	/* OK by default */
            if (errorcode != OK) save_errorinfo = 1;
          
            if (errorcode == OK)
                printf(" ");
            else
                printf("%d",errorcode);

            if (fwrite((char *) rawdata+1, 256, 1, fp_d64) != 1)
            {
                fprintf(stderr, "Cannot write sector data.\n");
                goto fail;
            }

            blockindex++;
        }
    }

    /* Missing: Track 36-40 detection */

    if (save_errorinfo)
    {
        if (fwrite((char *) errorinfo, BLOCKSONDISK, 1, fp_d64) != 1)
        {
            fprintf(stderr, "Cannot write error information.\n");
            goto fail;
        }
    }

fail:
    fclose(fp_d64);
    return -1;
}
