/* mnib - Markus' G64 nibbler

    (C) 2000,01 Markus Brenner

    V 0.10   implementation of serial and parallel protocol
    V 0.11   first density scan and nibbler functionality
    V 0.12   automatic port detection and improved program flow
    V 0.13   extended data output format
    V 0.14   fixed parallel port support
    V 0.15   2nd try for parallel port fix
    V 0.16   next try with adjustments to ECP ports
    V 0.17   added automatic drive type detection
    V 0.18   added 41 track support
    V 0.19   added Density and Halftrack command switches
    V 0.20   added Bump and Reset options
    V 0.21   added timeout routine for nibble transfer
    V 0.22   added flush command during reading
    V 0.23   disable interrupts during serial protocol
    V 0.24   improved serial protocol
    V 0.25   got rid of some more msleep()s
    V 0.26   added 'S' track reading (read without waiting for Sync)
    V 0.27   added hidden 'g' switch for GEOS 1.2 disk image
    V 0.28   improved killer detection (changed retries $80 -> $c0)
    V 0.29   added direct D64 nibble functionality
    V 0.30   added D64 error correction by multiple read
    V 0.31   added 40 track support for D64 images
    V 0.32   bin-include bn_flop.prg  (bin2h bn_flop.prg floppy_code bn_flop.h)
    V 0.33   improved D64 mode
    V 0.33a  VCFe3 release (35 track flag)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>
#include "cbm.h"
#include "gcr.h"
#include "bn_flop.h"        /* floppy code: unsigned char floppy_code[] */

#define VERSION 0.33
#define FD 1                /* (unused) file number for cbm_routines */

#define FL_STEPTO      0x00
#define FL_MOTOR       0x01
#define FL_RESET       0x02
#define FL_READNORMAL  0x03
#define FL_DENSITY     0x05
#define FL_SCANKILLER  0x06
#define FL_SCANDENSITY 0x07
#define FL_READWOSYNC  0x08
#define FL_TEST        0x0a
#define FL_VERIFY_CODE 0x10

#define DISK_NORMAL    0
#define DISK_GEOS      1

#define IMAGE_NIB      0    /* destination image format */
#define IMAGE_D64      1
#define IMAGE_G64      2

static int start_track;
static int end_track;
static int track_inc;
static int use_default_density;
static int no_extra_tracks;
static int current_track;
static unsigned int lpt[4];
static int lpt_num;
static int drivetype;
static unsigned int floppybytes;
static int disktype;
static int imagetype;

char bitrate_range[4] =
{ 43*2, 31*2, 25*2, 18*2 };

char bitrate_value[4] =
{ 0x00, 0x20, 0x40, 0x60 };

static BYTE density_branch[4] =
{ 0xb1, 0xb5, 0xb7, 0xb9 };

BYTE track_density[84];


void usage(void)
{
    fprintf(stderr, "usage: mnib <output>\n");
    fprintf(stderr, " -b: Bump before reading\n");
    fprintf(stderr, " -d: Use scanned density\n");
    fprintf(stderr, " -h: Add Halftracks\n");
    fprintf(stderr, " -r: Reset Drives\n");
    fprintf(stderr, " -35: 35 tracks only\n");

    exit(1);
}


int compare_extension(char *filename, char *extension)
{
    char *dot;

    dot = strrchr(filename, '.');
    if (dot == NULL) return (0);

    for (++dot; *dot != '\0'; dot++, extension++)
        if (tolower(*dot) != tolower(*extension)) return (0);

    if (*extension == '\0') return (1);
    else return (0);
}


