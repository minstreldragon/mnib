/*
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *  Copyright 1999 Michael Klein <michael.klein@puffin.lb.shuttle.de>
*/

#ifndef _KERNEL_H
#define _KERNEL_H


#define CBMCTRL_TALK	    0
#define CBMCTRL_LISTEN	    1
#define CBMCTRL_UNTALK      2
#define CBMCTRL_UNLISTEN    3
#define CBMCTRL_OPEN        4
#define CBMCTRL_CLOSE       5
#define CBMCTRL_RESET       6

#define CBMCTRL_PP_READ     10
#define CBMCTRL_PP_WRITE    11
#define CBMCTRL_IEC_POLL    12
#define CBMCTRL_IEC_SET     13
#define CBMCTRL_IEC_RELEASE 14
#define CBMCTRL_PAR_READ    15
#define CBMCTRL_PAR_WRITE   16

#endif
