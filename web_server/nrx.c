/*
 * Navtex · NMEA 0183 output of the received messages (NRX sentences)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS (fork EA7JRS/Navtex). See COPYING.md.
 *
 * NAVTEX has no standard NMEA 2000 message; its standard output is the NRX
 * sentence of NMEA 0183 / IEC 61162-1 (IMO MSC.148(77), 9: "at least one
 * interface for the transfer of received data to other navigation or
 * communication equipment"). The sentences go out by UDP and to the clients
 * of a small TCP server. Nothing is ever read from those clients.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
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

char *
nrx_sentences (const char *bbbb, int freq, const char *received, const char *message)
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

/* A client that hung up, or that does not read, is dropped */
static void
send_tcp (const char *text, size_t n)
{
  int i;
  for (i = 0; i < MAX_CLIENTS; i++)
    {
      char junk[64];
      ssize_t r;
      if (clients[i] < 0) continue;
      r = recv (clients[i], junk, sizeof (junk), 0);        /* what a client says is thrown away */
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

static void *
sender (void *arg)
{
  int i;
  (void) arg;
  for (i = 0; i < MAX_CLIENTS; i++) clients[i] = -1;
  for (;;)
    {
      char udp[96], tcp[16];
      int port, count = 0;
      char *ml;
      cJSON *list, *m;

      get_setting ("nrx.udp", udp, sizeof (udp));
      get_setting ("nrx.tcp", tcp, sizeof (tcp));
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
          char when[20];
          char *s;

          if (!cJSON_IsNumber (id)) continue;
          pthread_mutex_lock (&lock);
          if ((long) id->valuedouble > last_id) last_id = (long) id->valuedouble;
          pthread_mutex_unlock (&lock);
          if ((udp[0] == '\0' && port == 0) || !cJSON_IsString (bbbb) || !cJSON_IsNumber (freq) || !cJSON_IsString (rec) || !cJSON_IsString (text)) continue;
          /* "2026-11-22T07:12:00Z" -> "2026-11-22 07:12" */
          snprintf (when, sizeof (when), "%.10s %.5s", rec->valuestring, strlen (rec->valuestring) >= 16 ? rec->valuestring + 11 : "");
          s = nrx_sentences (bbbb->valuestring, (int) freq->valuedouble, when, text->valuestring);
          if (s == NULL) continue;
          set_error (NULL, NULL);
          if (udp[0] != '\0') send_udp (udp, s);
          if (port != 0) send_tcp (s, strlen (s));
          free (s);
          count++;
        }
      cJSON_Delete (list);
      free (ml);

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
