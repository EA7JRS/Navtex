/*
 * Navtex receiver: a KiwiSDR on the network as the receiver.
 *
 * A KiwiSDR gives 12 kHz of spectrum on each of its channels, and 518 and 490 kHz are 28 kHz
 * apart: on medium wave two of its channels are used, one on each frequency; on short wave, one.
 * Each is brought up to the 252 kS/s of the other sources and put where the demodulator looks for
 * it (14 kHz above or below the centre), and the two are added. No TLS: a KiwiSDR speaks plain
 * ws:// on its own port (8073).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "iq_source.h"
#include "kiwi_proto.h"

#define MAX_FRAME (4u << 20)
#define CONNECT_TIMEOUT_S 10
#define FIFO_PAIRS (IQ_SAMPLE_RATE * 3)     /* three seconds: more than that apart, the two channels are not together */
#define UP_MAX (8192 * 24)

double kiwi_lat = 0, kiwi_lon = 0;
volatile sig_atomic_t kiwi_has_position = 0;

struct conn {
    int sock;
    double khz, shift_hz, offset_khz, rssi;
    unsigned char *buf, *whole; size_t cap, have, whole_n;
    struct kiwi_resampler *rs;
    short *fifo; size_t fifo_n;             /* pairs waiting for the other channel */
    time_t last_alive;
    unsigned long stream_id;
};

static int n_conns;
static struct conn conns[2];

static int send_all(int sock, const void *p, size_t n)
{
    const char *c = p;
    while (n > 0) {
        ssize_t w = send(sock, c, n, MSG_NOSIGNAL);
        if (w <= 0) { if (w < 0 && errno == EINTR) continue; return -1; }
        c += w; n -= (size_t) w;
    }
    return 0;
}

static int send_text(int sock, const char *text)
{
    unsigned char f[600], mask[4];
    size_t n; int i;
    for (i = 0; i < 4; i++) mask[i] = (unsigned char) (rand() & 0xff);
    n = kiwi_ws_frame(f, sizeof(f), 1, text, strlen(text), mask);
    return n == 0 ? -1 : send_all(sock, f, n);
}

static int send_lines(int sock, char *lines)
{
    char *l = lines, *e;
    while ((e = strchr(l, '\n')) != NULL) { *e = '\0'; if (send_text(sock, l) != 0) return -1; l = e + 1; }
    return 0;
}

static int connect_to(const char *host, int port)
{
    struct addrinfo hints, *res, *a;
    char service[12]; int s = -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%d", port);
    if (getaddrinfo(host, service, &hints, &res) != 0) return -1;
    for (a = res; a != NULL; a = a->ai_next) {
        struct timeval tv = { CONNECT_TIMEOUT_S, 0 };
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s < 0) continue;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (connect(s, a->ai_addr, a->ai_addrlen) == 0) break;
        close(s); s = -1;
    }
    freeaddrinfo(res);
    return s;
}

static const char *refusal(const char *body, size_t len)
{
    char v[16];
    if (kiwi_msg_value(body, len, "too_busy", v, sizeof(v))) return "all its channels are taken";
    if (kiwi_msg_value(body, len, "badp", v, sizeof(v)) && v[0] != '0')
        return v[0] == '1' ? "wrong password, or all the channels that need none are taken" :
               v[0] == '5' ? "it does not allow two connections from the same address (Navtex needs two on medium wave)" : "it does not let this client in";
    if (kiwi_msg_value(body, len, "down", v, sizeof(v))) return "it is down for now";
    if (kiwi_msg_value(body, len, "redirect", v, sizeof(v))) return "it sends its visitors to another receiver";
    return NULL;
}