void upload_code(char *floppyfile)
{
    unsigned int databytes, dataread;
    unsigned int start;
    int i;

    /* patchdata if using 1571 drive */
    unsigned int patch_pos[9] =
    { 0x72, 0x89, 0x9e, 0x1da, 0x224, 0x258, 0x262, 0x293, 0x2a6 };


    databytes = sizeof(floppy_code);

    start = floppy_code[0] + (floppy_code[1] << 8);
    printf("Startadress: $%04x\n",start);

    /* patch code if using 1571 drive */
    if (drivetype == 1571)
    {
        for (i = 0; i < 9; i++)
        {
            if (floppy_code[patch_pos[i]] != 0x18)
                printf("Possibly bad patch at %04x!\n",patch_pos[i]);
            floppy_code[patch_pos[i]] = 0x40;
        }
    }

    cbm_upload(FD, 8, start, floppy_code+2, databytes-2); 

    floppybytes = databytes;
}


void send_par_cmd(BYTE cmd)
{
    cbm_par_write(FD, 0x00);
    cbm_par_write(FD, 0x55);
    cbm_par_write(FD, 0xaa);
    cbm_par_write(FD, 0xff);
    cbm_par_write(FD, cmd);
}

int test_par_port()
{
    int i;
    int byte;
    int rv;
    int test[0x100];

    send_par_cmd(FL_TEST);
    for (i = 0, rv = 1; i < 0x100; i++)
    {
/*
        printf("%02x ", byte = cbm_par_read(FD));
        if (byte != i) rv = 0;
*/
        if (cbm_par_read(FD) != i) rv = 0;
    }
    if (cbm_par_read(FD) != 0) rv = 0;
    return rv;
}

int verify_floppy()
{
    int i, rv;

    send_par_cmd(FL_VERIFY_CODE);
    for (i = 2, rv = 1; i < floppybytes; i++)
    {
        if (cbm_par_read(FD) != floppy_code[i])
        {
            rv = 0;
            printf("diff: %d\n", i);
        }
    }
    for (; i < 0x0800-0x0300+2; i++)
        cbm_par_read(FD);

    if (cbm_par_read(FD) != 0) rv = 0;
    return rv;
}

int find_par_port()
{
    int i;
    for (i = 0; set_par_port(i); i++)
    {
        if (test_par_port())
        {
            printf(" Found!\n");
            return (1);
        }
        printf(" no\n");
    }
    return (0); /* no parallel port found */
}

int set_full_track()
{
    send_par_cmd(FL_MOTOR);
    cbm_par_write(FD, 0xfc); /* $1c00 CLEAR mask (clear stepper bits) */
    cbm_par_write(FD, 0x02); /* $1c00  SET  mask (stepper bits = %10) */
    cbm_par_read(FD);
    delay(500); /* wait for motor to step */
}

int motor_on()
{
    send_par_cmd(FL_MOTOR);
    cbm_par_write(FD, 0xf3); /* $1c00 CLEAR mask */
    cbm_par_write(FD, 0x0c); /* $1c00  SET  mask (LED + motor ON) */
    cbm_par_read(FD);
    delay(500); /* wait for motor to turn on */
}

int motor_off()
{
    send_par_cmd(FL_MOTOR);
    cbm_par_write(FD, 0xf3); /* $1c00 CLEAR mask */
    cbm_par_write(FD, 0x00); /* $1c00  SET  mask (LED + motor OFF) */
    cbm_par_read(FD);
    delay(500); /* wait for motor to turn on */
}

int step_to_halftrack(int halftrack)
{
    send_par_cmd(FL_STEPTO);
    cbm_par_write(FD, (halftrack != 0) ? halftrack : 1);
//    cbm_par_write(FD, (halftrack != 0) ? halftrack/2 : 1);
    cbm_par_read(FD);
}

int reset_floppy()
{
    BYTE cmd[80];

    motor_on();
    step_to_halftrack(36);
    send_par_cmd(FL_RESET);
    printf("drive reset...\n");
    delay(5000);
    cbm_listen(FD,8,15);
    cbm_write(FD,"I",1);
    cbm_unlisten(FD);
    delay(5000);
    sprintf(cmd,"M-E%c%c",0x00,0x03);
    cbm_listen(FD,8,15);
    cbm_write(FD,cmd,5);
    cbm_unlisten(FD);
    cbm_par_read(FD);
}

