/*
 * Navtex · NMEA 0183 output of the received messages (NRX sentences)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS (fork EA7JRS/Navtex). See COPYING.md.
 *
 * NAVTEX has no standard NMEA 2000 message; its standard output is the NRX
 * sentence of NMEA 0183 / IEC 61162-1 (IMO MSC.508(105), 9: "at least one
 * interface for the transfer of received data to other navigation or
 * communication equipment"). The sentences go out by UDP and to the clients
 * of a small TCP server. From those clients only three sentences are taken:
 * ACK (resets the search and rescue alarm), NRM (stations and subjects
 * followed, or a request for the stored messages) and its query, CRQ.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <cjson/cJSON.h>
#include "nrx.h"
#include "../receiver/message_store.h"

#define MAX_SENTENCE 82          /* "$" ... "*hh", without CR LF (IEC 61162-1) */
#define MAX_CLIENTS 8

/* -------------------------------------------------------------- sentences */

static int
append (char **buf, size_t *len, size_t *cap, const char *text, size_t n)
{
  if (*len + n + 1 > *cap)
    {
      size_t want = (*cap + n + 1) * 2;
      char *nb = (char *) realloc (*buf, want);
      if (nb == NULL) return 0;
      *buf = nb;
      *cap = want;
    }
  memcpy (*buf + *len, text, n);
  *len += n;
  (*buf)[*len] = '\0';
  return 1;
}

/* Reserved and non-printable characters as ^hh */
static size_t
escape_nmea (const char *in, size_t n, char *out, size_t out_size)
{
  size_t o = 0, i;

  for (i = 0; i < n && o + 4 < out_size; i++)
    {
      unsigned char c = (unsigned char) in[i];
      if (strchr (",*$!\\^~", c) != NULL || c < 32 || c > 126)
        o += (size_t) snprintf (out + o, out_size - o, "^%02X", c);
      else
        out[o++] = (char) c;
    }
  out[o] = '\0';
  return o;
}

static int
add_sentence (char **buf, size_t *len, size_t *cap, const char *head_fmt, int line, int lines, const char *piece, size_t piece_len)
{
  char body[160];
  char tail[8];
  unsigned char sum = 0;
  int n = snprintf (body, sizeof (body), head_fmt, line, lines);
  size_t i;

  if (n < 0 || (size_t) n + piece_len >= sizeof (body)) return 0;
  memcpy (body + n, piece, piece_len);
  body[n + piece_len] = '\0';
  for (i = 0; body[i] != '\0'; i++) sum ^= (unsigned char) body[i];
  snprintf (tail, sizeof (tail), "*%02X\r\n", sum);
  return append (buf, len, cap, "$", 1) && append (buf, len, cap, body, strlen (body)) && append (buf, len, cap, tail, strlen (tail));
}

static int
add_raw (char **buf, size_t *len, size_t *cap, const char *body)
{
  char tail[8];
  unsigned char sum = 0;
  size_t i;
  for (i = 0; body[i] != '\0'; i++) sum ^= (unsigned char) body[i];
  snprintf (tail, sizeof (tail), "*%02X\r\n", sum);
  return append (buf, len, cap, "$", 1) && append (buf, len, cap, body, strlen (body)) && append (buf, len, cap, tail, strlen (tail));
}