/* Opens one channel of the KiwiSDR. Returns 0 when the WebSocket is up and the password sent. */
static int conn_open(struct conn *c, const char *host, int port, const char *password, double khz, double shift_hz, int which)
{
    char req[512], ans[1024], auth[120];
    unsigned char key[16];
    size_t n, got = 0; int i;
    memset(c, 0, sizeof(*c));
    c->khz = khz; c->shift_hz = shift_hz; c->rssi = -127;
    c->sock = connect_to(host, port);
    if (c->sock < 0) { printf("kiwi: cannot connect to %s:%d\n", host, port); return -1; }
    for (i = 0; i < 16; i++) key[i] = (unsigned char) (rand() & 0xff);
    c->stream_id = ((unsigned long) (time(NULL) + getpid()) + (unsigned long) which) & 0xffffffffUL;
    n = kiwi_ws_request(req, sizeof(req), host, port, c->stream_id, key);
    if (n == 0 || send_all(c->sock, req, n) != 0) { printf("kiwi: cannot talk to %s:%d\n", host, port); return -1; }
    while (got < sizeof(ans) - 1) {
        ssize_t r = recv(c->sock, ans + got, 1, 0);
        if (r <= 0) break;
        got++; ans[got] = '\0';
        if (got >= 4 && memcmp(ans + got - 4, "\r\n\r\n", 4) == 0) break;
    }
    if (got == 0) ans[0] = '\0';
    if (!kiwi_ws_accepted(ans)) {
        ans[strcspn(ans, "\r\n")] = '\0';
        printf("kiwi: %s:%d does not answer as a KiwiSDR (%s)\n", host, port, got ? ans : "nothing");
        return -1;
    }
    snprintf(auth, sizeof(auth), "SET auth t=kiwi p=%s", password != NULL && password[0] ? password : "#");
    if (send_text(c->sock, auth) != 0) return -1;
    c->cap = 65536; c->buf = malloc(c->cap);
    c->fifo = malloc(sizeof(short) * 2 * FIFO_PAIRS);
    return c->buf != NULL && c->fifo != NULL ? 0 : -1;
}

static void conn_close(struct conn *c)
{
    if (c->sock >= 0) close(c->sock);
    kiwi_resampler_free(c->rs);
    free(c->buf); free(c->whole); free(c->fifo);
    memset(c, 0, sizeof(*c)); c->sock = -1;
}

/* Takes what the socket has and leaves the samples, at 252 kS/s and in their place, in the fifo.
   Returns 0, or -1 when this channel is over. */