int set_bitrate(int density) /* $13d6 */
{
    send_par_cmd(FL_MOTOR);
    cbm_par_write(FD, 0x9f);                   /* $1c00 CLEAR mask */
    cbm_par_write(FD, bitrate_value[density]); /* $1c00  SET  mask */
    cbm_par_read(FD);
    return(density);
}

int set_default_bitrate(int track) /* $13bc */
{
    BYTE density;

    for (density = 3; track >= bitrate_range[density]; density--);
/*
    printf("default: %2x ", bitrate_value[density]);
*/
    send_par_cmd(FL_DENSITY);
    cbm_par_write(FD, density_branch[density]);
    cbm_par_write(FD, 0x9f);                   /* $1c00 CLEAR mask */
    cbm_par_write(FD, bitrate_value[density]); /* $1c00  SET  mask */
    cbm_par_read(FD);
    return(density);
}


int scan_track(int track) /* $152b Density Scan*/
{
    BYTE density;
    BYTE killer_info;
    int i, bin;
    BYTE count;
    unsigned int goodbest, statbest;
    unsigned int goodmax, statmax;

    unsigned int density_statistics[4];
    unsigned int density_isgood[4];


    density = set_default_bitrate(track);
    send_par_cmd(FL_SCANKILLER); /* scan for killer track */
    killer_info = cbm_par_read(FD);
    if (killer_info & 0x80) return (density | killer_info);
    set_bitrate(2);

    for (bin = 0; bin < 4; bin++)
        density_isgood[bin] = density_statistics[bin] = 0;

    for (i = 0; i < 6; i++)
    {
        send_par_cmd(FL_SCANDENSITY);
        for (bin = 3; bin >= 0; bin--)
        {
            count = cbm_par_read(FD);
            if (count >= 40) density_isgood[bin]++;
            density_statistics[bin] += count;
        }
        cbm_par_read(FD);
    }

    goodmax = 0;
    statmax = 0;
    for (bin = 0; bin < 4; bin++)
    {
        if (density_isgood[bin] > goodmax)
        {
            goodmax = density_isgood[bin];
            goodbest = bin;
        }
        if (density_statistics[bin] > statmax)
        {
            statmax = density_statistics[bin];
            statbest = bin;
        }
    }

    density = (goodmax > 0) ? goodbest : statbest;

    set_bitrate(density);
    send_par_cmd(FL_SCANKILLER); /* scan for killer track */
    killer_info = cbm_par_read(FD);

    return(density | killer_info);
}


int scan_density(void)
{
    int track;
    int density;

    if (!test_par_port()) return 0;
//    reset_floppy();
    printf("SCAN\n");  
    motor_on();
    for (track = start_track; track <= end_track; track += track_inc)
    {
        step_to_halftrack(track);
        printf("\n%02d: ",track);
        density = scan_track(track);
        track_density[track] = density;
        if (density & 0x80) printf("F");
        else if (density & 0x40) printf("S");
        else printf("%d", (density & 3));

    }
}



