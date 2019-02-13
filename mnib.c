/* mnib - Markus' G64 nibbler

    (C) 20000 Markus Brenner

    V 0.10   implementation of serial and parallel protocol
    V 0.11   first density scan and nibbler functionality
    V 0.12   automatic port detection and improved program flow
    V 0.13   extended data output format
    V 0.14   fixed parallel port support
    V 0.15   2nd try for parallel port fix
    V 0.16   next try with adjustments to ECP ports
    V 0.17   added automatic drive type detection
*/

#include "cbm.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/movedata.h>

#define VERSION 0.17
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
#define FL_SOFTSTEP    0x10

static int start_track;
static int end_track;
static int track_inc;
static int current_track;
static unsigned int lpt[4];
static int lpt_num;
static int drivetype;

char bitrate_range[4] =
{ 42*2, 31*2, 25*2, 18*2 };

char bitrate_value[4] =
{ 0x00, 0x20, 0x40, 0x60 };

static unsigned char density_branch[4] =
{ 0xb1, 0xb5, 0xb7, 0xb9 };

unsigned char track_density[84];


void usage(void)
{
    fprintf(stderr, "usage: mnib <output>\n");
    exit(1);
}


void upload_code(char *floppyfile)
{
    FILE *fpin;
    unsigned int databytes, dataread;
    unsigned char *buffer;
    unsigned int start;
    unsigned int patch_pos[9] =
    { 0x72, 0x89, 0x9e, 0x1da, 0x224, 0x258, 0x262, 0x293, 0x2a6 };
    int i;
 

    if ((fpin = fopen(floppyfile, "rb")) == NULL)
    {
        fprintf(stderr, "Couldn't open upload file %s!\n", floppyfile);
        exit(1);
    }

    fseek(fpin, 0, SEEK_END);
    databytes = ftell(fpin);
    rewind(fpin);

    if((buffer = calloc(databytes, sizeof(unsigned char))) == NULL)
    {
        fprintf(stderr, "Couldn't allocate upload buffer!\n");
        exit(2);
    }

    dataread = fread(buffer, sizeof(unsigned char), databytes, fpin);

    if (dataread != databytes)
    {
        fprintf(stderr, "Couldn't read all bytes from file %s!\n", floppyfile);
        exit(3);
    }

    printf("databytes = %ld\n", databytes);

    start = buffer[0] + (buffer[1] << 8);
    printf("Startadress: $%04x\n",start);

    /* patch code if using 1571 drive */
    if (drivetype == 1571)
    {
        for (i = 0; i < 9; i++)
        {
            if (buffer[patch_pos[i]] != 0x18)
                printf("Possibly bad patch at %04x!\n",patch_pos[i]);
            buffer[patch_pos[i]] = 0x40;
        }
    }

    cbm_upload(FD, 8, start, buffer+2, databytes-2); 

    free(buffer);
    fclose(fpin);
}


void send_par_cmd(unsigned char cmd)
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
    unsigned char cmd[80];

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
    unsigned char density;

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
    unsigned char density;
    unsigned char killer_info;
    int i, bin;
    unsigned char count;
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


int readdisk(FILE *fpout, char *track_header)
{
    int track;
    int density, defdensity;
    int i;
    int header_entry;
    unsigned char buffer[0x2000];

    if (!test_par_port()) return 0;

    printf("READ DISK\n");  
    motor_on();
    delay(500);
    header_entry = 0;
    for (track = start_track; track <= end_track; track += track_inc)
    {
        step_to_halftrack(track);
        printf("\n%02d: ",track);


        for (defdensity = 3; track >= bitrate_range[defdensity]; defdensity--);
        printf("(%d) ", (defdensity & 3));

        density = scan_track(track);
        track_density[track] = density;
        if (density & 0x80)
        {
            printf("F");
            density = defdensity;
        }
        else if (density & 0x40)
        {
            printf("S");
            density = defdensity;
        }
        else printf("%d", (density & 3));
        density = defdensity;

        track_header[header_entry*2] = track;
        track_header[header_entry*2+1] = density;
        header_entry++;

        send_par_cmd(FL_DENSITY);
        cbm_par_write(FD, density_branch[density]);
        cbm_par_write(FD, 0x9f);                   /* $1c00 CLEAR mask */
        cbm_par_write(FD, bitrate_value[density]); /* $1c00  SET  mask */
        cbm_par_read(FD);

        send_par_cmd(FL_READNORMAL);
        cbm_par_read(FD);
        for (i = 0; i < 0x2000; i+=2)
        {
            buffer[i]   = cbm_nib_read1(FD);
            buffer[i+1] = cbm_nib_read2(FD);
        }
//        delay(500);
        cbm_par_read(FD);

        for (i = 0; i < 0x2000; i++)
            fputc(buffer[i], fpout);
    }
    step_to_halftrack(4*2);
}


int main(int argc, char *argv[])
{
    int track, sector;
    int i;
    int byte;
    int fd;
    unsigned char buffer[500];
    unsigned char error[500];
    unsigned char cmd[80];
    char outname[80];
    char header[0x100];

    FILE *fpout;


    printf("\nmnib - Commodore G64 disk image nibbler v%.2f", VERSION);
    printf("\n (C) 2000 Markus Brenner\n\n");

    if (argc < 2) usage();

    start_track = 1*2;
    end_track = 35*2;
    track_inc = 2;

    strcpy(outname, argv[1]);
    if ((fpout = fopen(outname,"wb")) == NULL)
    {
        fprintf(stderr, "Couldn't open output file %s!\n", outname);
        exit(2);
    }

    memset(header, 0x00, 0x100);
    sprintf(header, "MNIB-1541-RAW%c%c%c",1,0,0);
    for (i = 0; i < 0x100; i++)
    {
        fputc(header[i], fpout);
    }

    if (!detect_ports()) exit (3);

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
    

    cbm_exec_command(fd, 8, "U0>M0", 0);
    cbm_exec_command(fd, 8, "I0:", 0);
    printf("Initialising disk\n");

    upload_code("bn_flop.prg");
    sprintf(cmd,"M-E%c%c",0x00,0x03);
    cbm_exec_command(fd, 8, cmd, 5);
/*
    cbm_listen(FD,8,15);
    cbm_write(FD,cmd,5);
    cbm_unlisten(FD);
*/

    cbm_par_read(FD);
    if (!find_par_port()) exit (4);

/*
    scan_density();
*/

    fprintf(stderr, "test: %s\n", test_par_port() ? "OK" : "FAILED");
    readdisk(fpout, header+0x10);
    printf("%02x \n",cbm_par_read(FD));


    rewind(fpout);
    for (i = 0; i < 0x100; i++)
    {
        fputc(header[i], fpout);
    }
    fseek(fpout, 0, SEEK_END);
    fclose(fpout);

    motor_on();
    step_to_halftrack(36);
    send_par_cmd(FL_RESET);
    printf("drive reset...\n");
    delay(2000);

    return 1;
}