static int conn_read(struct conn *c)
{
    static short pairs[8192], up[2 * UP_MAX];
    int opcode, fin, r;
    const unsigned char *pl; size_t pln, fln;
    ssize_t got;

    if (c->have == c->cap) {
        unsigned char *b2;
        if (c->cap >= MAX_FRAME + 16) { printf("kiwi: a message too large\n"); return -1; }
        b2 = realloc(c->buf, c->cap * 2); if (b2 == NULL) return -1;
        c->buf = b2; c->cap *= 2;
    }
    got = recv(c->sock, c->buf + c->have, c->cap - c->have, 0);
    if (got == 0) { if (!stop_requested && !reconfig_requested) printf("kiwi: the KiwiSDR closed the connection (a public one limits the time of each visitor)\n"); return -1; }
    if (got < 0) { if (errno == EINTR || errno == EAGAIN) return 0; if (!stop_requested && !reconfig_requested) printf("kiwi: connection lost\n"); return -1; }
    c->have += (size_t) got;

    while ((r = kiwi_ws_parse(c->buf, c->have, MAX_FRAME, &opcode, &fin, &pl, &pln, &fln)) == 1) {
        const unsigned char *msg = pl; size_t mn = pln;
        if (opcode == 8) { printf("kiwi: the KiwiSDR closed the connection (a public one limits the time of each visitor)\n"); return -1; }
        if (opcode == 9) {
            unsigned char f[140], mask[4] = { 1, 2, 3, 4 };
            size_t n = kiwi_ws_frame(f, sizeof(f), 10, pl, pln > 125 ? 125 : pln, mask);
            if (n) send_all(c->sock, f, n);
        } else if (opcode <= 2) {
            if (!fin || opcode == 0) {
                unsigned char *w2 = realloc(c->whole, c->whole_n + pln + 1);
                if (w2 == NULL) return -1;
                c->whole = w2; memcpy(c->whole + c->whole_n, pl, pln); c->whole_n += pln;
                msg = c->whole; mn = c->whole_n;
            }
            if (fin && mn >= 3) {
                if (memcmp(msg, "MSG", 3) == 0) {
                    const char *body = (const char *) msg + 3; size_t bl = mn - 3;
                    char v[40];
                    const char *why = refusal(body, bl);
                    if (why != NULL) { printf("kiwi: the KiwiSDR refuses: %s\n", why); return -1; }
                    if (kiwi_msg_value(body, bl, "freq_offset", v, sizeof(v))) c->offset_khz = atof(v);
                    if (kiwi_msg_value(body, bl, "audio_rate", v, sizeof(v))) {
                        char t[64]; snprintf(t, sizeof(t), "SET AR OK in=%d out=44100", atoi(v));
                        if (send_text(c->sock, t) != 0) return -1;
                    }
                    if (kiwi_msg_value(body, bl, "sample_rate", v, sizeof(v)) && c->rs == NULL) {
                        double rate = atof(v);
                        char t[400];
                        if (rate < 8000 || rate > 48000) { printf("kiwi: a sample rate that makes no sense (%s)\n", v); return -1; }
                        c->rs = kiwi_resampler_new(rate, IQ_SAMPLE_RATE, c->shift_hz);
                        if (c->rs == NULL || kiwi_setup_commands(t, sizeof(t), c->khz, c->offset_khz) == 0 || send_lines(c->sock, t) != 0) return -1;
                        printf("kiwi: %.1f kHz, %.3f samples a second\n", c->khz, rate);
                    }
                    if (bl > 9 && !kiwi_has_position) {
                        char *z = malloc(bl + 1);
                        if (z != NULL) {
                            memcpy(z, body, bl); z[bl] = '\0';
                            if (strstr(z, "load_cfg=") != NULL && kiwi_cfg_position(strstr(z, "load_cfg="), &kiwi_lat, &kiwi_lon)) {
                                kiwi_has_position = 1;
                                printf("kiwi: the receiver says it is at %.3f, %.3f\n", kiwi_lat, kiwi_lon);
                            }
                            free(z);
                        }
                    }
                } else if (memcmp(msg, "SND", 3) == 0 && c->rs != NULL) {
                    int n = kiwi_snd_iq(msg + 3, mn - 3, pairs, 8192, &c->rssi, NULL);
                    if (n < 0) { printf("kiwi: the KiwiSDR is not sending I/Q samples\n"); return -1; }
                    if (n > 0) {
                        size_t m = kiwi_resample(c->rs, pairs, (size_t) n, up, UP_MAX);
                        if (c->fifo_n + m > FIFO_PAIRS) { printf("kiwi: one of the two channels stopped\n"); return -1; }
                        memcpy(c->fifo + 2 * c->fifo_n, up, sizeof(short) * 2 * m); c->fifo_n += m;
                    }
                }
            }
            if (fin) { free(c->whole); c->whole = NULL; c->whole_n = 0; }
        }
        memmove(c->buf, c->buf + fln, c->have - fln); c->have -= fln;
    }
    if (r < 0) { printf("kiwi: what the KiwiSDR sends is not understood\n"); return -1; }
    return 0;
}