/* IEC 61097-6, annex C: the body is one stream of characters cut into sentences */
static char *
nrx_iec (const char *bbbb, int freq, int y, int mo, int d, int h, int mi, const char *body, int seq, int complete)
{
  char *esc, *out = NULL;
  size_t olen = 0, ocap = 0, n = strlen (body), e = 0, at, i, chars = 0;
  int total, line, ok = 1, bad = count_bad_characters (body);
  char first[80], next[40], sentence[128];
  size_t room_first, room_next;

  esc = (char *) malloc (n * 6 + 1);
  if (esc == NULL) return NULL;
  for (i = 0; i < n; i++)
    {
      unsigned char c = (unsigned char) body[i];
      if (c == '\r') continue;
      if (c == '\n') { memcpy (esc + e, "^0D^0A", 6); e += 6; chars += 2; continue; }   /* a line end is two characters */
      chars++;
      if (strchr (",*$!\\^~", c) != NULL || c < 32 || c > 126) e += (size_t) sprintf (esc + e, "^%02X", c);
      else esc[e++] = (char) c;
    }
  esc[e] = '\0';

  /* frequency table index: 1 = 490 kHz, 2 = 518 kHz, 3 = 4209.5 kHz */
  snprintf (first, sizeof (first), ",%02d,%.4s,%d,%02d%02d00,%02d,%02d,%04d,%d,%d,%c,", seq % 100, bbbb,
            freq == 490 ? 1 : freq == 4209 ? 3 : 2, h, mi, d, mo, y, (int) chars, bad, complete ? 'A' : 'V');
  snprintf (next, sizeof (next), ",%02d,,,,,,,,,,", seq % 100);
  /* 82 = "$CRNRX,nnn,nnn" + fields + text + "*hh" */
  room_first = (size_t) MAX_SENTENCE - 17 - strlen (first);
  room_next = (size_t) MAX_SENTENCE - 17 - strlen (next);

  /* how many sentences: the first holds less than the others; an escape is never cut */
  for (total = 0, at = 0; at < e || total == 0; total++)
    {
      size_t take = e - at < (total == 0 ? room_first : room_next) ? e - at : (total == 0 ? room_first : room_next);
      if (take < e - at)
        for (i = 1; i <= 2 && i <= take; i++)
          if (esc[at + take - i] == '^') { take -= i; break; }
      at += take;
      if (total >= 998) break;
    }
  for (line = 1, at = 0; ok && line <= total; line++)
    {
      size_t room = line == 1 ? room_first : room_next;
      size_t take = e - at < room ? e - at : room;
      if (take < e - at)
        for (i = 1; i <= 2 && i <= take; i++)
          if (esc[at + take - i] == '^') { take -= i; break; }
      snprintf (sentence, sizeof (sentence), "CRNRX,%03d,%03d%s%.*s", total, line, line == 1 ? first : next, (int) take, esc + at);
      ok = add_raw (&out, &olen, &ocap, sentence);
      at += take;
    }
  free (esc);
  if (!ok) { free (out); return NULL; }
  return out;
}

void
alr_sentence (char *out, size_t out_size, int hhmmss, int active, int acknowledged)
{
  char body[80];
  unsigned char sum = 0;
  size_t i;

  snprintf (body, sizeof (body), "CRALR,%06d.00,003,%c,%c,NAVTEX: Search and rescue information", hhmmss, active ? 'A' : 'V', acknowledged ? 'A' : 'V');
  for (i = 0; body[i] != '\0'; i++) sum ^= (unsigned char) body[i];
  snprintf (out, out_size, "$%s*%02X\r\n", body, sum);
}

int
is_alarm_ack (const char *text)
{
  const char *p;

  for (p = text; (p = strchr (p, '$')) != NULL; p++)
    {
      unsigned char sum = 0;
      unsigned int given = 0, number = 0;
      const char *q;
      for (q = p + 1; *q != '\0' && *q != '*' && *q != '\r' && *q != '\n' && q - p < 40; q++) sum ^= (unsigned char) *q;
      if (*q != '*' || sscanf (q + 1, "%2X", &given) != 1 || given != sum) continue;
      if (q - p >= 9 && strncmp (p + 3, "ACK,", 4) == 0 && sscanf (p + 7, "%3u", &number) == 1 && number == 3) return 1;
    }
  return 0;
}

/* ------------------------------------------------------------ bridge alert management */

static void
with_checksum (char *out, size_t out_size, const char *body)
{
  unsigned char sum = 0;
  size_t i;
  for (i = 0; body[i] != '\0'; i++) sum ^= (unsigned char) body[i];
  snprintf (out, out_size, "$%s*%02X\r\n", body, sum);
}

void
alf_sentence (char *out, size_t out_size, int hhmmss, char state, int revision)
{
  char body[80];
  snprintf (body, sizeof (body), "CRALF,1,1,0,%06d.00,B,W,%c,,3,1,%d,0,NAVTEX SAR", hhmmss, state, revision < 1 ? 1 : revision > 99 ? 99 : revision);
  with_checksum (out, out_size, body);
}

void
alc_sentence (char *out, size_t out_size, int active, int revision)
{
  char body[48];
  if (active) snprintf (body, sizeof (body), "CRALC,01,01,00,1,,3,1,%d", revision < 1 ? 1 : revision > 99 ? 99 : revision);
  else snprintf (body, sizeof (body), "CRALC,01,01,00,0");
  with_checksum (out, out_size, body);
}

