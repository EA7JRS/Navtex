/*
 * Navtex · checks of the receiver rules and of the NRX sentences (make check)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Manuel - EA7JRS (fork EA7JRS/Navtex). See COPYING.md.
 *
 * The messages used here are written for the test; they were not received.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>
#include "nrx.h"
#include "../receiver/message_store.h"


static int failed = 0, checks = 0;
#define CHECK(cond, what) do { checks++; if (!(cond)) { failed++; printf ("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

static void
sql (const char *text)
{
  sqlite3 *db;
  char *err = NULL;
  if (sqlite3_open (DATA_FILE, &db) != SQLITE_OK || sqlite3_exec (db, text, NULL, NULL, &err) != SQLITE_OK)
    {
      printf ("sqlite: %s\n", err != NULL ? err : "cannot open");
      exit (2);
    }
  sqlite3_close (db);
}

static int
count_listed (void)
{
  char *ml = get_messages_since (0, 500);
  cJSON *j = cJSON_Parse (ml);
  int n = cJSON_GetArraySize (j);
  cJSON_Delete (j);
  free (ml);
  return n;
}

/* Field of the only (or first) listed message with that identity */
static cJSON *
listed (const char *bbbb, cJSON **keep)
{
  char *ml = get_messages_since (0, 500);
  cJSON *j = cJSON_Parse (ml), *m;
  free (ml);
  *keep = j;
  cJSON_ArrayForEach (m, j)
    if (strcmp (cJSON_GetObjectItem (m, "bbbb")->valuestring, bbbb) == 0) return m;
  return NULL;
}

static unsigned char
checksum_ok (const char *sentence)
{
  unsigned char sum = 0;
  const char *p = sentence + 1;
  unsigned int given = 0;
  for (; *p != '\0' && *p != '*'; p++) sum ^= (unsigned char) *p;
  return *p == '*' && sscanf (p + 1, "%2X", &given) == 1 && given == sum;
}

