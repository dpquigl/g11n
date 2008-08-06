/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").  
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2008, by Sun Microsystems, Inc.
 * All rights reserved.
 */
#ident   "@(#)UTF-8_to_viscii.c	1.1 08/07/28"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unicode_viscii.h>	/* Unicode to viscii mapping table */
#include "common_defs.h"

#define	MSB	0x80	/* most significant bit */
#define  ONEBYTE	0xff	/* right most byte */

#define NON_ID_CHAR '?'	/* non-identified character */



typedef struct _icv_state {
    char    keepc[6];	/* maximum # byte of UTF8 code */
    short   ustate;
    int	_errno;		/* internal errno */
    boolean little_endian;
    boolean bom_written;
} _iconv_st;

enum _USTATE	{ U0, U1, U2, U3, U4 };


/*
 * Open; called from iconv_open()
 */
void *
_icv_open()
{
    _iconv_st *st;

    if ((st = (_iconv_st *)malloc(sizeof(_iconv_st))) == NULL) {
        errno = ENOMEM;
        return ((void *) -1);
    }

    st->ustate = U0;
    st->_errno = 0;
    st->little_endian = false;
    st->bom_written = false;
#if defined(UCS_2LE)
    st->little_endian = true;
    st->bom_written = true;
#endif
    return ((void *) st);
}


/*
 * Close; called from iconv_close()
 */
void
_icv_close(_iconv_st *st)
{
    if (!st)
        errno = EBADF;
    else
        free(st);
}


/*
 * Actual conversion; called from iconv()
 */
/*=========================================================
 *
 *       State Machine for interpreting UTF8 code
 *
 *=========================================================
 *               4 byte unicode
 *          +----->------->------------> U5 -----> U6-------> U7---+
 *          |                                                      |
 *          |    3 byte unicode                                    |
 *          +----->------->-------+                                |
 *          |                     |                                |
 *          ^                     v                                |
 *          |  2 byte             U2 ---> U3                       |
 *          |  unicode                    v                        |
 * +------> U0 -------> U1                +-------->U4---+         |
 * ^  ascii |           |                           ^    |         |
 * |        |           +-------->--------->--------+    |         |
 * |        v                                            v         V
 * +----<---+-----<------------<------------<------------+---------+
 *
 *=========================================================*/