void
hbt_sentence (char *out, size_t out_size, int interval, int seq)
{
  char body[32];
  snprintf (body, sizeof (body), "CRHBT,%d,A,%d", interval, ((seq % 10) + 10) % 10);
  with_checksum (out, out_size, body);
}

void
arc_sentence (char *out, size_t out_size, int hhmmss, long identifier, long instance, char command)
{
  char body[64];
  snprintf (body, sizeof (body), "CRARC,%06d.00,,%ld,%ld,%c", hhmmss, identifier, instance, command);
  with_checksum (out, out_size, body);
}

int
acn_parse (const char *text, long *identifier, long *instance, char *command)
{
  const char *p;

  for (p = text; (p = strchr (p, '$')) != NULL; p++)
    {
      unsigned char sum = 0;
      unsigned int given = 0;
      const char *q, *f[6];
      int n = 0;
      char *end;
      long id, inst;

      for (q = p + 1; *q != '\0' && *q != '*' && *q != '\r' && *q != '\n' && q - p < 80; q++) sum ^= (unsigned char) *q;
      if (*q != '*' || sscanf (q + 1, "%2X", &given) != 1 || given != sum) continue;
      if (q - p < 8 || strncmp (p + 3, "ACN,", 4) != 0) continue;
      /* the six fields: time, manufacturer, identifier, instance, command, flag */
      f[n++] = p + 7;
      for (const char *c = p + 7; c < q && n < 6; c++) if (*c == ',') f[n++] = c + 1;
      if (n != 6) continue;
      if (f[2] - f[1] != 1) continue;                       /* a manufacturer code: not one of ours */
      id = strtol (f[2], &end, 10);
      if (end == f[2] || *end != ',' || id < 0) continue;
      inst = strtol (f[3], &end, 10);
      if (*end != ',' || inst < 0) continue;
      if (f[5] - f[4] != 2 || strchr ("AQOS", f[4][0]) == NULL || f[4][0] == '\0') continue;
      if (f[5][0] != 'C' || f[5] + 1 != q) continue;
      *identifier = id;
      *instance = inst;
      *command = f[4][0];
      return 1;
    }
  return 0;
}

/* ------------------------------------------------------------ NRM and CRQ */

/* Length of the sentence that starts at p ("$...*hh") when its checksum is right, else 0 */
static size_t
valid_sentence (const char *p)
{
  unsigned char sum = 0;
  unsigned int given = 0;
  const char *q;

  if (*p != '$') return 0;
  for (q = p + 1; *q != '\0' && *q != '*' && *q != '\r' && *q != '\n' && q - p < 80; q++) sum ^= (unsigned char) *q;
  if (*q != '*' || sscanf (q + 1, "%2X", &given) != 1 || given != sum) return 0;
  return (size_t) (q - p) + 3;
}

unsigned long
letters_to_mask (const char *letters)
{
  unsigned long m = 0;
  for (; *letters != '\0'; letters++)
    if (*letters >= 'A' && *letters <= 'Z') m |= 1UL << (*letters - 'A');
  return m;
}

void
mask_to_letters (unsigned long mask, char *out)
{
  int i, n = 0;
  for (i = 0; i < 26; i++)
    if (mask & (1UL << i)) out[n++] = (char) ('A' + i);
  out[n] = '\0';
}

int
nrm_parse (const char *sentence, int *function, int *freq, unsigned long *areas, unsigned long *types)
{
  int f, index;
  unsigned long a, t;
  char end;

  if (valid_sentence (sentence) == 0 || strncmp (sentence + 3, "NRM,", 4) != 0) return 0;
  /* exactly: one digit, one digit, eight hex digits, eight hex digits */
  if (sscanf (sentence + 7, "%1d,%1d,%8lx,%8lx%c", &f, &index, &a, &t, &end) != 5 || end != '*') return 0;
  if (strspn (sentence + 11, "0123456789ABCDEFabcdef") != 8 || strspn (sentence + 20, "0123456789ABCDEFabcdef") != 8) return 0;
  if (index < 1 || index > 3) return 0;
  /* bits 26-31 are reserved; the standard's own example sets some of them, so they are just dropped */
  a &= 0x3ffffffUL;
  t &= 0x3ffffffUL;
  *function = f;
  *freq = index == 1 ? 490 : index == 2 ? 518 : 4209;
  *areas = a;
  *types = t;
  return 1;
}

