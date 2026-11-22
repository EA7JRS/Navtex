/*
 * Navtex · NMEA 0183 output of the received messages (NRX sentences)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS (fork EA7JRS/Navtex). See COPYING.md.
 */
#ifndef NAVTEX_NRX_H
#define NAVTEX_NRX_H

/* The message as NRX sentences, each ended with CR LF, in one string to be freed by
   the caller (NULL when memory runs out). The form is the one of Furuno's NAVTEX
   receivers, which the APRS LoRa+ app and plotter use too:

     $CRNRX,GA05,line,lines,518,dd,mm,yyyy,hhmmss,error rate %,text*hh

   one line of text per sentence; a long line is split, at a space when possible, so
   that no sentence passes 82 characters; reserved characters go as ^hh (IEC 61162-1).
   bbbb: message identity; freq: 518, 490 or 4209; received: "YYYY-MM-DD HH:MM" UTC;
   message: the stored text, with its ZCZC line and without NNNN. */
char *nrx_sentences (const char *bbbb, int freq, const char *received, const char *message);

/* Starts the background sender. It reads its two settings every few seconds, so a
   change needs no restart:  nrx.udp  "host:port" to send datagrams to (empty: off)
                             nrx.tcp  port of a server other programs connect to (0: off) */
void nrx_start (void);

/* {"udp":"192.168.1.255:10110","tcp":10110,"clients":1,"sent":12,"last_id":57,"error":""} */
char *nrx_json (void);

#endif
