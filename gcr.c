/* gcr.c - Group Code Recording helper functions

    (C) 2001 Markus Brenner <markus@brenner.de>
        based on code by Andreas Boose

    V 0.30   created file based on n2d
    V 0.31   improved error handling of convert_GCR_sector()
    V 0.32   removed some functions, added sector-2-GCR conversion
    V 0.33   improved sector extraction, added find_track_cycle() function
    V 0.34   added MAX_SYNC_OFFSET constant, for better error conversion
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcr.h"


char sector_map_1541[43] =
{
    0,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21,     /*  1 - 10 */
    21, 21, 21, 21, 21, 21, 21, 19, 19, 19,     /* 11 - 20 */
    19, 19, 19, 19, 18, 18, 18, 18, 18, 18,     /* 21 - 30 */
    17, 17, 17, 17, 17,                         /* 31 - 35 */
    17, 17, 17, 17, 17, 17, 17                  /* 36 - 42 (non-standard) */
};


int speed_map_1541[42] =
{
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3,               /*  1 - 10 */
    3, 3, 3, 3, 3, 3, 3, 2, 2, 2,               /* 11 - 20 */
    2, 2, 2, 2, 1, 1, 1, 1, 1, 1,               /* 21 - 30 */
    0, 0, 0, 0, 0,                              /* 31 - 35 */
    0, 0, 0, 0, 0, 0, 0                         /* 36 - 42 (non-standard) */
};


/* Nibble-to-GCR conversion table */
static BYTE GCR_conv_data[16] =
{
    0x0a, 0x0b, 0x12, 0x13,
    0x0e, 0x0f, 0x16, 0x17,
    0x09, 0x19, 0x1a, 0x1b,
    0x0d, 0x1d, 0x1e, 0x15
};


/* GCR-to-Nibble conversion tables */
static BYTE GCR_decode_high[32] =
{
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x80, 0x00, 0x10, 0xff, 0xc0, 0x40, 0x50,
    0xff, 0xff, 0x20, 0x30, 0xff, 0xf0, 0x60, 0x70,
    0xff, 0x90, 0xa0, 0xb0, 0xff, 0xd0, 0xe0, 0xff 
};

static BYTE GCR_decode_low[32] =
{
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x08, 0x00, 0x01, 0xff, 0x0c, 0x04, 0x05,
    0xff, 0xff, 0x02, 0x03, 0xff, 0x0f, 0x06, 0x07,
    0xff, 0x09, 0x0a, 0x0b, 0xff, 0x0d, 0x0e, 0xff 
};


int find_sync(BYTE **gcr_pptr, BYTE *gcr_end)
{
    while ((*gcr_pptr < gcr_end) && (**gcr_pptr != 0xff)) (*gcr_pptr)++;
    while ((*gcr_pptr < gcr_end) && (**gcr_pptr == 0xff)) (*gcr_pptr)++;
    return (*gcr_pptr < gcr_end);
}
                             

void convert_4bytes_to_GCR(BYTE *buffer, BYTE *ptr)
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


void convert_4bytes_from_GCR(BYTE *gcr, BYTE *plain)
{
    BYTE hnibble, lnibble;

    hnibble = GCR_decode_high[gcr[0] >> 3];
    lnibble = GCR_decode_low[((gcr[0] << 2) | (gcr[1] >> 6)) & 0x1f];
    *plain++ = hnibble | lnibble;

    hnibble = GCR_decode_high[(gcr[1] >> 1) & 0x1f];
    lnibble = GCR_decode_low[((gcr[1] << 4) | (gcr[2] >> 4)) & 0x1f];
    *plain++ = hnibble | lnibble;

    hnibble = GCR_decode_high[((gcr[2] << 1) | (gcr[3] >> 7)) & 0x1f];
    lnibble = GCR_decode_low[(gcr[3] >> 2) & 0x1f];
    *plain++ = hnibble | lnibble;

    hnibble = GCR_decode_high[((gcr[3] << 3) | (gcr[4] >> 5)) & 0x1f];
    lnibble = GCR_decode_low[gcr[4] & 0x1f];
    *plain++ = hnibble | lnibble;
}