int read_halftrack(int halftrack, BYTE *buffer)
{
    int density, defdensity;
    int scanned_density;
    int timeout;
    int byte;
    int i;

    step_to_halftrack(halftrack);
    printf("\n%4.1f: ",(float)halftrack/2);
    for (defdensity = 3; halftrack >= bitrate_range[defdensity]; defdensity--);
    printf("(%d) ", (defdensity & 3));

    scanned_density = scan_track(halftrack);
    if (scanned_density & 0x80)
    {
        /* killer track */
        printf("F");
        memset(buffer, 0xff, 0x2000);
        return (0x80);
    }
    else if (scanned_density & 0x40)
    {
        /* no sync found */
        printf("S");
    }
    else printf("%d", (scanned_density & 3));

    density = (use_default_density || (scanned_density & 0x40))
              ? defdensity : (scanned_density & 3);

    if ((disktype == DISK_GEOS) && (halftrack == 36*2 ))
    {
        printf(" GEOS!");
        density = 3;
    }

    printf(" -> %d", density);

    do
    {
        send_par_cmd(FL_DENSITY);
        cbm_par_write(FD, density_branch[density]);
        cbm_par_write(FD, 0x9f);                   /* $1c00 CLEAR mask */
        cbm_par_write(FD, bitrate_value[density]); /* $1c00  SET  mask */
        cbm_par_read(FD);

        fflush(NULL);

        disable();
         
        if (scanned_density & 0x40)
            send_par_cmd(FL_READWOSYNC);
        else
            send_par_cmd(FL_READNORMAL);
        cbm_par_read(FD);

        timeout = 0;
        for (i = 0; i < 0x2000; i+=2)
        {
            byte = cbm_nib_read1(FD);
            if (byte < 0)
            {
                timeout = 1;
                break;
            }
            buffer[i] = byte;
            byte = cbm_nib_read2(FD);
            if (byte < 0)
            {
                timeout = 1;
                break;
            }
            buffer[i+1] = byte;
        }
        enable();
        if (timeout)
        {
            printf("r");
            printf("%02x\n", cbm_par_read(FD));
            delay(500);
            printf(".\n");
            printf("%02x\n", cbm_par_read(FD));
            delay(500);
            printf("%02x ", cbm_par_read(FD));
            delay(500);
            printf("%02x ", cbm_par_read(FD));
            fprintf(stderr, "%s", test_par_port() ? "+" : "-");
        }
    } while (timeout);

    cbm_par_read(FD);
    return (density);
}


int readdisk(FILE *fpout, char *track_header)
{
    int track;
    int density;
    int header_entry;
    BYTE buffer[0x2100];
    int i;

    header_entry = 0;
    for (track = start_track; track <= end_track; track += track_inc)
    {
        density = read_halftrack(track, buffer);
        track_header[header_entry*2] = track;

        if (density & 0x80)
            track_header[header_entry*2+1] = 'F';
        else if (density & 0x40)
            track_header[header_entry*2+1] = 'S';
        else
            track_header[header_entry*2+1] = density;

        header_entry++;

        /* process and save track to disk */
        for (i = 0; i < 0x2000; i++)
            fputc(buffer[i], fpout);
    }
    step_to_halftrack(4*2);
}



