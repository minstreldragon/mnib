/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *  Copyright 1999 Michael Klein <michael.klein@puffin.lb.shuttle.de>
 *
 *	Modified for DOS use by Markus Brenner <markus@brenner.de>
*/

/*
#define DEBUG
*/

#include <pc.h>                         /* PC specific includes (outb, inb) */
#include <stdio.h>                      /* printk substituted by printf */
#include <unistd.h>                     /* usleep() function */
#include <dos.h>                        /* delay() */
#include <errno.h>                      /* EINVAL */
#include <time.h>                       /* PC specific includes (outb, inb) */

#include "kernel.h"

/* unsigned int serport        = 0x378; */      /* 'serial' LPT port address */
// unsigned int serport        = 0x3bc;       /* 'serial' LPT port address */
// unsigned int parport        = 0x378;       /* 'parallel' LPT port address */

unsigned int serport;   /* 'serial' LPT port address */
unsigned int parport;   /* 'parallel' LPT port address */


/* symbolic names */
#define IEC_DATA   1
#define IEC_CLOCK  2
#define IEC_ATN    4

/* lpt output lines */
#define ATN_OUT    0x01
#define CLK_OUT    0x02
#define DATA_OUT   0x08
#define RESET_OUT  0x04
#define OUTMASK    0x04                 /* output mask for XE1541 cable */

/* lpt input lines */
#define ATN_IN     0x10
#define CLK_IN     0x20
#define DATA_IN    0x80                 /* changed to XE1541 standards */
#define RESET_IN   0x40
#define INMASK     0x80                 /* input mask for XE1541 cable */


static int lpt_num;                        /* # of available printer ports */
static unsigned int lpt[4];                /* port addresses */

static unsigned char *serportval;           /* current value in output register */
static unsigned char *parportval;           /* current value in output register */
static unsigned char portval[4];           /* current value in output register */

#define SET(line)       (outportb(serport+2,(*serportval|=line)^OUTMASK))
#define RELEASE(line)   (outportb(serport+2,(*serportval&=~(line))^OUTMASK))
#define GET(line)       (((inportb(serport+1)^INMASK)&line)==0?1:0)

#define PARREAD()       (outportb(parport+2,(*parportval|=0x20)^OUTMASK))
#define PARWRITE()      (outportb(parport+2,(*parportval&=0xdf)^OUTMASK))

#ifdef DEBUG
  #define DPRINTK(fmt,args...)     printf(fmt, ## args)
  #define SHOW(str)                show(str)
#else
  #define DPRINTK(fmt,args...)
  #define SHOW(str)
#endif

static int eoi;
static int irq_count;


void msleep(unsigned long usec)
{
	uclock_t start, stop;
	unsigned long ticks;

	start = uclock();
	stop = start + ((float)usec*UCLOCKS_PER_SEC/1000000);

	while(uclock() < stop);
}

/*
 *  dump input lines
 */
void show( char *s )
{
        printf("%s: data=%d, clk=%d, atn=%d, reset=%d\n", s,
                        GET(DATA_IN), GET(CLK_IN), GET(ATN_IN), GET(RESET_IN));
}

static void do_reset( void )
{
        printf("cbm_init: resetting devices\n");
        RELEASE(DATA_OUT | ATN_OUT | CLK_OUT);
        SET(RESET_OUT);

        delay(100); /* 100ms */

        RELEASE(RESET_OUT);

        printf("cbm_init: sleeping 5 seconds...\n");
        delay(5000); /* 5s */
}

/*
 *  send byte
 */
static int send_byte(int b)
{
        int i, ack = 0;

/*
        DPRINTK("send_byte %02x\n", b);
*/
/*
        disable();
*/

        for( i = 0; i < 8; i++ ) {
                msleep(70);
                if( !((b>>i) & 1) ) {
                        SET(DATA_OUT);
                }
                RELEASE(CLK_OUT);
                msleep(20);
                SET(CLK_OUT);
                RELEASE(DATA_OUT);
        }
        for( i = 0; (i < 20) && !(ack=GET(DATA_IN)); i++ ) {
                msleep(100);
        }
/*
        enable();
*/

        DPRINTK("ack=%d\n", ack);

        return ack;
}