void
nrm_sentence (char *out, size_t out_size, int function, int freq, unsigned long areas, unsigned long types)
{
  char body[48];
  unsigned char sum = 0;
  size_t i;

  snprintf (body, sizeof (body), "CRNRM,%d,%d,%08lX,%08lX", function, freq == 490 ? 1 : freq == 4209 ? 3 : 2, areas & 0x3ffffffUL, types & 0x3ffffffUL);
  for (i = 0; body[i] != '\0'; i++) sum ^= (unsigned char) body[i];
  snprintf (out, out_size, "$%s*%02X\r\n", body, sum);
}

int
is_nrm_query (const char *text)
{
  const char *p;
  for (p = text; (p = strchr (p, '$')) != NULL; p++)
    if (valid_sentence (p) == 13 && strncmp (p + 3, "CRQ,NRM", 7) == 0) return 1;
  return 0;
}

char *
nrx_sentences (const char *bbbb, int freq, const char *received, const char *message, int iec, int seq, int complete)
{
  char head[96];
  char *pieces = NULL;             /* the pieces, one after another, each ended with '\n' */
  size_t plen = 0, pcap = 0;
  char *out = NULL;
  size_t olen = 0, ocap = 0;
  int y = 0, mo = 0, d = 0, h = 0, mi = 0;
  int total = 0, line, ok = 1;
  int length, errors;
  size_t room;
  const char *p, *body;

  if (bbbb == NULL || strlen (bbbb) != 4 || message == NULL) return NULL;
  if (received == NULL || sscanf (received, "%4d-%2d-%2d %2d:%2d", &y, &mo, &d, &h, &mi) != 5) return NULL;
  length = (int) strlen (message);
  errors = count_bad_characters (message);

  /* the text without its header line: the identity already has its own field */
  body = message;
  if (strncmp (body, "ZCZC", 4) == 0)
    {
      const char *nl = strchr (body, '\n');
      body = nl != NULL ? nl + 1 : body + strlen (body);
    }

  if (iec) return nrx_iec (bbbb, freq, y, mo, d, h, mi, body, seq, complete);

  snprintf (head, sizeof (head), "CRNRX,%.4s,%%d,%%d,%s,%02d,%02d,%04d,%02d%02d00,%.1f,", bbbb,
            freq == 4209 ? "4209" : freq == 490 ? "490" : "518", d, mo, y, h, mi, length > 0 ? 100.0 * errors / length : 0.0);
  /* 82 = "$" + head (its two "%d" up to 3 digits each) + text + "*hh" */
  room = (size_t) (MAX_SENTENCE - 6) - strlen (head);

  for (p = body; ok; )
    {
      const char *nl = strchr (p, '\n');
      size_t n = nl != NULL ? (size_t) (nl - p) : strlen (p);
      char esc[4 * 1024];
      size_t e, at = 0;

      while (n > 0 && p[n - 1] == '\r') n--;
      if (nl == NULL && n == 0) break;                    /* nothing after the last line feed */
      if (n > sizeof (esc) / 4) n = sizeof (esc) / 4;
      e = escape_nmea (p, n, esc, sizeof (esc));
      if (e == 0) { ok = append (&pieces, &plen, &pcap, "\n", 1); total++; }
      while (ok && at < e)
        {
          size_t take = e - at < room ? e - at : room;
          if (take < e - at)
            {
              size_t i;
              for (i = take; i > take / 2 + 1; i--)           /* at a space when possible */
                if (esc[at + i - 1] == ' ') { take = i; break; }
              for (i = 1; i <= 2 && i <= take; i++)           /* never inside ^hh */
                if (esc[at + take - i] == '^') { take -= i; break; }
              if (take == 0) take = e - at < room ? e - at : room;
            }
          ok = append (&pieces, &plen, &pcap, esc + at, take) && append (&pieces, &plen, &pcap, "\n", 1);
          at += take;
          total++;
        }
      if (nl == NULL) break;
      p = nl + 1;
    }
  if (ok && total == 0) { ok = append (&pieces, &plen, &pcap, "\n", 1); total = 1; }
  if (total > 999) total = 999;
  for (p = pieces, line = 1; ok && line <= total; line++)
    {
      const char *nl = strchr (p, '\n');
      size_t n = (size_t) (nl - p);
      while (n > 0 && p[n - 1] == ' ') n--;                   /* no trailing spaces */
      ok = add_sentence (&out, &olen, &ocap, head, line, total, p, n);
      p = nl + 1;
    }
  free (pieces);
  if (!ok) { free (out); return NULL; }
  return out;
}

