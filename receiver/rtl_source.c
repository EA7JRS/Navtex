/*
 * Navtex receiver: RTL-SDR source.
 *
 * NAVTEX is at 490 and 518 kHz, far below what the tuner of an RTL-SDR
 * reaches, so one of these is needed:
 *   - direct sampling (RTL-SDR Blog V3 and similar): the antenna goes
 *     straight into the RTL2832 converter, usually on the Q branch (-D 2);
 *   - a dongle with a built-in upconverter (RTL-SDR Blog V4): -D 0, the
 *     driver switches it in for frequencies below 28.8 MHz;
 *   - an external upconverter: -u <its local oscillator in Hz>.
 * The dongle is asked for 252 kS/s centred on 504 kHz, the same stream the
 * SDRplay source delivers after its hardware decimation.
 *
 * 2026 - Manuel - EA7JRS (fork EA7JRS/Navtex), MIT licence.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "iq_source.h"

#ifdef HAVE_LIBRTLSDR
#include <rtl-sdr.h>

static rtlsdr_dev_t *dev = NULL;

static void rtl_callback(unsigned char *buf, uint32_t len, void *ctx)
{
    (void) ctx;
    if (!stop_requested)
        iq_push_u8(buf, len);
}

static void *rtl_reader(void *arg)
{
    (void) arg;
    /* Blocks until rtlsdr_cancel_async() or until the dongle disappears */
    if (rtlsdr_read_async(dev, rtl_callback, NULL, 0, 16 * 16384) != 0 && !stop_requested)
        printf("rtlsdr: the sample stream ended with an error\n");
    if (!stop_requested)
        device_lost = 1;
    return NULL;
}

/* Nearest gain the tuner really has */
static int nearest_gain(int wanted_tenths)
{
    int gains[64];
    int n = rtlsdr_get_tuner_gains(dev, NULL);
    int i, best;

    if (n <= 0 || n > 64) return wanted_tenths;
    rtlsdr_get_tuner_gains(dev, gains);
    best = gains[0];
    for (i = 1; i < n; i++)
        if (abs(gains[i] - wanted_tenths) < abs(best - wanted_tenths)) best = gains[i];
    return best;
}

int rtlsdr_source_run(void)
{
    pthread_t reader;
    char manufact[256] = "", product[256] = "", serial[256] = "";
    uint32_t count = rtlsdr_get_device_count();
    uint32_t freq = (uint32_t) (IQ_CENTRE_HZ + iq_opt.rtl_upconverter_hz);
    int failed = 1;
    int r;

    if (count == 0)
    {
        printf("rtlsdr: no RTL-SDR device found\n");
        return 1;
    }
    if (iq_opt.rtl_index < 0 || (uint32_t) iq_opt.rtl_index >= count)
    {
        printf("rtlsdr: there is no device %d (%u found)\n", iq_opt.rtl_index, count);
        return 1;
    }
    rtlsdr_get_device_usb_strings((uint32_t) iq_opt.rtl_index, manufact, product, serial);
    printf("rtlsdr: device %d: %s %s, serial %s\n", iq_opt.rtl_index, manufact, product, serial);

    if ((r = rtlsdr_open(&dev, (uint32_t) iq_opt.rtl_index)) < 0)
    {
        printf("rtlsdr: cannot open the device (error %d). Is another program using it, or the DVB kernel driver loaded?\n", r);
        return 1;
    }

    if (rtlsdr_set_direct_sampling(dev, iq_opt.rtl_direct_sampling) != 0)
    {
        printf("rtlsdr: cannot set direct sampling mode %d\n", iq_opt.rtl_direct_sampling);
        goto close_dev;
    }
    printf("rtlsdr: direct sampling %s\n", iq_opt.rtl_direct_sampling == 0 ? "off (tuner / upconverter)" :
                                           iq_opt.rtl_direct_sampling == 1 ? "on, I branch" : "on, Q branch");

    if (rtlsdr_set_sample_rate(dev, IQ_SAMPLE_RATE) != 0)
    {
        printf("rtlsdr: cannot set the sample rate to %d S/s\n", IQ_SAMPLE_RATE);
        goto close_dev;
    }
    if (iq_opt.rtl_ppm != 0 && rtlsdr_set_freq_correction(dev, iq_opt.rtl_ppm) < 0)
        printf("rtlsdr: cannot set the frequency correction (continuing)\n");
    if (rtlsdr_set_center_freq(dev, freq) != 0)
    {
        printf("rtlsdr: cannot tune to %u Hz. Below the tuner range a dongle needs direct sampling (-D 1 or -D 2) or an upconverter (-u)\n", freq);
        goto close_dev;
    }
    printf("rtlsdr: tuned to %u Hz, %d S/s\n", rtlsdr_get_center_freq(dev), (int) rtlsdr_get_sample_rate(dev));

    if (iq_opt.rtl_direct_sampling != 0)
    {
        /* No tuner in the path: the only level control is the AGC of the RTL2832 */
        rtlsdr_set_agc_mode(dev, 1);
    }
    else if (iq_opt.rtl_gain_tenths < 0)
    {
        rtlsdr_set_tuner_gain_mode(dev, 0);
        printf("rtlsdr: automatic tuner gain\n");
    }
    else
    {
        int g = nearest_gain(iq_opt.rtl_gain_tenths);
        rtlsdr_set_tuner_gain_mode(dev, 1);
        rtlsdr_set_tuner_gain(dev, g);
        printf("rtlsdr: tuner gain %.1f dB\n", g / 10.0);
    }

    if (iq_opt.bias_t)
    {
#ifdef HAVE_RTLSDR_BIAS_TEE
        if (rtlsdr_set_bias_tee(dev, 1) != 0)
            printf("rtlsdr: cannot switch the bias T on (continuing)\n");
#else
        printf("rtlsdr: this librtlsdr has no bias T control (continuing)\n");
#endif
    }

    rtlsdr_reset_buffer(dev);
    if (iq_ring_init() != 0)
    {
        printf("out of memory\n");
        goto close_dev;
    }
    if (pthread_create(&reader, NULL, rtl_reader, NULL) != 0)
    {
        printf("rtlsdr: cannot start the reader thread\n");
        iq_ring_free();
        goto close_dev;
    }

    consume_loop();
    failed = device_lost ? 1 : 0;
    if (stop_requested) printf("Stop requested - releasing the receiver\n");

    rtlsdr_cancel_async(dev);
    pthread_join(reader, NULL);
    iq_ring_free();
#ifdef HAVE_RTLSDR_BIAS_TEE
    if (iq_opt.bias_t) rtlsdr_set_bias_tee(dev, 0);
#endif

close_dev:
    rtlsdr_close(dev);
    dev = NULL;
    return failed;
}

#else  /* !HAVE_LIBRTLSDR */

int rtlsdr_source_run(void)
{
    printf("This build has no RTL-SDR support (librtlsdr-dev was not found when it was compiled)\n");
    return 1;
}

#endif