/*
 *  wait until listener is ready to receive
 */
static void wait_for_listener()
{
/*
        SHOW("waiting for device");
*/

        RELEASE(CLK_OUT);

        while(GET(DATA_IN));
	if (--irq_count == 0)
        {
                SET(CLK_OUT);
/*
                DPRINTK("continue to send (no EOI)\n");
*/
                return;
        }
        DPRINTK("signaling EOI\n");
        msleep(150);
        while(!GET(DATA_IN));
        while(GET(DATA_IN));
        SET(CLK_OUT);
}

/*
 *  idle
 */
static void release_all(void)
{
        RELEASE(ATN_OUT | DATA_OUT);
}

int cbm_read(int f, char *buf, int count)
{
        int received = 0;
        int i, b, bit;
        int ok = 0;

        DPRINTK("cbm_read: %d bytes\n", count);

        if(eoi) {
                return 0;
        }

        do {
                i = 0;
                while(GET(CLK_IN)) {
                        if( i >= 50 ) {
                                delay(20); /* 20ms */
                        } else {
                                i++;
                                msleep(20);
                        }
                }
/*
                disable();
*/
                RELEASE(DATA_OUT);
                for(i = 0; (i < 40) && !(ok=GET(CLK_IN)); i++) {
                        msleep(10);
                }
                if(!ok) {
                        /* device signals eoi */
                        eoi = 1;
                        SET(DATA_OUT);
                        msleep(70);
                        RELEASE(DATA_OUT);
                }
                for(i = 0; i < 100 && !(ok=GET(CLK_IN)); i++) {
                        msleep(20);
                }
                for(bit = b = 0; (bit < 8) && ok; bit++) {
                        for(i = 0; (i < 200) && !(ok=(GET(CLK_IN)==0)); i++) {
                                msleep(10);
                        }
                        if(ok) {
                                b >>= 1;
                                if(GET(DATA_IN)==0) {
                                        b |= 0x80;
                                }
                                for(i = 0; i < 100 && !(ok=GET(CLK_IN)); i++) {
                                        msleep(20);
                                }
                        }
                }
                if(ok) {
                        received++;
                        SET(DATA_OUT);
                        *buf++ = (char)b;
                }
/*
                enable();
*/
                msleep(50);

        } while(received < count && ok && !eoi);

        DPRINTK("received=%d, count=%d, ok=%d, eoi=%d\n",
                        received, count, ok, eoi);

        return received;
}

static int cbm_raw_write(const char *buf, size_t cnt, int atn, int talk)
{
        unsigned char c;
        int i;
        int rv   = 0;
        int sent = 0;
        
        eoi = irq_count =  0;

        DPRINTK("cbm_write: %d bytes, atn=%d\n", cnt, atn);

        if(atn) {
                SET(ATN_OUT);
        }
        SET(CLK_OUT);
        RELEASE(DATA_OUT);

        for(i=0; (i<100) && !GET(DATA_IN); i++) {
            msleep(10);
        }

        if(!GET(DATA_IN)) {
                printf("cbm: no devices found\n");
                RELEASE(CLK_OUT | ATN_OUT);
                return -ENODEV;
        }

        delay(20); /* 20ms */


        while(cnt > sent && rv == 0) {
                c = *buf++;
                msleep(50);
                irq_count = ((sent == (cnt-1)) && (atn == 0)) ? 2 : 1;
                wait_for_listener();


                if(send_byte(c)) {
                        sent++;
                        msleep(100);
                } else {
                        printf("cbm: I/O error\n");
                        rv = -EIO;
                }
        }
        DPRINTK("%d bytes sent, rv=%d\n", sent, rv);

        if(talk) {
/*
                disable();
*/
                SET(DATA_OUT);
                RELEASE(ATN_OUT);
                msleep(30);
                RELEASE(CLK_OUT);
/*
                enable();
*/
        } else {
                RELEASE(ATN_OUT);
        }
        msleep(100);

        return (rv < 0) ? rv : sent;
}