/* ------------------------------------------------------------------ sender */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char cur_udp[96];
static int cur_tcp, n_clients;
static long sent_messages, last_id = -1;
static char last_error[120];

static int listener = -1, listener_port;
static int clients[MAX_CLIENTS];

static void
set_error (const char *what, const char *detail)
{
  pthread_mutex_lock (&lock);
  if (what == NULL) last_error[0] = '\0';
  else snprintf (last_error, sizeof (last_error), "%s: %s", what, detail);
  pthread_mutex_unlock (&lock);
}

static void
close_listener (void)
{
  int i;
  if (listener >= 0) close (listener);
  listener = -1;
  listener_port = 0;
  for (i = 0; i < MAX_CLIENTS; i++)
    if (clients[i] >= 0) { close (clients[i]); clients[i] = -1; }
}

static void
serve_tcp (int port)
{
  struct sockaddr_in a;
  int one = 1, fd, i;

  if (port != listener_port) close_listener ();
  if (port == 0) return;
  if (listener < 0)
    {
      listener = socket (AF_INET, SOCK_STREAM, 0);
      if (listener < 0) return;
      setsockopt (listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof (one));
      memset (&a, 0, sizeof (a));
      a.sin_family = AF_INET;
      a.sin_addr.s_addr = htonl (INADDR_ANY);
      a.sin_port = htons ((unsigned short) port);
      if (bind (listener, (struct sockaddr *) &a, sizeof (a)) != 0 || listen (listener, 4) != 0)
        {
          set_error ("TCP", strerror (errno));
          close (listener);
          listener = -1;
          return;
        }
      fcntl (listener, F_SETFL, fcntl (listener, F_GETFL, 0) | O_NONBLOCK);
      listener_port = port;
      printf ("NRX: TCP server on port %d\n", port);
      fflush (stdout);
    }
  while ((fd = accept (listener, NULL, NULL)) >= 0)
    {
      for (i = 0; i < MAX_CLIENTS && clients[i] >= 0; i++) ;
      if (i == MAX_CLIENTS) { close (fd); continue; }       /* full */
      fcntl (fd, F_SETFL, fcntl (fd, F_GETFL, 0) | O_NONBLOCK);
      clients[i] = fd;
    }
}

static const int FREQS[3] = { 490, 518, 4209 };
static const char *FREQ_TAG[3] = { "490", "518", "4209" };
static int out_iec, out_seq;         /* form of the NRX sentences, and the next message number */

/* The mask of what is followed on one frequency, as the web interface keeps it */
static void
followed (int k, unsigned long *areas, unsigned long *types)
{
  char tag[24], letters[40];
  snprintf (tag, sizeof (tag), "stations%s", FREQ_TAG[k]);
  get_config_value (tag, letters, sizeof (letters));
  *areas = letters_to_mask (letters);
  snprintf (tag, sizeof (tag), "messages%s", FREQ_TAG[k]);
  get_config_value (tag, letters, sizeof (letters));
  /* categories A, B, D and L cannot be excluded (IEC 61097-6, 4.3.7) */
  *types = letters_to_mask (letters) | letters_to_mask ("ABDL");
}