int read_d64(FILE *fpout)
{
    int density;
    int track, sector;
    int csec; /* compare sector variable */
    int blockindex;
    int save_errorinfo;
    int save_40_errors;
    int save_40_tracks;
    int retry;
    BYTE buffer[0x2100];
    BYTE* gcr_cycle;
    BYTE id[3];
    BYTE rawdata[260];
    BYTE sectordata[16*21*260];
    BYTE d64data[MAXBLOCKSONDISK*256];
    BYTE *d64ptr;
    BYTE errorinfo[MAXBLOCKSONDISK];
    BYTE errorcode;
    int sector_count[21];     /* number of different sector data read */
    int sector_occur[21][16]; /* how many times was this sector data read? */
    int sector_error[21][16]; /* type of error on this sector data */
    int sector_use[21];       /* best data for this sector so far */
    int sector_max[21];       /* # of times the best sector data has occured */
    int goodtrack;
    int goodsector;
    int any_sectors;           /* any valid sectors on track at all? */
    int blocks_to_save;



    blockindex = 0;
    save_errorinfo = 0;
    save_40_errors = 0;
    save_40_tracks = 0;

    density = read_halftrack(18*2, buffer);
    if (!extract_id(buffer, id))
    {
        fprintf(stderr, "Cannot find directory sector.\n");
        return (0);
    }

    d64ptr = d64data;
    for (track = 1; track <= 40; track += 1)
    {
        /* no sector data read in yet */
        for (sector = 0; sector < 21; sector++)
            sector_count[sector] = 0;

        any_sectors = 0;
        for (retry = 0; retry < 16; retry++)
        {
            goodtrack = 1;
            read_halftrack(2*track, buffer);
            gcr_cycle = find_track_cycle(buffer);

/*
            if (gcr_cycle != NULL) printf(" cycle: %d ", gcr_cycle-buffer); 
*/

            for (sector = 0; sector < sector_map_1541[track]; sector++)
            {
                sector_max[sector] = 0;
                /* convert sector to free sector buffer */
                errorcode = convert_GCR_sector(buffer, gcr_cycle, rawdata,
                                               track, sector, id);

                if (errorcode == OK) any_sectors = 1;

                /* check, if identical sector has been read before */
                for (csec = 0; csec < sector_count[sector]; csec++)
                {
                    if ((memcmp(sectordata+(21*csec+sector)*260, rawdata, 260)
                        == 0) && (sector_error[sector][csec] == errorcode))
                    {
                        sector_occur[sector][csec] += 1;
                        break;
                    }
                }
                if (csec == sector_count[sector])
                {
                    /* sectordaten sind neu, kopieren, zaehler erhoehen */
                    memcpy(sectordata+(21*csec+sector)*260, rawdata, 260);
                    sector_occur[sector][csec] = 1;
                    sector_error[sector][csec] = errorcode;
                    sector_count[sector] += 1;
                }

                goodsector = 0;
                for (csec = 0; csec < sector_count[sector]; csec++)
                {
                    if (sector_occur[sector][csec]-((sector_error[sector][csec]==OK)?0:8)
                        > sector_max[sector])
                    {
                        sector_use[sector] = csec;
                        sector_max[sector] = csec;
                    }

                    if (sector_occur[sector][csec]-((sector_error[sector][csec]==OK)?0:8)
                        > (retry / 2 + 1))
                    {
                        goodsector = 1;
                    }
                }
                if (goodsector == 0)
                    goodtrack = 0;
            } /* for sector.... */
            if (goodtrack == 1) break; /* break out of for loop */
            if ((retry == 1) && (any_sectors==0)) break;
        } /* for retry.... */


        /* keep the best data for each sector */

        for (sector = 0; sector < sector_map_1541[track]; sector++)
        {
            printf("%d",sector);

            memcpy(d64ptr, sectordata+1+(21*sector_use[sector]+sector)*260,
                   256);
            d64ptr += 256;

            errorcode = sector_error[sector][sector_use[sector]];
            errorinfo[blockindex] = errorcode;

            if (errorcode != OK)
            {
                if (track <= 35)
                    save_errorinfo = 1;
                else
                    save_40_errors = 1;
            }
            else if (track > 35)
            {
                save_40_tracks = 1;
            }
   
            /* screen information */
            if (errorcode == OK)
                printf(" ");
            else
                printf("%d",errorcode);
            blockindex++;
       }


    } /* track loop */

    blocks_to_save = (save_40_tracks) ? MAXBLOCKSONDISK : BLOCKSONDISK;

    if (fwrite((char *) d64data, blocks_to_save*256, 1,fpout) != 1)
    {
        fprintf(stderr, "Cannot write d64 data.\n");
        return (0);
    }

    if (save_errorinfo == 1)
    {
        if (fwrite((char *) errorinfo, blocks_to_save, 1, fpout) != 1)
        {
            fprintf(stderr, "Cannot write sector data.\n");
            return (0);
        }
    }
    return (1);
}



