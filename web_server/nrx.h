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

/* NRM, the "NAVTEX receiver mask" (IEC 61097-6, annex C.2):  $--NRM,function,frequency,areas,types*hh
   function 0: send me the stored messages that match; 1: the mask of what is stored (here: followed).
   frequency: 1 = 490 kHz, 2 = 518 kHz, 3 = 4209.5 kHz. areas, types: 32 bit hex, bit 0 = letter A.
   nrm_parse() returns 1 for a valid sentence (checksum included) and fills the four values, the
   frequency as 490, 518 or 4209. */
int nrm_parse (const char *sentence, int *function, int *freq, unsigned long *areas, unsigned long *types);
void nrm_sentence (char *out, size_t out_size, int function, int freq, unsigned long areas, unsigned long types);
unsigned long letters_to_mask (const char *letters);
void mask_to_letters (unsigned long mask, char *out);        /* out: 27 bytes */
/* 1 when the text holds a valid "$--CRQ,NRM*hh" query */
int is_nrm_query (const char *text);

/* Bridge alert management (IMO MSC.302(87); sentences of IEC 61162-1 ed. 5 as IEC 62923 uses them), besides ALR / ACK.
   The one alert of this program: search and rescue information received, identifier 3 (the number IEC 61097-6
   gives the alarm in ALR), instance 1, category B, priority warning, no manufacturer code.
     $CRALF,1,1,0,hhmmss.00,B,W,V,,3,1,revision,0,NAVTEX SAR*hh     state V: active, not acknowledged; N: normal
     $CRALC,01,01,00,1,,3,1,revision*hh                             the cyclic list of active alerts (0 entries: none)
     $CRHBT,30,A,n*hh                                               heartbeat, n = 0..9
     $CRARC,hhmmss.00,,3,1,S*hh                                     a command that is not carried out (silence, transfer)
   acn_parse() returns 1 for a valid "$--ACN,time,manufacturer,identifier,instance,command,C*hh" found in the text
   and gives the identifier (0 = all alerts), the instance (0 when empty) and the command (A, Q, O or S).
   The buffers: 96 bytes. */
void alf_sentence (char *out, size_t out_size, int hhmmss, char state, int revision);
void alc_sentence (char *out, size_t out_size, int active, int revision);
void hbt_sentence (char *out, size_t out_size, int interval, int seq);
void arc_sentence (char *out, size_t out_size, int hhmmss, long identifier, long instance, char command);
int acn_parse (const char *text, long *identifier, long *instance, char *command);

/* Starts the background sender. It reads its two settings every few seconds, so a
   change needs no restart:  nrx.udp     "host:port" to send datagrams to (empty: off)
                             nrx.tcp     port of a server other programs connect to (0: off)
                             nrx.format  "furuno" (default) or "iec"
                             nrx.bam     "on": also the bridge alert management sentences (ALF on every change,
                                         ALC and HBT every 30 s); "off" (default)
   While a search and rescue message waits to be acknowledged it also sends the ALR
   sentence every 30 s. From the TCP clients it takes the ACK sentence, the ACN sentence
   (acknowledge, or repeat the alert; silence and transfer are answered with ARC), the NRM
   sentence and its query (CRQ). */
void nrx_start (void);

/* {"udp":"192.168.1.255:10110","tcp":10110,"clients":1,"sent":12,"last_id":57,"error":""} */
char *nrx_json (void);

#endif
