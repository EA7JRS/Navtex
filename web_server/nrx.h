/*
 * Navtex · NMEA 0183 output of the received messages (NRX sentences)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS (fork EA7JRS/Navtex). See COPYING.md.
 */
#ifndef NAVTEX_NRX_H
#define NAVTEX_NRX_H

#include <stddef.h>

/* The message as NRX sentences, each ended with CR LF, in one string to be freed by
   the caller (NULL when memory runs out or the input is not valid).

   iec = 0: the form of Furuno's NAVTEX receivers, which the APRS LoRa+ app and plotter use:
     $CRNRX,GA05,line,lines,518,dd,mm,yyyy,hhmmss,error rate %,text*hh
   one line of text per sentence, a long line split at a space when possible.

   iec = 1: the form of IEC 61097-6, annex C:
     $CRNRX,sentences,sentence,id,GA05,2,hhmmss,dd,mm,yyyy,characters,bad characters,A,text*hh
     $CRNRX,sentences,sentence,id,,,,,,,,,,more text*hh
   the text runs on from one sentence to the next, its line ends as ^0D^0A; the frequency
   is an index (1 = 490 kHz, 2 = 518 kHz, 3 = 4209.5 kHz); status A when the message
   reached its NNNN, V when it did not; id is a number 00-99 that tells messages apart.

   No sentence passes 82 characters; reserved characters go as ^hh (IEC 61162-1).
   bbbb: message identity; freq: 518, 490 or 4209; received: "YYYY-MM-DD HH:MM" UTC;
   message: the stored text, with its ZCZC line and without NNNN. */
char *nrx_sentences (const char *bbbb, int freq, const char *received, const char *message, int iec, int seq, int complete);

/* $CRALR,hhmmss.00,003,A,V,NAVTEX: Search and rescue information*hh  (IEC 61097-6, 4.3.9.2)
   active: the alarm condition holds; acknowledged: the operator has reset it. out: 96 bytes. */
void alr_sentence (char *out, size_t out_size, int hhmmss, int active, int acknowledged);

/* 1 when the text holds a valid "$--ACK,003*hh" sentence: the alarm reset from other equipment */
int is_alarm_ack (const char *text);

/* Starts the background sender. It reads its two settings every few seconds, so a
   change needs no restart:  nrx.udp     "host:port" to send datagrams to (empty: off)
                             nrx.tcp     port of a server other programs connect to (0: off)
                             nrx.format  "furuno" (default) or "iec"
   While a search and rescue message waits to be acknowledged it also sends the ALR
   sentence every 30 s, and it takes the ACK sentence from the TCP clients. */
void nrx_start (void);

/* {"udp":"192.168.1.255:10110","tcp":10110,"clients":1,"sent":12,"last_id":57,"error":""} */
char *nrx_json (void);

#endif