int extract_id(BYTE *gcr_track, BYTE *id)
{
    BYTE header[10];
    BYTE *gcr_ptr, *gcr_end;

    int track = 18;
    int sector = 0;

    gcr_ptr = gcr_track;
    gcr_end = gcr_track+GCR_TRACK_LENGTH;

    do
    {
        if (!find_sync(&gcr_ptr, gcr_end)) return (0);

        convert_4bytes_from_GCR(gcr_ptr, header);
        convert_4bytes_from_GCR(gcr_ptr+5, header+4);
    } while ((header[0]!=0x08) || (header[2]!=sector) || (header[3]!=track));

    id[0] = header[5];
    id[1] = header[4];
    return (1);
}




int convert_GCR_sector(BYTE *gcr_start, BYTE *gcr_cycle,
                       BYTE *d64_sector,
                       int track, int sector, BYTE *id)
{
    BYTE header[10];    /* block header */
    BYTE hdr_chksum;    /* header checksum */
    BYTE blk_chksum;    /* block  checksum */
    BYTE gcr_buffer[2*GCR_TRACK_LENGTH];
    BYTE *gcr_ptr, *gcr_end, *gcr_last;
    BYTE *sectordata;
    int error_code;
    int sync_found;
    int track_len;
    int i;

    if (track > MAX_TRACK_D64) return (0);
    if ((gcr_cycle == NULL) || (gcr_cycle < gcr_start)) return (0);

    /* initialize sector data with Original Format Pattern */
    memset(d64_sector, 0x01, 260);
    d64_sector[0] = 0x07; /* Block header mark */
    d64_sector[1] = 0x4b; /* Use Original Format Pattern */
    for (blk_chksum = 0, i = 1; i < 257; i++)
        blk_chksum ^= d64_sector[i + 1];
    d64_sector[257] = blk_chksum;

    /* copy to  temp. buffer with twice the track data */
    track_len = gcr_cycle - gcr_start;
    memcpy(gcr_buffer, gcr_start, track_len);
    memcpy(gcr_buffer+track_len, gcr_start, track_len);
    track_len *= 2;

    /* Check for at least one Sync */
    gcr_end = gcr_buffer+track_len;
    sync_found = 0;
    for (gcr_ptr = gcr_buffer; gcr_ptr < gcr_end; gcr_ptr++)
    {
        if (*gcr_ptr == 0xff)
            if (sync_found < 2) sync_found++;
        else /* (*gcr_ptr != 0xff) */
        {
            if (sync_found < 2) sync_found = 0;
            else sync_found = 3;
        }
    }
    if (sync_found != 3) return(SYNC_NOT_FOUND);


    /* Check for missing SYNCs */
    gcr_last = gcr_ptr = gcr_buffer;
    while (gcr_ptr < gcr_end)
    {
        find_sync(&gcr_ptr, gcr_end);
        if ((gcr_ptr-gcr_last) > MAX_SYNC_OFFSET)
            return (SYNC_NOT_FOUND);
        else
            gcr_last = gcr_ptr;
    }


    /* Try to find block header for Track/Sector */
    gcr_ptr = gcr_buffer;
    gcr_end = gcr_buffer+track_len;
    do
    {
        if (!find_sync(&gcr_ptr, gcr_end)) return (HEADER_NOT_FOUND);
        if (gcr_ptr >= gcr_end - 10) return (HEADER_NOT_FOUND);
        convert_4bytes_from_GCR(gcr_ptr, header);
        convert_4bytes_from_GCR(gcr_ptr+5, header+4);
        gcr_ptr++;
    } while ((header[0]!=0x08) || (header[2]!=sector) || (header[3]!=track));

    error_code = OK;

    /* Header checksum */
    hdr_chksum = 0;
    for (i = 1; i < 6; i++)
        hdr_chksum = hdr_chksum ^ header[i];
    if (hdr_chksum != 0)
        error_code = (error_code == OK) ? BAD_HEADER_CHECKSUM : error_code;


    /* check for correct disk ID */
    if ((header[5]!=id[0]) || (header[4]!=id[1]))
        error_code = (error_code == OK) ? ID_MISMATCH : error_code;

    if (!find_sync(&gcr_ptr, gcr_end)) return (DATA_NOT_FOUND);

    for (i = 0, sectordata = d64_sector; i < 65; i++)
    {
        if (gcr_ptr >= gcr_end-5) return (DATA_NOT_FOUND);
        convert_4bytes_from_GCR(gcr_ptr, sectordata);
        gcr_ptr += 5;
        sectordata += 4;
    }


    /* check for Block header mark */
    if (d64_sector[0] != 0x07)
        error_code = (error_code == OK) ? DATA_NOT_FOUND : error_code;


    /* Block checksum */
    for (i = 1, blk_chksum = 0; i < 258; i++)
        blk_chksum ^= d64_sector[i];

    if (blk_chksum  != 0)
        error_code = (error_code == OK) ? BAD_DATA_CHECKSUM : error_code;

    return (error_code);
}