/* Hands on what all the channels have: one as it is, two added (each at half, so the sum fits) */
static void hand_on(void)
{
    static short xi[4096], xq[4096];
    size_t n = conns[0].fifo_n, done = 0, i; int k;
    for (k = 1; k < n_conns; k++) if (conns[k].fifo_n < n) n = conns[k].fifo_n;
    while (done < n) {
        size_t m = n - done > 4096 ? 4096 : n - done;
        if (n_conns == 1)
            for (i = 0; i < m; i++) { xi[i] = conns[0].fifo[2 * (done + i)]; xq[i] = conns[0].fifo[2 * (done + i) + 1]; }
        else
            for (i = 0; i < m; i++) {
                xi[i] = (short) (conns[0].fifo[2 * (done + i)] / 2 + conns[1].fifo[2 * (done + i)] / 2);
                xq[i] = (short) (conns[0].fifo[2 * (done + i) + 1] / 2 + conns[1].fifo[2 * (done + i) + 1] / 2);
            }
        iq_push_short(xi, xq, (unsigned int) m);
        done += m;
    }
    for (k = 0; k < n_conns; k++) {
        memmove(conns[k].fifo, conns[k].fifo + 2 * n, sizeof(short) * 2 * (conns[k].fifo_n - n));
        conns[k].fifo_n -= n;
    }
}

static void *kiwi_reader(void *arg)
{
    (void) arg;
    while (!stop_requested && !reconfig_requested && !device_lost) {
        struct pollfd p[2];
        int k, r;
        for (k = 0; k < n_conns; k++) {
            p[k].fd = conns[k].sock; p[k].events = POLLIN; p[k].revents = 0;
            if (time(NULL) != conns[k].last_alive) { conns[k].last_alive = time(NULL); if (send_text(conns[k].sock, "SET keepalive") != 0) goto out; }
        }
        r = poll(p, (nfds_t) n_conns, 500);
        if (r < 0 && errno != EINTR) break;
        for (k = 0; r > 0 && k < n_conns; k++)
            if ((p[k].revents & (POLLIN | POLLHUP | POLLERR)) && conn_read(&conns[k]) != 0) goto out;
        hand_on();
    }
out:
    if (!stop_requested && !reconfig_requested) device_lost = 1;
    return NULL;
}

int kiwi_source_run(void)
{
    pthread_t reader;
    int failed = 1, k, hf = iq_centre_hz() == IQ_CENTRE_HF_HZ;
    const char *host = iq_opt.kiwi_host;
    int port = iq_opt.kiwi_port > 0 ? iq_opt.kiwi_port : 8073;

    if (host == NULL || host[0] == '\0') { printf("kiwi: no KiwiSDR given (its name or address)\n"); return 1; }
    srand((unsigned) (time(NULL) ^ getpid()));
    conns[0].sock = conns[1].sock = -1;
    n_conns = hf ? 1 : 2;
    /* where the demodulator looks for each frequency: 14 kHz above the centre, and 490 kHz 14 below */
    if (conn_open(&conns[0], host, port, iq_opt.kiwi_password, hf ? 4209.5 : 518.0, 14000.0, 0) != 0) goto close_all;
    if (!hf && conn_open(&conns[1], host, port, iq_opt.kiwi_password, 490.0, -14000.0, 1) != 0) goto close_all;
    printf("kiwi: connected to %s:%d, %s\n", host, port, hf ? "one channel on 4209.5 kHz" : "two channels, on 518 and 490 kHz");

    if (iq_ring_init() != 0) { printf("out of memory\n"); goto close_all; }
    if (pthread_create(&reader, NULL, kiwi_reader, NULL) != 0) { printf("kiwi: cannot start the reader thread\n"); iq_ring_free(); goto close_all; }
    consume_loop();
    failed = device_lost ? 1 : 0;
    if (stop_requested) printf("Stop requested - leaving the KiwiSDR\n");
    for (k = 0; k < n_conns; k++) shutdown(conns[k].sock, SHUT_RDWR);
    pthread_join(reader, NULL);
    iq_ring_free();

close_all:
    for (k = 0; k < 2; k++) if (conns[k].sock >= 0 || conns[k].buf != NULL) conn_close(&conns[k]);
    return failed;
}
