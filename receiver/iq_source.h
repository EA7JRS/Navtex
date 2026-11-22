/*
 * Navtex receiver: where the I/Q samples come from.
 *
 * Every source delivers complex samples at 252 kS/s centred on 504 kHz
 * (halfway between 490 and 518 kHz) and hands them to the common ring
 * buffer. The demodulator does not care which receiver produced them.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS (fork EA7JRS/Navtex). See COPYING.md.
 */
#ifndef IQ_SOURCE_H
#define IQ_SOURCE_H

#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IQ_SAMPLE_RATE 252000      /* samples per second delivered by a source */
#define IQ_CENTRE_MF_HZ 504000     /* (518000 + 490000) / 2: both MF channels at +-14 kHz */
#define IQ_CENTRE_HF_HZ 4195500    /* 4209500 - 14000: the HF channel on the upper demodulator */
/* Where the receiver must be tuned for the selected band (-B) */
long iq_centre_hz(void);

/* Options shared by the sources (set from the command line) */
struct iq_options {
    int bias_t;               /* -b  power an active antenna through the coax */
    char antenna_port;        /* -p  SDRplay antenna port A/B/C */
    int rtl_index;            /* -i  RTL-SDR device index */
    const char *rtl_serial;   /* -S  RTL-SDR serial number (instead of the index) */
    int rtl_direct_sampling;  /* -D  0 off (tuner or built-in upconverter), 1 I branch, 2 Q branch; -1 = from the model */
    long rtl_upconverter_hz;  /* -u  LO of an external upconverter, 0 = none */
    int rtl_gain_tenths;      /* -g  tuner gain in tenths of dB, -1 = automatic */
    int rtl_ppm;              /* -P  crystal correction */
    const char *file_path;    /* -f  recording to decode instead of a receiver */
    int file_realtime;        /* -t  play the recording at real speed */
};
extern struct iq_options iq_opt;

/* Set by SIGINT/SIGTERM: stop and release the receiver */
extern volatile sig_atomic_t stop_requested;
/* Set when the settings of this receiver changed: stop, re-read them and start again */
extern volatile sig_atomic_t reconfig_requested;
/* Asked every few seconds while receiving; returns 1 when the settings changed (may be NULL) */
extern int (*iq_settings_changed)(void);
/* Prefix of the status entries of this receiver: "" or "rx1." */
extern char iq_status_prefix[8];
/* Set by a source when its receiver is unplugged or fails */
extern volatile sig_atomic_t device_lost;

/* Ring buffer: sources push, consume_loop() feeds the demodulator */
int  iq_ring_init(void);
void iq_ring_free(void);
void iq_push_short(const short *xi, const short *xq, unsigned int num_samples);
void iq_push_u8(const unsigned char *iq, unsigned int num_bytes);   /* RTL-SDR: unsigned 8 bit, I then Q */

/* Feeds the demodulator until a stop is requested or the receiver is lost
   (no samples for a while counts as lost). */
void consume_loop(void);

/* Feeds samples straight to the demodulator, without the ring (file source) */
void iq_demodulate_short(const short *interleaved_iq, unsigned int num_pairs);

/* Each source opens its receiver, runs until told to stop and closes it.
   Returns 0 after a requested stop (or the end of a file), 1 on failure. */
int sdrplay_source_run(void);
int rtlsdr_source_run(void);
int file_source_run(void);

#ifdef __cplusplus
}
#endif
#endif