void convert_sector_to_GCR(BYTE *buffer, BYTE *ptr,
                                  int track, int sector, BYTE *diskID)
{
    int i;
    BYTE buf[4];

    memset(ptr, 0xff, 5);       /* Sync */
    ptr += 5;

    buf[0] = 0x08;              /* Header identifier */
    buf[1] = sector ^ track ^ diskID[1] ^ diskID[0];
    buf[2] = sector;
    buf[3] = track;
    convert_4bytes_to_GCR(buf, ptr);
    ptr += 5;

    buf[0] = diskID[1];
    buf[1] = diskID[2];
    buf[2] = buf[3] = 0x0f;
    convert_4bytes_to_GCR(buf, ptr);
    ptr += 5;

    memset(ptr, 0x55, 9);       /* Header Gap */
    ptr += 9;

    memset(ptr, 0xff, 5);       /* Sync */
    ptr += 5;

    for (i = 0; i < 65; i++) {
        convert_4bytes_to_GCR(buffer, ptr);
        buffer += 4;
        ptr += 5;
    }

    /* FIXME: This is approximated.  */
    memset(ptr, 0x55, 6);       /* Gap before next sector.  */
    ptr += 6;

}


/*
BYTE* find_track_cycle2(BYTE *start_pos)
{
    BYTE *pos;
    BYTE *cycle_pos;
    BYTE *stop_pos;

    pos = start_pos + MIN_TRACK_LENGTH;
    stop_pos = start_pos+GCR_TRACK_LENGTH-50;
    cycle_pos = NULL;

    printf("try2!\n");

    while (pos < stop_pos)
    {
		if (memcmp(start_pos, pos, 50) == 0)
        {
            printf("cycle2: %x\n", pos-start_pos);
            cycle_pos = pos;
            break;
        }
		pos++;
    }

    return (cycle_pos);
}
*/



BYTE* find_track_cycle(BYTE *start_pos)
{
    BYTE *sync_pos;
    BYTE *cycle_pos;
    BYTE *stop_pos;
    BYTE *p1, *p2;
    BYTE *cycle_try;

    sync_pos = start_pos + MIN_TRACK_LENGTH;
    stop_pos = start_pos+GCR_TRACK_LENGTH-MATCH_LENGTH; // -MATCH_LENGTH;
    cycle_pos = NULL;

    /* try to find next sync */
    while (find_sync(&sync_pos, stop_pos))
    {
        /* found a sync, now let's see if data matches */
        p1 = start_pos;
        cycle_try = sync_pos;
        for (p2 = sync_pos; p2 < stop_pos;)
        {
            /* try to match all remaining syncs, too */
            if (memcmp(p1, p2, MATCH_LENGTH) != 0)
            {
                cycle_try = NULL;
                break;
            }
//            printf("c");
            if (!find_sync(&p1, stop_pos)) break;
            if (!find_sync(&p2, stop_pos)) break;
        }

        if (cycle_try != NULL)
        {
//            printf("cycle: %x\n", cycle_try-start_pos);
            cycle_pos = cycle_try;
            break;
        }
          
//        cycle_pos = (cycle_try != NULL) ? cycle_try : cycle_pos; 
    }

//    if (cycle_pos == NULL)
//        cycle_pos = find_track_cycle2(start_pos);

    return (cycle_pos);
}