int
main (void)
{
  cJSON *j, *m;
  char *s, *line, *save;
  char text[2000];
  int i, n;

  unlink (DATA_FILE);
  sql ("create table messages (id integer primary key autoincrement, bbbb text, message text, timestamp text, age text, freq integer);"
       "create table config (id integer primary key, tag text, value text);"
       "insert into config (tag,value) values ('stations518','ABCDEFGHIJKLMNOPQRSTUVWXYZ'),('messages518','ABCDEFL'),"
       "('stations490','ABCDEFGHIJKLMNOPQRSTUVWXYZ'),('messages490','ABCDEFL');");

  /* --- character error rate below 4 % (IMO MSC.148(77), 6.2.3) */
  CHECK (satisfactorily_received (1, 3, 100), "3 % is satisfactory");
  CHECK (!satisfactorily_received (1, 4, 100), "4 % is not");
  CHECK (!satisfactorily_received (0, 0, 100), "a cut message is not");
  CHECK (count_bad_characters ("AB*D**") == 3, "bad characters are the asterisks");
  CHECK (better_copy (1, 0, 100, 0, 0, 300), "complete beats cut");
  CHECK (better_copy (1, 10, 100, 1, 30, 100), "lower error rate wins among complete copies");
  CHECK (!better_copy (0, 0, 50, 0, 0, 80), "among cut copies, the longer one stays");
  CHECK (!better_copy (1, 5, 100, 1, 2, 100), "a satisfactory copy is not replaced by a worse one");

  /* --- time slots (NAVTEX Manual, table 2) */
  CHECK (slot_minute ('A') == 0 && slot_minute ('G') == 60 && slot_minute ('X') == 230 && slot_minute ('Z') == -1, "slot of each letter");
  CHECK (in_own_slot ('G', "2026-11-22 09:08") == 1, "G at 0908 is in its slot (0900)");
  CHECK (in_own_slot ('G', "2026-11-22 13:12") == 1, "a little overrun is still its slot");
  CHECK (in_own_slot ('G', "2026-11-22 09:40") == 0, "G at 0940 is not");
  CHECK (in_own_slot ('X', "2026-11-22 23:55") == 1, "X at 2355");
  CHECK (in_own_slot ('A', "2026-11-22 00:03") == 1 && in_own_slot ('A', "2026-11-22 03:58") == 0, "A around midnight");
  CHECK (in_own_slot ('Z', "2026-11-22 00:03") == -1, "no slot for Z");

  /* --- storing, repeats, better copies */
  CHECK (add_message ("GA12", "ZCZC GA12\nLIGHT UNLIT. TEST MESSAGE, NOT A REAL ONE.\n", 518, 1) == 0, "stored");
  CHECK (count_listed () == 1, "one message");
  m = listed ("GA12", &j);
  i = m != NULL ? cJSON_GetObjectItem (m, "id")->valueint : -1;
  CHECK (m != NULL && cJSON_IsTrue (cJSON_GetObjectItem (m, "satisfactory")) && cJSON_GetObjectItem (m, "errors")->valueint == 0, "satisfactory, no errors");
  cJSON_Delete (j);
  add_message ("GA12", "ZCZC GA12\nLIGHT UNLIT. TEST MESSAGE, NOT A REAL ONE.\n", 518, 1);
  m = listed ("GA12", &j);
  CHECK (count_listed () == 1 && m != NULL && cJSON_GetObjectItem (m, "id")->valueint == i, "a repeat is not stored again");
  cJSON_Delete (j);
  add_message ("GA12", "ZCZC GA12\nLIGHT UNLIT. TEST MESSAGE, NOT A REAL ONE.\n", 490, 1);
  CHECK (count_listed () == 2, "the same identity on another frequency is another message");

  /* a corrupted copy, then a clean one: the clean one replaces it */
  add_message ("GB20", "ZCZC GB20\nGA*E W*RN*NG. T**T ***SAGE.\n", 518, 1);
  m = listed ("GB20", &j);
  CHECK (m != NULL && cJSON_IsFalse (cJSON_GetObjectItem (m, "satisfactory")), "too many errors: not satisfactory");
  i = m != NULL ? cJSON_GetObjectItem (m, "id")->valueint : -1;
  cJSON_Delete (j);
  add_message ("GB20", "ZCZC GB20\nGALE WARNING. TEST MESSAGE.\n", 518, 1);
  m = listed ("GB20", &j);
  CHECK (m != NULL && cJSON_IsTrue (cJSON_GetObjectItem (m, "satisfactory")) && cJSON_GetObjectItem (m, "id")->valueint != i, "the clean copy replaces the corrupted one");
  cJSON_Delete (j);
  add_message ("GB20", "ZCZC GB20\nGALE WARN\n", 518, 0);
  m = listed ("GB20", &j);
  CHECK (m != NULL && cJSON_IsTrue (cJSON_GetObjectItem (m, "complete")), "a cut repeat does not replace it");
  cJSON_Delete (j);

  /* B3B4 = 00 is always shown again */
  add_message ("GD00", "ZCZC GD00\nTEST. FIRST.\n", 518, 1);
  m = listed ("GD00", &j); i = cJSON_GetObjectItem (m, "id")->valueint; cJSON_Delete (j);
  add_message ("GD00", "ZCZC GD00\nTEST. SECOND.\n", 518, 1);
  m = listed ("GD00", &j);
  CHECK (m != NULL && cJSON_GetObjectItem (m, "id")->valueint != i && strstr (cJSON_GetObjectItem (m, "message")->valuestring, "SECOND") != NULL, "00 is stored every time");
  cJSON_Delete (j);

  /* --- after 72 hours the identity is forgotten: a repeat is a new message */
  sql ("update messages set timestamp = strftime('%Y-%m-%d %H:%M', 'now', '-73 hours') where bbbb = 'GA12' and freq = 518");
  m = listed ("GA12", &j); i = cJSON_GetObjectItem (m, "id")->valueint; cJSON_Delete (j);
  add_message ("GA12", "ZCZC GA12\nLIGHT UNLIT. TEST MESSAGE, NOT A REAL ONE.\n", 518, 1);
  m = listed ("GA12", &j);
  CHECK (m != NULL && cJSON_GetObjectItem (m, "id")->valueint != i && cJSON_IsTrue (cJSON_GetObjectItem (m, "unread")), "after 72 h it is new again");
  cJSON_Delete (j);

  /* --- deleting hides the message, and its repeat does not bring it back */
  n = count_listed ();
  m = listed ("GB20", &j); i = cJSON_GetObjectItem (m, "id")->valueint; cJSON_Delete (j);
  CHECK (delete_messages ((unsigned int) i) == 1 && count_listed () == n - 1, "deleted");
  add_message ("GB20", "ZCZC GB20\nGALE WARNING. TEST MESSAGE.\n", 518, 1);
  CHECK (count_listed () == n - 1, "its repeat stays hidden");
  CHECK (delete_messages (999999) == 0, "nothing to delete");

  /* --- retention: heard long ago goes, heard lately stays */
  CHECK (retention_seconds () == 72 * 3600, "72 h by default");
  CHECK (set_setting ("retention_hours", "71") != 0 && set_setting ("retention_hours", "9000") != 0 && set_setting ("retention_hours", "168") == 0, "retention: 72 to 8760");
  CHECK (retention_seconds () == 168 * 3600, "a week");
  sql ("update messages set heard = strftime('%s','now') - 100*3600, timestamp = strftime('%Y-%m-%d %H:%M', 'now', '-100 hours') where bbbb = 'GA12' and freq = 490");
  purge_old_messages ();
  CHECK (listed ("GD00", &j) != NULL, "still there"); cJSON_Delete (j);
  n = count_listed ();
  set_setting ("retention_hours", "72");
  purge_old_messages ();
  CHECK (count_listed () == n - 1, "not heard for 100 h: gone with 72 h of retention");
  sql ("update messages set timestamp = strftime('%Y-%m-%d %H:%M', 'now', '-73 hours') where deleted = 1");
  purge_old_messages ();
  add_message ("GB20", "ZCZC GB20\nGALE WARNING. TEST MESSAGE.\n", 518, 1);
  CHECK (listed ("GB20", &j) != NULL, "a deleted message comes back once its identity is forgotten"); cJSON_Delete (j);

  /* --- other settings */
  CHECK (set_setting ("nrx.udp", "192.168.1.255:10110") == 0 && set_setting ("nrx.udp", "") == 0 && set_setting ("nrx.udp", "plotter.local:2000") == 0, "UDP destination");
  CHECK (set_setting ("nrx.udp", "host") != 0 && set_setting ("nrx.udp", "a:b:1") != 0 && set_setting ("nrx.udp", "h:0") != 0 && set_setting ("nrx.udp", "h o:1") != 0, "bad UDP destinations");
  CHECK (set_setting ("nrx.tcp", "10110") == 0 && set_setting ("nrx.tcp", "0") == 0 && set_setting ("nrx.tcp", "80") != 0 && set_setting ("nrx.tcp", "70000") != 0, "TCP port");

  /* --- export */
  s = export_messages (1, 0);
  CHECK (s != NULL && strncmp (s, "id,bbbb,khz,", 12) == 0 && strstr (s, ",GD00,518,") != NULL && strstr (s, "\"ZCZC GD00\nTEST. SECOND.\n\"") != NULL, "CSV");
  free (s);
  add_message ("GA33", "ZCZC GA33\n=1+1 \"QUOTED\" TEST\n", 490, 1);
  s = export_messages (1, 490);
  CHECK (s != NULL && strstr (s, "\"ZCZC GA33\n=1+1 \"\"QUOTED\"\" TEST\n\"") != NULL && strstr (s, "GD00") == NULL, "CSV: quotes doubled, one frequency");
  free (s);
  s = export_messages (0, 0);
  CHECK (s != NULL && strstr (s, "==== GD00  518 kHz  received ") != NULL && strstr (s, "TEST. SECOND.\r\n") != NULL, "text");
  free (s);

  /* --- NRX: the form of the Furuno manual: $NXNRX,TA21,1,4,518,10,01,2005,102000,2.2,291600 UTC JAN = */
  s = nrx_sentences ("TA21", 518, "2005-01-10 10:20", "ZCZC TA21\n291600 UTC JAN =\nOOSTENDERADIO - INFO 37/04 =\nWESTHINDER ANCHORAGE, ANCHOR LOST IN\nPOSITION 51-24.94N 002-40.02E.+\n");
  CHECK (s != NULL, "NRX made");
  if (s != NULL)
    {
      CHECK (strncmp (s, "$CRNRX,TA21,1,5,518,10,01,2005,102000,0.0,291600 UTC JAN =*", 59) == 0, "first sentence");
      CHECK (strstr (s, "$CRNRX,TA21,3,5,518,10,01,2005,102000,0.0,WESTHINDER ANCHORAGE^2C ANCHOR*") != NULL, "the comma goes as ^2C, split at a space");
      for (n = 0, line = strtok_r (s, "\r\n", &save); line != NULL; line = strtok_r (NULL, "\r\n", &save), n++)
        CHECK (checksum_ok (line) && strlen (line) <= 82, "checksum and length");
      CHECK (n == 5, "five sentences: every one carries all the fields, as the app sends them");
      free (s);
    }
  /* a long line is split at a space; bad characters count in the error rate; nothing passes 82 */
  memset (text, 0, sizeof (text));
  strcpy (text, "ZCZC GA05\n");
  for (i = 0; i < 30; i++) strcat (text, i == 7 ? "W*RD " : "WORD ");
  strcat (text, "\n\nLAST,LINE$\n");
  s = nrx_sentences ("GA05", 4209, "2026-11-22 09:07", text);
  CHECK (s != NULL && strstr (s, ",4209,22,11,2026,090700,0.6,") != NULL, "4209, date, error rate");
  if (s != NULL)
    {
      char joined[2000] = "";
      CHECK (strstr (s, "W^2ARD") != NULL && strstr (s, "LAST^2CLINE^24*") != NULL, "escapes");
      for (n = 0, line = strtok_r (s, "\r\n", &save); line != NULL; line = strtok_r (NULL, "\r\n", &save), n++)
        {
          const char *t = line;
          int commas = 0;
          CHECK (checksum_ok (line) && strlen (line) <= 82, "checksum and length (long line)");
          while (commas < 10 && *t != '\0') if (*t++ == ',') commas++;
          strncat (joined, t, strcspn (t, "*"));
          strcat (joined, "|");
        }
      CHECK (n == 7 && strstr (joined, "WORD|WORD") != NULL && strstr (joined, "||LAST") != NULL, "split at spaces, empty line kept");
      CHECK (strstr (joined, "WO|RD") == NULL && strstr (joined, "^|") == NULL && strstr (joined, "^2|") == NULL, "never inside a word or an escape");
      free (s);
    }
  CHECK (nrx_sentences ("GA5", 518, "2026-11-22 09:07", "x") == NULL && nrx_sentences ("GA05", 518, "ayer", "x") == NULL, "bad input gives nothing");

  unlink (DATA_FILE);
  printf ("%d checks, %d failed\n", checks, failed);
  return failed != 0;
}