size_t
_icv_iconv(_iconv_st *st, char **inbuf, size_t *inbytesleft,
				char **outbuf, size_t *outbytesleft)
{
    char    c1, c2;
    int     uconv_num = 0;
    unsigned long  uni = 0;
    int     utf8_len = 0;	

#ifdef DEBUG
    fprintf(stderr, "==========     iconv(): UTF2 --> GBK2K     ==========\n");
#endif
    if (st == NULL) {
        errno = EBADF;
        return ((size_t) -1);
    }

    if (inbuf == NULL || *inbuf == NULL) { /* Reset request. */
        st->ustate = U0;
        st->_errno = 0;
        return ((size_t) 0); 
    }

    st->_errno = 0;     /* reset internal errno */
    errno = 0;          /* reset external errno */

    /* a state machine for interpreting UTF8 code */
    while (*inbytesleft > 0 && *outbytesleft > 0) {
	   
        uchar_t  first_byte;
        unsigned short  ch = 0;
        switch (st->ustate) {
        case U0:
             /*
             * assuming ASCII in the beginning 
             */
            if ((**inbuf & MSB) == 0) {	/* ASCII */
                **outbuf = **inbuf;
                (*outbuf)++;
                (*outbytesleft)--;
            } else {	
                if ((**inbuf & 0xe0) == 0xc0) {	
                    /* 2 byte unicode 0xc0..0xdf */
                    /* invalid sequence if the first char is either 0xc0 or 0xc1 */
                    if ( number_of_bytes_in_utf8_char[((uchar_t)**inbuf)] == ICV_TYPE_ILLEGAL_CHAR )
                        st->_errno = errno = EILSEQ;
                    else {
                        st->ustate = U1;
                        st->keepc[0] = **inbuf;
                    }
                } else if ((**inbuf & 0xf0) == 0xe0) {	/* 3 byte 0xe0..0xf0 */
                    st->ustate = U2;
                    st->keepc[0] = **inbuf;
                } else {
                    /* more than 3  bytes of UTF-8 sequences */
                    if ( number_of_bytes_in_utf8_char[((uchar_t)**inbuf)] == ICV_TYPE_ILLEGAL_CHAR )
                        st->_errno = errno = EILSEQ;
                    st->_errno = errno = EILSEQ;
                }
            }
            break;
        case U1:        
            /* 2 byte utf-8 encoding */
            if ((**inbuf & 0xc0) == MSB) {
                utf8_len = 2;
                st->keepc[1] = **inbuf;
                
                c1 = (st->keepc[0]&0x1c)>>2;
                c2 = ((st->keepc[0]&0x03)<<6) | ((st->keepc[1])&0x3f);
                st->ustate = U4;
#ifdef DEBUG
                fprintf(stderr, "UTF8: %02x%02x   --> ",
                st->keepc[0]&ONEBYTE, st->keepc[1]&ONEBYTE);
#endif
                continue;	/* should not advance *inbuf */
            } else {
                st->_errno = errno = EILSEQ;
            }
            break;
        case U2:		
            /* 3 byte unicode - 2nd byte */
            first_byte = (uchar_t)st->keepc[0];
            /* if the first byte is 0xed, it is illegal sequence if the second 
             * one is between 0xa0 and 0xbf because surrogate section is ill-formed
             */
            if (((uchar_t)**inbuf) < valid_min_2nd_byte[first_byte] ||
                ((uchar_t)**inbuf) > valid_max_2nd_byte[first_byte] )
                st->_errno = errno = EILSEQ;
            else {			
                st->ustate = U3;
                st->keepc[1] = **inbuf;
            }
            break;
        case U3:
            /* 3 byte unicode - 3rd byte */
            if ((**inbuf & 0xc0) == MSB) {
                st->ustate = U4;
                utf8_len = 3;
                st->keepc[2] = **inbuf;
                c1 = ((st->keepc[0]&0x0f)<<4) |
                ((st->keepc[1]&0x3c)>>2);
                c2 = ((st->keepc[1]&0x03)<<6) | ((**inbuf)&0x3f);
#ifdef DEBUG
                fprintf(stderr, "UTF8: %02x%02x%02x --> ", st->keepc[0]&ONEBYTE,
                st->keepc[1]&ONEBYTE, **inbuf&ONEBYTE);
#endif
                continue;	/* should not advance *inbuf */
            } else {
                st->_errno = errno = EILSEQ;
            }
            break;
        case U4:
            uni = (unsigned long) ((c1 & ONEBYTE) << 8) + (c2 & ONEBYTE);
            if (!uni_2_viscii(uni, (unsigned char*)&ch)) {
                **outbuf = NON_ID_CHAR;
                uconv_num += utf8_len;
            } else {
                **outbuf = ch;
            }
            (*outbuf)++;
            (*outbytesleft)--;
            st->ustate = U0;
            break;
        default:			
            /* should never come here */
            st->_errno = errno = EILSEQ;
            st->ustate = U0;	/* reset state */
            break;
        }

        if (st->_errno) {
#ifdef DEBUG
            fprintf(stderr, "!!!!!\tst->_errno = %d\tst->ustate = %d\n",
                st->_errno, st->ustate);
#endif
            break;
        }

        (*inbuf)++;
        (*inbytesleft)--;
    }

    if (*inbytesleft == 0 && st->ustate != U0)
        errno = EINVAL;

    if (*inbytesleft > 0 && *outbytesleft == 0)
        errno = E2BIG;
   
    if (errno) {
        int num_reversed_bytes = 0;
	   
        switch (st->ustate) 
        {
        case U1:
            num_reversed_bytes = 1;
            break;
        case U2:
            num_reversed_bytes = 1;
            break;
        case U3:
            num_reversed_bytes = 2;
            break;
        case U4:
            num_reversed_bytes = utf8_len - 1;
            break;
        }

        /*
         * if error, *inbuf points to the byte following the last byte
         * successfully used in conversion.
         */
        *inbuf -= num_reversed_bytes;
        *inbytesleft += num_reversed_bytes;
        st->ustate = U0;
	   
        return ((size_t) -1);
    }

    return uconv_num;
}