int cbm_write(int f, char *buf, int cnt)
{
        return cbm_raw_write(buf, cnt, 0, 0);
}

int cbm_ioctl(int f, unsigned int cmd, unsigned long arg)
{
        unsigned char buf[2], c, talk;
        int rv = 0;

        buf[0] = (arg >> 8) & 0x1f;  /* device */
        buf[1] = arg & 0x0f;         /* secondary address */

        switch( cmd ) {
                case CBMCTRL_RESET:
                        do_reset();
                        return 0;

                case CBMCTRL_TALK:
                case CBMCTRL_LISTEN:
                        talk = (cmd == CBMCTRL_TALK);
                        buf[0] |= talk ? 0x40 : 0x20;
                        buf[1] |= 0x60;
                        rv = cbm_raw_write(buf, 2, 1, talk);
                        return rv > 0 ? 0 : rv;

                case CBMCTRL_UNTALK:
                case CBMCTRL_UNLISTEN:
                        buf[0] = (cmd == CBMCTRL_UNTALK) ? 0x5f : 0x3f;
                        rv = cbm_raw_write(buf, 1, 1, 0);
                        return rv > 0 ? 0 : rv;

                case CBMCTRL_OPEN:
                case CBMCTRL_CLOSE:
                        buf[0] |= 0x20;
                        buf[1] |= (cmd == CBMCTRL_OPEN) ? 0xf0 : 0xe0;
                        rv = cbm_raw_write(buf, 2, 1, 0);
                        return rv > 0 ? 0 : rv;

                case CBMCTRL_IEC_POLL:
                        c = inportb(serport+1);
                        if((c & DATA_IN) == 0) rv |= IEC_DATA;
                        if((c & CLK_IN ) == 0) rv |= IEC_CLOCK;
                        if((c & ATN_IN ) == 0) rv |= IEC_ATN;
                        return rv;

                case CBMCTRL_IEC_SET:
                        switch(arg) {
                            case IEC_DATA:
                                    SET(DATA_OUT);
                                    break;
                            case IEC_CLOCK:
                                    SET(CLK_OUT);
                                    break;
                            case IEC_ATN:
                                    SET(ATN_OUT);
                                    break;
                            default:
                                    return -EINVAL;
                        }
                        return 0;

                case CBMCTRL_IEC_RELEASE:
                        switch(arg) {
                            case IEC_DATA:
                                    RELEASE(DATA_OUT);
                                    break;
                            case IEC_CLOCK:
                                    RELEASE(CLK_OUT);
                                    break;
                            case IEC_ATN:
                                    RELEASE(ATN_OUT);
                                    break;
                            default:
                                    return -EINVAL;
                        }
                        return 0;

                case CBMCTRL_PP_READ:
                        PARREAD();
                        rv = inportb(parport);
                        PARWRITE();
                        return rv;

                case CBMCTRL_PP_WRITE:
                        outportb(parport, arg);
                        return 0;

                case CBMCTRL_PAR_READ:
//                        PARREAD();
                        RELEASE(DATA_OUT|CLK_OUT);
                        SET(ATN_OUT);
//                        msleep(10); /* 200? */
                        while(GET(DATA_IN));
                        rv = inportb(parport);
                        RELEASE(ATN_OUT);
                        msleep(10);
                        while(!GET(DATA_IN));
//                        PARWRITE();
                        return rv;

                case CBMCTRL_PAR_WRITE:
                        RELEASE(DATA_OUT|CLK_OUT);
                        SET(ATN_OUT);
                        msleep(10);
                        while(GET(DATA_IN));
                        PARWRITE();
                        outportb(parport, arg);
/*
                        msleep(10);
*/
                        RELEASE(ATN_OUT);
                        msleep(10);
                        while(!GET(DATA_IN));
                        PARREAD();
/*
                        msleep(10);
*/
                        return 0;
        }
        return -EINVAL;
}

