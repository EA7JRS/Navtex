/* 
     This file is part of libmicrohttpd
     (C) 2007, 2008 Christian Grothoff (and other contributing authors)

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/
/**
 * @file https_server_example.c
 * @brief a simple HTTPS file server using TLS.
 *
 * Usage :
 *
 *  'http_fileserver_example HTTP-PORT SECONDS-TO-RUN'
 *
 * The certificate & key are required by the server to operate,  Omitting the
 * path arguments will cause the server to use the hard coded example certificate & key.
 *
 * 'certtool' may be used to generate these if required.
 *
 * @author Sagie Amir
 */



/*
 * Navtex web server: serves the web GUI and a small JSON API backed by the
 * SQLite message store.
 *
 * 2026 - Manuel - EA7JRS (fork EA7JRS/Navtex): request handling rewritten.
 *  - URLs are length-checked and never copied into fixed buffers;
 *  - /saveconfig only accepts the known tags and A-Z values;
 *  - changes need POST and the X-Navtex header (no cross-site requests);
 *  - static files cannot be requested outside the web root;
 *  - JSON answers are sent as application/json, files with their type;
 *  - optional listen address:  navtexwebserver [port] [address]
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <microhttpd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../receiver/message_store.h"
#include "gps.h"
#include "nrx.h"

#define MAX_URL_LEN 255

#define NOT_FOUND_PAGE "<html><head><title>File not found</title></head><body>File not found</body></html>"
#define BAD_REQUEST_PAGE "<html><head><title>Bad request</title></head><body>Bad request</body></html>"
#define ERROR_JSON "{\"error\":\"database not available\"}"

static ssize_t
file_reader (void *cls, uint64_t pos, char *buf, size_t max)
{
  FILE *file = cls;

  (void) fseek (file, pos, SEEK_SET);
  return fread (buf, 1, max, file);
}

static void
file_free_callback (void *cls)
{
  FILE *file = cls;
  fclose (file);
}

/* Queue a response made of a copy of `body`. */
static enum MHD_Result
send_text (struct MHD_Connection *connection, unsigned int status,
           const char *body, const char *content_type, int api)
{
  struct MHD_Response *response;
  enum MHD_Result ret;

  response = MHD_create_response_from_buffer (strlen (body), (void *) body,
                                              MHD_RESPMEM_MUST_COPY);
  if (response == NULL)
    return MHD_NO;
  MHD_add_response_header (response, "Content-Type", content_type);
  MHD_add_response_header (response, "X-Content-Type-Options", "nosniff");
  if (api)
  {
    MHD_add_response_header (response, MHD_HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");
    MHD_add_response_header (response, "Cache-Control", "no-store");
  }
  ret = MHD_queue_response (connection, status, response);
  MHD_destroy_response (response);
  return ret;
}

/* A file for the browser to save */
static enum MHD_Result
send_download (struct MHD_Connection *connection, const char *body, const char *content_type, const char *file_name)
{
  struct MHD_Response *response;
  enum MHD_Result ret;
  char disposition[96];

  response = MHD_create_response_from_buffer (strlen (body), (void *) body, MHD_RESPMEM_MUST_COPY);
  if (response == NULL)
    return MHD_NO;
  snprintf (disposition, sizeof (disposition), "attachment; filename=\"%s\"", file_name);
  MHD_add_response_header (response, "Content-Type", content_type);
  MHD_add_response_header (response, "Content-Disposition", disposition);
  MHD_add_response_header (response, "X-Content-Type-Options", "nosniff");
  MHD_add_response_header (response, "Cache-Control", "no-store");
  ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
  MHD_destroy_response (response);
  return ret;
}

static const char *
content_type_of (const char *path)
{
  const char *ext = strrchr (path, '.');

  if (ext == NULL) return "application/octet-stream";
  if (strcmp (ext, ".html") == 0) return "text/html; charset=UTF-8";
  if (strcmp (ext, ".js") == 0)   return "text/javascript; charset=UTF-8";
  if (strcmp (ext, ".css") == 0)  return "text/css; charset=UTF-8";
  if (strcmp (ext, ".json") == 0) return "application/json";
  if (strcmp (ext, ".map") == 0)  return "application/json";
  if (strcmp (ext, ".txt") == 0)  return "text/plain; charset=UTF-8";
  if (strcmp (ext, ".svg") == 0)  return "image/svg+xml";
  if (strcmp (ext, ".png") == 0)  return "image/png";
  if (strcmp (ext, ".ico") == 0)  return "image/x-icon";
  return "application/octet-stream";
}

/* A static path is safe when it is relative, has no ".." component and only
   uses plain file name characters. The server runs with the web root as its
   working directory, so such a path cannot leave it. */
static int
safe_static_path (const char *path)
{
  const char *p;

  if (path[0] == '\0' || path[0] == '/' || path[0] == '.')
    return 0;
  if (strstr (path, "..") != NULL || strstr (path, "//") != NULL)
    return 0;
  for (p = path; *p != '\0'; p++)
    if (!isalnum ((unsigned char) *p) && strchr ("._-/", *p) == NULL)
      return 0;
  return 1;
}

/* Changes are only accepted with the X-Navtex header. A browser cannot add it
   to a request made from another site without asking first, and the answer to
   that question is never yes: other pages cannot change our settings. */
static int
is_own_request (struct MHD_Connection *connection)
{
  return MHD_lookup_connection_value (connection, MHD_HEADER_KIND, "X-Navtex") != NULL;
}

/* POST /api/settings/<key>/<value>   (the value may be empty) */
static enum MHD_Result
handle_setting (struct MHD_Connection *connection, const char *url)
{
  char tag[24];
  char value[200];
  const char *p = url + strlen ("/api/settings/");
  const char *slash = strchr (p, '/');
  size_t tag_len, value_len;

  if (slash == NULL)
    return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
  tag_len = (size_t) (slash - p);
  value_len = strlen (slash + 1);
  if (tag_len == 0 || tag_len >= sizeof (tag) || value_len >= sizeof (value))
    return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
  memcpy (tag, p, tag_len);
  tag[tag_len] = '\0';
  memcpy (value, slash + 1, value_len + 1);
  if (set_setting (tag, value) != 0)
    return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
  return send_text (connection, MHD_HTTP_OK, "{}", "application/json", 0);
}

/* POST /saveconfig/<tag>/<value>   (the value may be empty) */
static enum MHD_Result
handle_saveconfig (struct MHD_Connection *connection, const char *url)
{
  char tag[16];
  char value[32];
  const char *p = url + strlen ("/saveconfig/");
  const char *slash = strchr (p, '/');
  size_t tag_len, value_len;

  if (slash == NULL)
    return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
  tag_len = (size_t) (slash - p);
  value_len = strlen (slash + 1);
  if (tag_len == 0 || tag_len >= sizeof (tag) || value_len >= sizeof (value))
    return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
  memcpy (tag, p, tag_len);
  tag[tag_len] = '\0';
  memcpy (value, slash + 1, value_len + 1);

  /* update_config() validates the tag and the value and uses a bound statement */
  if (update_config (tag, value) != 0)
    return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
  return send_text (connection, MHD_HTTP_OK, "{}", "application/json", 0);
}

/* HTTP access handler call back */
enum MHD_Result
http_ahc (void *cls,
          struct MHD_Connection *connection,
          const char *url,
          const char *method,
          const char *version,
          const char *upload_data,
          size_t *upload_data_size, void **ptr)
{
  static int aptr;
  struct MHD_Response *response;
  enum MHD_Result ret;
  FILE *file;
  struct stat buf;
  const char *path;

  (void) cls; (void) version; (void) upload_data;

  if ( (0 != strcmp (method, MHD_HTTP_METHOD_GET)) && (0 != strcmp (method, MHD_HTTP_METHOD_POST)) && (0 != strcmp (method, "OPTIONS")) )
    return MHD_NO;              /* unexpected method */
  if (&aptr != *ptr)
    {
      /* do never respond on first call */
      *ptr = &aptr;
      return MHD_YES;
    }
  if (*upload_data_size != 0)
    {
      /* a body is never expected: swallow it */
      *upload_data_size = 0;
      return MHD_YES;
    }
  *ptr = NULL;                  /* reset when done */

  if (url == NULL || url[0] != '/' || strlen (url) > MAX_URL_LEN)
    return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);

  if (strcmp (url, "/messagelist") == 0)
  {
      char *ml = get_message_list ();
      if (ml == NULL)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 1);
      ret = send_text (connection, MHD_HTTP_OK, ml, "application/json", 1);
      free (ml);
      return ret;
  }
  if (strcmp (url, "/config") == 0)
  {
      char *cfg = get_config ();
      if (cfg == NULL)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 1);
      ret = send_text (connection, MHD_HTTP_OK, cfg, "application/json", 1);
      free (cfg);
      return ret;
  }
  if (strcmp (url, "/api/messages") == 0)
  {
      /* Full messages newer than ?since=<id>, for other programs; nothing is marked as read */
      const char *since_arg = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "since");
      const char *limit_arg = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "limit");
      /* ...and those heard again after ?heard=<unix time>: what is still being broadcast */
      const char *heard_arg = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "heard");
      unsigned long since = 0, limit = 200, heard = 0;
      char *end;
      char *ml;

      if (since_arg != NULL)
      {
        errno = 0;
        since = strtoul (since_arg, &end, 10);
        if (!isdigit ((unsigned char) since_arg[0]) || errno != 0 || *end != '\0' || since > 0x7fffffffUL)
          return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      }
      if (limit_arg != NULL)
      {
        errno = 0;
        limit = strtoul (limit_arg, &end, 10);
        if (!isdigit ((unsigned char) limit_arg[0]) || errno != 0 || *end != '\0' || limit < 1 || limit > 500)
          return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      }
      if (heard_arg != NULL)
      {
        errno = 0;
        heard = strtoul (heard_arg, &end, 10);
        if (!isdigit ((unsigned char) heard_arg[0]) || errno != 0 || *end != '\0' || heard > 0x7fffffffUL)
          return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      }
      purge_old_messages ();
      ml = get_messages_changed ((unsigned int) since, (long) heard, (int) limit);
      if (ml == NULL)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 1);
      ret = send_text (connection, MHD_HTTP_OK, ml, "application/json", 1);
      free (ml);
      return ret;
  }
  if (strcmp (url, "/api/export") == 0)
  {
      /* Every stored message as a file: ?format=txt (default) or csv, ?freq=518|490|4209 */
      const char *format = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "format");
      const char *freq_arg = MHD_lookup_connection_value (connection, MHD_GET_ARGUMENT_KIND, "freq");
      int csv = format != NULL && strcmp (format, "csv") == 0;
      int freq = 0;
      char *ex;

      if (format != NULL && !csv && strcmp (format, "txt") != 0)
        return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      if (freq_arg != NULL)
      {
        if (strcmp (freq_arg, "518") == 0) freq = 518;
        else if (strcmp (freq_arg, "490") == 0) freq = 490;
        else if (strcmp (freq_arg, "4209") == 0) freq = 4209;
        else return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      }
      ex = export_messages (csv, freq);
      if (ex == NULL)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 1);
      ret = send_download (connection, ex, csv ? "text/csv; charset=UTF-8" : "text/plain; charset=UTF-8", csv ? "navtex.csv" : "navtex.txt");
      free (ex);
      return ret;
  }
  if (strcmp (url, "/api/nrx") == 0)
  {
      char *n = nrx_json ();
      if (n == NULL)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 0);
      ret = send_text (connection, MHD_HTTP_OK, n, "application/json", 0);
      free (n);
      return ret;
  }
  if (strncmp (url, "/api/messages/delete/", 21) == 0)
  {
      /* Gone since 1.7.0: the user must not be able to erase messages from the memory (IMO MSC.508(105), 6.1.1).
         They leave on their own when their retention time is up. */
      return send_text (connection, MHD_HTTP_GONE, "{\"error\":\"messages cannot be deleted (IMO MSC.508(105), 6.1.1)\"}", "application/json", 0);
  }
  if (strncmp (url, "/api/messages/keep/", 19) == 0)
  {
      /* POST /api/messages/keep/<id>/<0|1> : tag a message for permanent retention, or remove the tag */
      const char *what = url + 19;
      unsigned long m_id;
      char *end;
      int rc;

      if (0 != strcmp (method, MHD_HTTP_METHOD_POST) || !is_own_request (connection))
        return send_text (connection, MHD_HTTP_FORBIDDEN, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      errno = 0;
      m_id = strtoul (what, &end, 10);
      if (!isdigit ((unsigned char) what[0]) || errno != 0 || m_id == 0 || m_id > 0x7fffffffUL || end[0] != '/' || (end[1] != '0' && end[1] != '1') || end[2] != '\0')
        return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      rc = set_keep ((unsigned int) m_id, end[1] == '1');
      if (rc == -3)
        return send_text (connection, MHD_HTTP_CONFLICT, "{\"error\":\"too many kept messages\"}", "application/json", 0);
      if (rc == -1)
        return send_text (connection, MHD_HTTP_NOT_FOUND, "{\"error\":\"no such message\"}", "application/json", 0);
      if (rc != 0)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 0);
      return send_text (connection, MHD_HTTP_OK, "{}", "application/json", 0);
  }
  if (strcmp (url, "/api/alarm/ack") == 0)
  {
      /* POST: the operator resets the search and rescue alarm */
      char answer[40];
      int n;

      if (0 != strcmp (method, MHD_HTTP_METHOD_POST) || !is_own_request (connection))
        return send_text (connection, MHD_HTTP_FORBIDDEN, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      n = acknowledge_alarms ();
      if (n < 0)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 0);
      snprintf (answer, sizeof (answer), "{\"acknowledged\":%d}", n);
      return send_text (connection, MHD_HTTP_OK, answer, "application/json", 0);
  }
  if (strcmp (url, "/api/status") == 0)
  {
      char *st = get_status_json ();
      if (st == NULL)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 1);
      ret = send_text (connection, MHD_HTTP_OK, st, "application/json", 1);
      free (st);
      return ret;
  }
  if (strcmp (url, "/api/gps") == 0)
  {
      char *g = gps_json ();
      if (g == NULL)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 0);
      /* the position of the station is only for our own page */
      ret = send_text (connection, MHD_HTTP_OK, g, "application/json", 0);
      free (g);
      return ret;
  }
  if (strcmp (url, "/api/settings") == 0)
  {
      char *st = get_settings_json ();
      if (st == NULL)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 1);
      ret = send_text (connection, MHD_HTTP_OK, st, "application/json", 0);
      free (st);
      return ret;
  }
  if (strncmp (url, "/api/settings/", 14) == 0 || strncmp (url, "/saveconfig/", 12) == 0)
  {
      /* the two addresses that change something */
      if (0 != strcmp (method, MHD_HTTP_METHOD_POST) || !is_own_request (connection))
        return send_text (connection, MHD_HTTP_FORBIDDEN, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      return (url[1] == 'a') ? handle_setting (connection, url) : handle_saveconfig (connection, url);
  }
  if (strncmp (url, "/messagetext/", 13) == 0)
  {
      const char *digits = url + 13;
      char *end;
      unsigned long m_id;
      char *mt;

      if (!isdigit ((unsigned char) digits[0]))
        return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      errno = 0;
      m_id = strtoul (digits, &end, 10);
      if (errno != 0 || *end != '\0' || m_id > 0x7fffffffUL)
        return send_text (connection, MHD_HTTP_BAD_REQUEST, BAD_REQUEST_PAGE, "text/html; charset=UTF-8", 0);
      mt = get_message_text ((unsigned int) m_id);
      if (mt == NULL)
        return send_text (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, ERROR_JSON, "application/json", 1);
      ret = send_text (connection, MHD_HTTP_OK, mt, "application/json", 1);
      free (mt);
      return ret;
  }

  /* Anything else is a file of the web GUI */
  path = (strcmp (url, "/") == 0) ? "index.html" : &url[1];
  file = NULL;
  if (safe_static_path (path) && (0 == stat (path, &buf)) && S_ISREG (buf.st_mode))
    file = fopen (path, "rb");
  if (file == NULL)
    return send_text (connection, MHD_HTTP_NOT_FOUND, NOT_FOUND_PAGE, "text/html; charset=UTF-8", 0);

  response = MHD_create_response_from_callback (buf.st_size, 32 * 1024,
                                                &file_reader, file,
                                                &file_free_callback);
  if (response == NULL)
    {
      fclose (file);
      return MHD_NO;
    }
  MHD_add_response_header (response, "Content-Type", content_type_of (path));
  MHD_add_response_header (response, "X-Content-Type-Options", "nosniff");
  /* The GUI only uses its own files: nothing received by radio can load or run anything else */
  MHD_add_response_header (response, "Content-Security-Policy", "default-src 'self'; frame-ancestors 'none'");
  MHD_add_response_header (response, "Cache-Control", "no-cache");
  ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
  MHD_destroy_response (response);
  return ret;
}

