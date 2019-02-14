/* gcr.h - Group Code Recording helper functions

    (C) 2001 Markus Brenner <markus@brenner.de>
        based on code by Andreas Boose

    V 0.33   improved sector extraction, added find_track_cycle() function
    V 0.34   added MAX_SYNC_OFFSET constant, approximated to 800 GCR bytes
*/

#ifndef _GCR_
#define _GCR_


#define BYTE unsigned char
#define DWORD unsigned int
#define MAX_TRACKS_1541 42

/* D64 constants */
#define BLOCKSONDISK 683
#define BLOCKSEXTRA 85
#define MAXBLOCKSONDISK (BLOCKSONDISK+BLOCKSEXTRA)
#define MAX_TRACK_D64 40

/* NIB format constants */
#define GCR_TRACK_LENGTH 0x2000

/* Conversion routines constants */
#define MIN_TRACK_LENGTH 0x1780
#define MATCH_LENGTH 7
/* number of GCR bytes until NO SYNC error
   timer counts down from $d000 to $8000 ($20480 cycles)
   until timeout when waiting for a SYNC signal
   This is approx. 20.48 ms, which is approx 1/10th disk revolution
   8000 GCR bytes / 10 = 800 bytes */
#define MAX_SYNC_OFFSET 800

/* Disk Controller error codes */
#define OK                  0x01
#define HEADER_NOT_FOUND    0x02
#define SYNC_NOT_FOUND      0x03
#define DATA_NOT_FOUND      0x04
#define BAD_DATA_CHECKSUM   0x05
#define VERIFY_ERROR        0x07
#define WRITE_PROTECTED     0x08
#define BAD_HEADER_CHECKSUM 0x09
#define ID_MISMATCH         0x0b
#define DISK_NOT_INSERTED   0x0f


extern char sector_map_1541[];

extern int speed_map_1541[];


void convert_4bytes_to_GCR(BYTE *buffer, BYTE *ptr);

void convert_4bytes_from_GCR(BYTE *gcr, BYTE *plain);

int extract_id(BYTE *gcr_track, BYTE *id);

BYTE* find_track_cycle(BYTE *start_pos);

int convert_GCR_sector(BYTE *gcr_start, BYTE *gcr_end,
                       BYTE *d64_sector,
                       int track, int sector, BYTE *id);

void convert_sector_to_GCR(BYTE *buffer, BYTE *ptr,
                                  int track, int sector, BYTE *diskID);


#endif