int main(int argc, char *argv[])
{
    int track, sector;
    int i;
    int byte;
    int fd;
    int bump, reset;
    BYTE buffer[500];
    BYTE error[500];
    BYTE cmd[80];
    char outname[80];
    char header[0x100];
    int ok;

    FILE *fpout;


    printf("\nmnib - Commodore G64 disk image nibbler v%.2f", VERSION);
    printf("\n (C) 2000,01 Markus Brenner\n\n");

    bump = reset = 0;
    start_track = 1*2;
    end_track = 41*2;
    track_inc = 2;
    use_default_density = 1;
    no_extra_tracks = 0;
    disktype = DISK_NORMAL;

    while (--argc && (*(++argv)[0] == '-'))
    {
        switch (tolower((*argv)[1]))
        {
            case 'b':
                bump = 1;
                break;
            case 'h':
                track_inc = 1;
                break;
            case 'd':
                use_default_density = 0;
                break;
            case 'r':
                reset = 1;
                break;
            case 'g':
                disktype = DISK_GEOS;
                break;
            case '3':
                no_extra_tracks = 1; 
                end_track = 35*2;
                break;
            default:
                break;
        }
    }


    if (argc < 1) usage();

    strcpy(outname, argv[0]);
    if ((fpout = fopen(outname,"wb")) == NULL)
    {
        fprintf(stderr, "Couldn't open output file %s!\n", outname);
        exit(2);
    }

    if (compare_extension(outname, "D64"))
        imagetype = IMAGE_D64;
    else if (compare_extension(outname, "G64"))
        imagetype = IMAGE_G64;
    else
        imagetype = IMAGE_NIB;

    /* write NIB-header if appropriate */
    if (imagetype == IMAGE_NIB)
    {
        memset(header, 0x00, 0x100);
        sprintf(header, "MNIB-1541-RAW%c%c%c",1,0,0);
        for (i = 0; i < 0x100; i++)
        {
            fputc(header[i], fpout);
        }
    }

    if (!detect_ports(reset)) exit (3);

    /* prepare error string $73: CBM DOS V2.6 1541 */
    sprintf(cmd,"M-W%c%c%c%c%c%c%c%c",0,3,5,0xa9,0x73,0x4c,0xc1,0xe6);
    cbm_exec_command(fd, 8, cmd, 11);
    sprintf(cmd,"M-E%c%c",0x00,0x03);
    cbm_exec_command(fd, 8, cmd, 5);
    cbm_device_status(fd, 8, error, 500);
    printf("Drive Version: %s\n", error);

    if (error[18] == '4')
        drivetype = 1541;
    else if (error[18] == '7')
        drivetype = 1571;
    else
        drivetype = 0; /* unknown drive, use 1541 code */

    printf("Drive type: %d\n", drivetype);
    
    if (bump)
    {
        /* perform a bump */
        delay(1000);
        printf("Bumping...\n");
        sprintf(cmd,"M-W%c%c%c%c%c",6,0,2,1,0);
        cbm_exec_command(fd, 8, cmd, 8);
        sprintf(cmd,"M-W%c%c%c%c",0,0,1,0xc0);
        cbm_exec_command(fd, 8, cmd, 7);
        delay(2500);
    }

    cbm_exec_command(fd, 8, "U0>M0", 0);
    cbm_exec_command(fd, 8, "I0:", 0);
    printf("Initialising disk\n");

    upload_code("bn_flop.prg");
    sprintf(cmd,"M-E%c%c",0x00,0x03);
    cbm_exec_command(fd, 8, cmd, 5);

    cbm_par_read(FD);
    if (!find_par_port()) exit (4);

/*
    scan_density();
*/
    fprintf(stderr, "test: %s\n", test_par_port() ? "OK" : "FAILED");
    fprintf(stderr, "code: %s\n", (ok=verify_floppy()) ? "OK" : "FAILED");
    if (!ok) exit (5);

    /* read out disk into file */
    motor_on();


    if (imagetype == IMAGE_NIB)
        readdisk(fpout, header+0x10);
    else if (imagetype == IMAGE_D64)
        read_d64(fpout);

    printf("\n");
    cbm_par_read(FD);


    /* fill NIB-header if appropriate */
    if (imagetype == IMAGE_NIB)
    {
        rewind(fpout);
        for (i = 0; i < 0x100; i++)
        {
            fputc(header[i], fpout);
        }
        fseek(fpout, 0, SEEK_END);
    }

    fclose(fpout);

    motor_on();
    step_to_halftrack(36);
    send_par_cmd(FL_RESET);
    printf("drive reset...\n");
    delay(2000);

    return 1;
}