/* What a client may ask with NRM and CRQ (IEC 61097-6, annex C.2) */
static void
answer_client (int fd, const char *said)
{
  const char *p;
  char line[64];
  int k;

  if (is_nrm_query (said))
    for (k = 0; k < 3; k++)
      {
        unsigned long areas, types;
        followed (k, &areas, &types);
        nrm_sentence (line, sizeof (line), 1, FREQS[k], areas, types);
        send (fd, line, strlen (line), MSG_NOSIGNAL);
      }
  for (p = said; (p = strchr (p, '$')) != NULL; p++)
    {
      int function, freq;
      unsigned long areas, types;
      char letters[27], tag[24];

      if (!nrm_parse (p, &function, &freq, &areas, &types)) continue;
      k = freq == 490 ? 0 : freq == 518 ? 1 : 2;
      if (function == 1)
        {
          /* the storage mask: here, the stations and subjects that are followed */
          mask_to_letters (areas, letters);
          snprintf (tag, sizeof (tag), "stations%s", FREQ_TAG[k]);
          update_config (tag, letters);
          mask_to_letters (types | letters_to_mask ("ABDL"), letters);
          snprintf (tag, sizeof (tag), "messages%s", FREQ_TAG[k]);
          update_config (tag, letters);
          printf ("NRX: a TCP client set the stations and subjects followed on %s kHz\n", FREQ_TAG[k]);
          fflush (stdout);
          followed (k, &areas, &types);
          nrm_sentence (line, sizeof (line), 1, freq, areas, types);
          send (fd, line, strlen (line), MSG_NOSIGNAL);
        }
      else if (function == 0)
        {
          /* the stored messages that match the mask, as NRX sentences; no mask is changed */
          char *ml = get_messages_since (0, 500);
          cJSON *list = ml != NULL ? cJSON_Parse (ml) : NULL, *m;
          cJSON_ArrayForEach (m, list)
            {
              cJSON *bbbb = cJSON_GetObjectItemCaseSensitive (m, "bbbb");
              cJSON *f = cJSON_GetObjectItemCaseSensitive (m, "freq");
              cJSON *rec = cJSON_GetObjectItemCaseSensitive (m, "received");
              cJSON *text = cJSON_GetObjectItemCaseSensitive (m, "message");
              char when[20];
              char *s;
              if (!cJSON_IsString (bbbb) || strlen (bbbb->valuestring) != 4 || !cJSON_IsNumber (f) || (int) f->valuedouble != freq || !cJSON_IsString (rec) || !cJSON_IsString (text)) continue;
              if (bbbb->valuestring[0] < 'A' || bbbb->valuestring[0] > 'Z' || bbbb->valuestring[1] < 'A' || bbbb->valuestring[1] > 'Z') continue;
              if (!(areas & (1UL << (bbbb->valuestring[0] - 'A'))) || !(types & (1UL << (bbbb->valuestring[1] - 'A')))) continue;
              snprintf (when, sizeof (when), "%.10s %.5s", rec->valuestring, strlen (rec->valuestring) >= 16 ? rec->valuestring + 11 : "");
              s = nrx_sentences (bbbb->valuestring, freq, when, text->valuestring, out_iec, out_seq, !cJSON_IsFalse (cJSON_GetObjectItemCaseSensitive (m, "complete")));
              out_seq = (out_seq + 1) % 100;
              if (s != NULL) { send (fd, s, strlen (s), MSG_NOSIGNAL); free (s); }
            }
          cJSON_Delete (list);
          free (ml);
        }
      /* functions 2 and 3 (printer and INS masks) do not exist here: nothing to do */
    }
}

static int bam_repeat;               /* a client asked for the alert again (ACN with command Q) */
static int utc_hhmmss (void);

/* A client that hung up, or that does not read, is dropped */
static void
send_tcp (const char *text, size_t n)
{
  int i;
  for (i = 0; i < MAX_CLIENTS; i++)
    {
      char said[128];
      ssize_t r;
      if (clients[i] < 0) continue;
      /* The only thing a client can say: the ACK sentence that resets the alarm (IEC 61097-6, 8.4.2) */
      r = recv (clients[i], said, sizeof (said) - 1, 0);
      if (r > 0)
        {
          said[r] = '\0';
          if (is_alarm_ack (said) && acknowledge_alarms () > 0) { printf ("NRX: alarm acknowledged by a TCP client\n"); fflush (stdout); }
          {
            long alert, instance;
            char command;
            /* Bridge alert management: acknowledge, or repeat the alert; this program has no sound to silence here and
               cannot hand the alert over, so those two are answered with ARC */
            if (acn_parse (said, &alert, &instance, &command) && (alert == 3 || (alert == 0 && command == 'Q')))
              {
                if (command == 'A') { if (acknowledge_alarms () > 0) { printf ("NRX: alert acknowledged by a TCP client (ACN)\n"); fflush (stdout); } }
                else if (command == 'Q') bam_repeat = 1;
                else
                  {
                    char arc[96];
                    arc_sentence (arc, sizeof (arc), utc_hhmmss (), alert, instance, command);
                    send (clients[i], arc, strlen (arc), MSG_NOSIGNAL);
                  }
              }
          }
          answer_client (clients[i], said);
        }
      if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) ||
          (text != NULL && send (clients[i], text, n, MSG_NOSIGNAL) != (ssize_t) n))
        {
          close (clients[i]);
          clients[i] = -1;
        }
    }
}