int cbm_nib_read1(int f)
{
//    PARREAD();
    RELEASE(DATA_OUT);
    while (GET(DATA_IN));
/*
    inportb(parport);
*/
    return inportb(parport);
}

int cbm_nib_read2(int f)
{
//    PARREAD();
    RELEASE(DATA_OUT);
    while (!GET(DATA_IN));
/*
    inportb(parport);
*/
    return inportb(parport);
}

/*
static int cbm_open(int f)
{
        MOD_INC_USE_COUNT;
        return 0;
}
*/

static int cbm_release(int f)
{
        /*
        MOD_DEC_USE_COUNT;
        */
        return 0;
}



int scan_xe1541(unsigned int port)
{
    int i, rv;

    serport = port;

    RELEASE(DATA_OUT | ATN_OUT | CLK_OUT | RESET_OUT);
    delay(100);
    if(GET(RESET_IN)) return 0;
    if(GET(DATA_IN)) return 0;
    SET(ATN_OUT);
    SET(CLK_OUT);
    for(i=0; (i<100) && !GET(DATA_IN); i++)
        msleep(10);
    rv = GET(DATA_IN);
    RELEASE(ATN_OUT | CLK_OUT);
    return(rv);
}

int set_par_port(int port)
{
    if (port < lpt_num)
    {
        parport = lpt[port];
        parportval = &portval[port];
        PARREAD();
        printf("Port %d: %04x ", port, lpt[port]);
        return (1);
    }
    else return (0);
}


int detect_ports()
{
    int i;
    unsigned char byte[8];
    unsigned int port;
    int found;
    int goodport;

    unsigned char ecr;
    char *ecpm[8] = 
    {
        "SPP",
        "Byte",
        "Fast Centronics",
        "ECP",
        "EPP",
        "Reserved",
        "Test",
        "Configuration"
    };

    _dosmemgetb(0x411, 1, byte);
    lpt_num = (byte[0] & 0xc0) >> 6;
    printf("Number of LPT ports found: %d\n", lpt_num);

    _dosmemgetb(0x408, lpt_num * 2, byte);

    for (i = 0; i < lpt_num; i++)
    {
        port = byte[2*i] + byte[2*i+1]*0x100;
        lpt[i] = port;
    }


    /* on ECP ports force BYTE mode */
    for (i = 0; i < lpt_num; i++)
    {
        port = lpt[i];
        ecr = inportb(port+0x402);
        outportb(port+0x402, 0x34);
        outportb(port+2, 0xc6);
        if (inportb(port+0x402) != 0x35) continue; /* no ECP port */
        printf("ECP port at %04x in %s Mode\n", port, ecpm[(ecr&0xe0)>>5]);
        printf("Forcing Byte Mode\n");
        outportb(port+0x402, (ecr & 0x1f) | 0x20);
    }


    goodport = -1;
    for (i = 0; i < lpt_num; i++)
    {
        irq_count = 0;
        portval[i] = 0xc0;
        serportval   = &portval[i];
        found = scan_xe1541(lpt[i]);
        if ((goodport== (-1)) && found) goodport = i;
        printf("Port %d: %04x - %s\n", i, lpt[i], found ? "Found!" : "none");
    }

    if (goodport != -1)
    {
        printf("Using port %04x for XE1541 connection.\n", lpt[goodport]);
        serport = lpt[goodport];
        parport = lpt[goodport];
        serportval = &portval[goodport];
        parportval = &portval[goodport];
        RELEASE(DATA_OUT | CLK_OUT);
        SET(CLK_OUT);
        return (1);
    }
    else
    {
        printf("Drive not found!\n");
        return (0);
    }
}