int
main (int argc, char *const *argv)
{
  struct MHD_Daemon *http_daemon;
  struct sockaddr_in addr;
  const char *address = NULL;
  int port_nr = 8080;
  unsigned int flags = MHD_USE_THREAD_PER_CONNECTION;

#ifdef MHD_USE_INTERNAL_POLLING_THREAD
  flags |= MHD_USE_INTERNAL_POLLING_THREAD;
#endif

  if (argc > 3 || (argc >= 2 && (strcmp (argv[1], "-h") == 0 || strcmp (argv[1], "--help") == 0)))
  {
    fprintf (stderr, "Usage: %s [port] [listen address]\n"
                     "  port            TCP port (default 8080)\n"
                     "  listen address  IPv4 address to listen on (default: all), e.g. 127.0.0.1\n", argv[0]);
    return 1;
  }
  if (argc >= 2)
  {
    port_nr = atoi (argv[1]);
    if (port_nr < 1 || port_nr > 65535)
    {
      fprintf (stderr, "Error: invalid port '%s'\n", argv[1]);
      return 1;
    }
  }
  if (argc == 3)
    address = argv[2];

  if (chdir (WEB_ROOT) != 0)
  {
    perror ("chdir() to webroot failed");
    return 1;
  }

  memset (&addr, 0, sizeof (addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons ((unsigned short) port_nr);
  addr.sin_addr.s_addr = htonl (INADDR_ANY);
  if (address != NULL && inet_pton (AF_INET, address, &addr.sin_addr) != 1)
  {
    fprintf (stderr, "Error: invalid IPv4 listen address '%s'\n", address);
    return 1;
  }

  http_daemon = MHD_start_daemon (flags, (unsigned short) port_nr, NULL, NULL,
                                  &http_ahc, NULL,
                                  MHD_OPTION_SOCK_ADDR, (struct sockaddr *) &addr,
                                  MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int) 256,
                                  MHD_OPTION_END);
  if (http_daemon == NULL)
    {
      fprintf (stderr, "Error: failed to start the HTTP daemon on port %d\n", port_nr);
      return 1;
    }
  printf ("MHD daemon listening on %s port %d\n", address != NULL ? address : "all addresses", port_nr);
  /* Position from gpsd: NAVTEX_GPSD=host:port, or empty to do without */
  {
    const char *gpsd = getenv ("NAVTEX_GPSD");
    if (gpsd == NULL) gpsd = "127.0.0.1:2947";
    gps_start (gpsd);
    nrx_start ();
    printf (gpsd[0] != '\0' ? "Position from gpsd at %s\n" : "Position from gpsd disabled%s\n", gpsd);
  }
  fflush (stdout);

  for (;;)
    sleep (62);

  MHD_stop_daemon (http_daemon);
  return 0;
}