/* One datagram per sentence, as NMEA 0183 over UDP is usually sent */
static void
send_udp (const char *dest, const char *text)
{
  char host[96];
  char *colon;
  struct addrinfo hints, *res = NULL;
  int fd, one = 1, rc;
  const char *p;

  snprintf (host, sizeof (host), "%s", dest);
  colon = strrchr (host, ':');
  if (colon == NULL) return;
  *colon = '\0';
  memset (&hints, 0, sizeof (hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  rc = getaddrinfo (host, colon + 1, &hints, &res);
  if (rc != 0 || res == NULL) { set_error ("UDP", gai_strerror (rc)); return; }
  fd = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd >= 0)
    {
      setsockopt (fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof (one));
      for (p = text; *p != '\0'; )
        {
          const char *nl = strchr (p, '\n');
          size_t n = nl != NULL ? (size_t) (nl - p) + 1 : strlen (p);
          if (sendto (fd, p, n, 0, res->ai_addr, res->ai_addrlen) < 0) { set_error ("UDP", strerror (errno)); break; }
          p += n;
        }
      close (fd);
    }
  freeaddrinfo (res);
}

static long
stored_last_id (void)
{
  char *st = get_status_json ();
  cJSON *j = st != NULL ? cJSON_Parse (st) : NULL;
  cJSON *v = j != NULL ? cJSON_GetObjectItemCaseSensitive (j, "last_id") : NULL;
  long id = cJSON_IsNumber (v) ? (long) v->valuedouble : 0;
  cJSON_Delete (j);
  free (st);
  return id;
}

static int
utc_hhmmss (void)
{
  time_t now = time (NULL);
  struct tm t;
  gmtime_r (&now, &t);
  return t.tm_hour * 10000 + t.tm_min * 100 + t.tm_sec;
}

static void *
sender (void *arg)
{
  int i;
  int alarm_was = 0;
  time_t last_alr = 0, last_bam = 0;
  int bam_revision = 1, bam_seq = 0, bam_was = -1;
  (void) arg;
  for (i = 0; i < MAX_CLIENTS; i++) clients[i] = -1;
  for (;;)
    {
      char udp[96], tcp[16], format[16], bam[8];
      int port, count = 0, iec, alarms;
      char *ml;
      cJSON *list, *m;

      get_setting ("nrx.udp", udp, sizeof (udp));
      get_setting ("nrx.tcp", tcp, sizeof (tcp));
      get_setting ("nrx.format", format, sizeof (format));
      get_setting ("nrx.bam", bam, sizeof (bam));
      iec = strcmp (format, "iec") == 0;
      out_iec = iec;
      port = atoi (tcp);
      serve_tcp (port);
      send_tcp (NULL, 0);

      /* Only what arrives from now on: the first time, start after the last stored message */
      if (last_id < 0) { pthread_mutex_lock (&lock); last_id = stored_last_id (); pthread_mutex_unlock (&lock); }
      ml = get_messages_since ((unsigned int) last_id, 50);
      list = ml != NULL ? cJSON_Parse (ml) : NULL;
      cJSON_ArrayForEach (m, list)
        {
          cJSON *id = cJSON_GetObjectItemCaseSensitive (m, "id");
          cJSON *bbbb = cJSON_GetObjectItemCaseSensitive (m, "bbbb");
          cJSON *freq = cJSON_GetObjectItemCaseSensitive (m, "freq");
          cJSON *rec = cJSON_GetObjectItemCaseSensitive (m, "received");
          cJSON *text = cJSON_GetObjectItemCaseSensitive (m, "message");
          cJSON *complete = cJSON_GetObjectItemCaseSensitive (m, "complete");
          char when[20];
          char *s;

          if (!cJSON_IsNumber (id)) continue;
          pthread_mutex_lock (&lock);
          if ((long) id->valuedouble > last_id) last_id = (long) id->valuedouble;
          pthread_mutex_unlock (&lock);
          if ((udp[0] == '\0' && port == 0) || !cJSON_IsString (bbbb) || !cJSON_IsNumber (freq) || !cJSON_IsString (rec) || !cJSON_IsString (text)) continue;
          /* "2026-11-22T07:12:00Z" -> "2026-11-22 07:12" */
          snprintf (when, sizeof (when), "%.10s %.5s", rec->valuestring, strlen (rec->valuestring) >= 16 ? rec->valuestring + 11 : "");
          s = nrx_sentences (bbbb->valuestring, (int) freq->valuedouble, when, text->valuestring, iec, out_seq, !cJSON_IsFalse (complete));
          out_seq = (out_seq + 1) % 100;
          if (s == NULL) continue;
          set_error (NULL, NULL);
          if (udp[0] != '\0') send_udp (udp, s);
          if (port != 0) send_tcp (s, strlen (s));
          free (s);
          count++;
        }
      cJSON_Delete (list);
      free (ml);

      /* Search and rescue alarm: the ALR sentence every 30 s until someone acknowledges it, and once more then */
      alarms = pending_alarms ();
      if ((udp[0] != '\0' || port != 0) && ((alarms > 0 && time (NULL) - last_alr >= 30) || (alarms == 0 && alarm_was)))
        {
          char alr[96];
          alr_sentence (alr, sizeof (alr), utc_hhmmss (), alarms > 0, alarms == 0);
          if (udp[0] != '\0') send_udp (udp, alr);
          if (port != 0) send_tcp (alr, strlen (alr));
          last_alr = time (NULL);
        }
      alarm_was = alarms > 0;

      /* Bridge alert management: the alert sentence on every change (and when asked), the list and the heartbeat every 30 s */
      if (strcmp (bam, "on") == 0 && (udp[0] != '\0' || port != 0))
        {
          char line[96];
          int active = alarms > 0;
          if (bam_was >= 0 && active != bam_was) bam_revision = bam_revision % 99 + 1;
          if ((bam_was >= 0 && active != bam_was) || (bam_was < 0 && active) || bam_repeat)
            {
              alf_sentence (line, sizeof (line), utc_hhmmss (), active ? 'V' : 'N', bam_revision);
              if (udp[0] != '\0') send_udp (udp, line);
              if (port != 0) send_tcp (line, strlen (line));
            }
          bam_was = active;
          bam_repeat = 0;
          if (time (NULL) - last_bam >= 30)
            {
              hbt_sentence (line, sizeof (line), 30, bam_seq++);
              if (udp[0] != '\0') send_udp (udp, line);
              if (port != 0) send_tcp (line, strlen (line));
              alc_sentence (line, sizeof (line), active, bam_revision);
              if (udp[0] != '\0') send_udp (udp, line);
              if (port != 0) send_tcp (line, strlen (line));
              last_bam = time (NULL);
            }
        }
      else { bam_was = -1; bam_repeat = 0; }

      pthread_mutex_lock (&lock);
      snprintf (cur_udp, sizeof (cur_udp), "%s", udp);
      cur_tcp = listener >= 0 ? listener_port : 0;
      for (i = 0, n_clients = 0; i < MAX_CLIENTS; i++) if (clients[i] >= 0) n_clients++;
      sent_messages += count;
      pthread_mutex_unlock (&lock);
      sleep (5);
    }
  return NULL;
}

void
nrx_start (void)
{
  pthread_t t;
  if (pthread_create (&t, NULL, sender, NULL) == 0) pthread_detach (t);
}

char *
nrx_json (void)
{
  cJSON *j = cJSON_CreateObject ();
  char *s, *r;

  pthread_mutex_lock (&lock);
  cJSON_AddStringToObject (j, "udp", cur_udp);
  cJSON_AddNumberToObject (j, "tcp", cur_tcp);
  cJSON_AddNumberToObject (j, "clients", n_clients);
  cJSON_AddNumberToObject (j, "sent", (double) sent_messages);
  cJSON_AddNumberToObject (j, "last_id", (double) (last_id < 0 ? 0 : last_id));
  cJSON_AddStringToObject (j, "error", last_error);
  pthread_mutex_unlock (&lock);
  s = cJSON_PrintUnformatted (j);
  r = s != NULL ? strdup (s) : NULL;
  cJSON_free (s);
  cJSON_Delete (j);
  return r;
}
