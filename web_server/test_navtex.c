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

  /* --- character error rate below 4 % (IMO MSC.508(105), 6.2.3) */
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

  /* --- the user cannot delete messages (IMO MSC.508(105), 6.1.1); what an earlier version hid is shown again */
  n = count_listed ();
  sql ("update messages set deleted = 1 where bbbb = 'GB20'");
  CHECK (count_listed () == n - 1, "a row hidden by an earlier version");
  sql ("update messages set deleted = 0 where deleted = 1");      /* what open_db() does once per process */
  CHECK (count_listed () == n, "is shown again");

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
  s = nrx_sentences ("TA21", 518, "2005-01-10 10:20", "ZCZC TA21\n291600 UTC JAN =\nOOSTENDERADIO - INFO 37/04 =\nWESTHINDER ANCHORAGE, ANCHOR LOST IN\nPOSITION 51-24.94N 002-40.02E.+\n", 0, 0, 1);
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
  s = nrx_sentences ("GA05", 4209, "2026-11-22 09:07", text, 0, 0, 1);
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
  CHECK (nrx_sentences ("GA5", 518, "2026-11-22 09:07", "x", 0, 0, 1) == NULL && nrx_sentences ("GA05", 518, "ayer", "x", 1, 0, 1) == NULL, "bad input gives nothing");

  /* --- NRX in the form of IEC 61097-6, annex C. Its own example has this checksum: */
  CHECK (checksum_ok ("$CRNRX,007,001,00,IE69,1,135600,27,06,2001,241,3,A,==========================*09"), "the example of the standard");
  s = nrx_sentences ("IE69", 490, "2001-06-27 13:56", "ZCZC IE69\n==========================\nISSUED ON SATURDAY 06 JANUARY 2001.\nINSHORE WATERS FORECAST TO 12 MILES\nOFF SHORE FROM 1700 UTC TO 0500 UTC.\n\nNORTH FORELAND TO SELSEY BILL.\n12 HOURS FORECAST:\nSHOWERY WINDS, STRONGEST IN NORTH.\n\n", 1, 0, 1);
  CHECK (s != NULL && strncmp (s, "$CRNRX,006,001,00,IE69,1,135600,27,06,2001,232,0,A,==========================*", 78) == 0, "IEC: first sentence, with every field; a line end counts as two characters");
  if (s != NULL)
    {
      char joined[1200] = "";
      for (n = 0, line = strtok_r (s, "\r\n", &save); line != NULL; line = strtok_r (NULL, "\r\n", &save), n++)
        {
          const char *t = line;
          int commas = 0;
          CHECK (checksum_ok (line) && strlen (line) <= 82, "IEC: checksum and length");
          if (n > 0) CHECK (strncmp (line + 14, ",00,,,,,,,,,,", 13) == 0, "IEC: later sentences leave the other fields empty");
          while (commas < 13 && *t != '\0') if (*t++ == ',') commas++;
          strncat (joined, t, strcspn (t, "*"));
        }
      CHECK (n == 6, "IEC: six sentences");
      CHECK (strcmp (joined, "==========================^0D^0AISSUED ON SATURDAY 06 JANUARY 2001.^0D^0AINSHORE WATERS FORECAST TO 12 MILES^0D^0AOFF SHORE FROM 1700 UTC TO 0500 UTC.^0D^0A^0D^0ANORTH FORELAND TO SELSEY BILL.^0D^0A12 HOURS FORECAST:^0D^0ASHOWERY WINDS^2C STRONGEST IN NORTH.^0D^0A^0D^0A") == 0, "IEC: the whole text, line ends as ^0D^0A");
      free (s);
    }
  s = nrx_sentences ("GA05", 4209, "2026-11-22 09:07", "ZCZC GA05\nCUT SH*RT", 1, 123, 0);
  CHECK (s != NULL && strncmp (s, "$CRNRX,001,001,23,GA05,3,090700,22,11,2026,9,1,V,CUT SH^2ART*", 60) == 0, "IEC: index 3, status V, bad characters, id 00-99");
  free (s);

  /* --- search and rescue alarm (B2 = D): until it is acknowledged by hand */
  n = pending_alarms ();
  add_message ("GD77", "ZCZC GD77\nTEST. MAYDAY RELAY. NOT A REAL ONE.\n", 518, 1);
  CHECK (pending_alarms () == n + 1, "a D message raises the alarm");
  m = listed ("GD77", &j);
  CHECK (m != NULL && cJSON_IsTrue (cJSON_GetObjectItem (m, "alarm")), "and says so");
  cJSON_Delete (j);
  add_message ("GA78", "ZCZC GA78\nTEST. NOT A REAL ONE.\n", 518, 1);
  CHECK (pending_alarms () == n + 1, "other subjects do not");
  CHECK (acknowledge_alarms () == n + 1 && pending_alarms () == 0, "acknowledged");
  m = listed ("GD77", &j);
  CHECK (m != NULL && cJSON_GetObjectItem (m, "alarm") == NULL, "the message stays, without alarm");
  cJSON_Delete (j);
  add_message ("GD77", "ZCZC GD77\nTEST. MAYDAY RELAY. NOT A REAL ONE.\n", 518, 1);
  CHECK (pending_alarms () == 0, "its repeat does not raise it again");
  {
    char alr[96];
    alr_sentence (alr, sizeof (alr), 90705, 1, 0);
    CHECK (strncmp (alr, "$CRALR,090705.00,003,A,V,NAVTEX: Search and rescue information*", 63) == 0 && strlen (alr) <= 84, "ALR sentence");
    alr[strlen (alr) - 2] = '\0';
    CHECK (checksum_ok (alr), "ALR checksum");
    CHECK (is_alarm_ack ("$IIACK,003*56\r\n") && !is_alarm_ack ("$IIACK,003*00\r\n") && !is_alarm_ack ("$IIACK,004*51\r\n") && !is_alarm_ack ("ACK,003") && !is_alarm_ack (""), "ACK sentence");
  }
  /* --- NRM, with the two examples of IEC 61097-6, annex C.2 */
  {
    int function = -1, freq = 0;
    unsigned long areas = 0, types = 0;
    char letters[27], out[64];
    CHECK (nrm_parse ("$INNRM,2,1,00001E1F,00000023*57", &function, &freq, &areas, &types) && function == 2 && freq == 490, "NRM: the first example of the standard");
    mask_to_letters (types, letters);
    CHECK (strcmp (letters, "ABF") == 0, "its message types are A, B and F, as the standard says");
    mask_to_letters (areas, letters);
    CHECK (strcmp (letters, "ABCDEJKLM") == 0 && letters_to_mask (letters) == 0x1E1FUL, "areas, letters and back");
    CHECK (nrm_parse ("$INNRM,0,2,00001E1F,0FFFFFFF*21", &function, &freq, &areas, &types) && function == 0 && freq == 518 && types == 0x3FFFFFFUL, "NRM: the second example, a request for stored messages; its reserved bits are dropped");
    CHECK (!nrm_parse ("$INNRM,2,1,00001E1F,00000023*58", &function, &freq, &areas, &types) && !nrm_parse ("$INNRM,1,4,00000001,00000001*53", &function, &freq, &areas, &types)
           && !nrm_parse ("$INNRM,1,2,1E1F,23*00", &function, &freq, &areas, &types), "bad checksum, frequency or width: refused");
    nrm_sentence (out, sizeof (out), 1, 4209, letters_to_mask ("GX"), letters_to_mask ("ABDL"));
    CHECK (strncmp (out, "$CRNRM,1,3,00800040,0000080B*", 29) == 0, "NRM written");
    out[strlen (out) - 2] = '\0';
    CHECK (checksum_ok (out) && nrm_parse (out, &function, &freq, &areas, &types) && freq == 4209 && areas == letters_to_mask ("GX"), "and read back");
    CHECK (is_nrm_query ("$INCRQ,NRM*3A\r\n") && !is_nrm_query ("$INCRQ,NRX*2F\r\n") && !is_nrm_query ("$INCRQ,NRM*00"), "CRQ query");
    CHECK (update_config ("stations518", "GX") == 0 && get_config_value ("stations518", letters, sizeof (letters)) && strcmp (letters, "GX") == 0, "followed stations read back");
  }

  /* --- tagged for permanent retention (IMO MSC.508(105), 6.1.2) */
  add_message ("GA40", "ZCZC GA40\nTEST. KEEP THIS ONE.\n", 518, 1);
  m = listed ("GA40", &j); i = cJSON_GetObjectItem (m, "id")->valueint; cJSON_Delete (j);
  CHECK (set_keep ((unsigned int) i, 1) == 0 && set_keep (999999, 1) == -1, "tagged");
  m = listed ("GA40", &j);
  CHECK (m != NULL && cJSON_IsTrue (cJSON_GetObjectItem (m, "keep")), "and says so");
  cJSON_Delete (j);
  sql ("update messages set heard = strftime('%s','now') - 400*3600, timestamp = strftime('%Y-%m-%d %H:%M', 'now', '-400 hours') where bbbb = 'GA40'");
  purge_old_messages ();
  CHECK (listed ("GA40", &j) != NULL, "a tagged message is not purged"); cJSON_Delete (j);
  add_message ("GA40", "ZCZC GA40\nTEST. ANOTHER MESSAGE WITH THE SAME NUMBER, WEEKS LATER.\n", 518, 1);
  s = export_messages (0, 518);
  CHECK (s != NULL && strstr (s, "KEEP THIS ONE") != NULL && strstr (s, "WEEKS LATER") != NULL, "a later message with the same number does not replace it");
  free (s);
  n = count_listed ();
  sql ("delete from messages where coalesce(keep,0) = 0");      /* the test starts over; the program has no such thing */
  CHECK (count_listed () == 1, "only the tagged one is left");
  sql ("delete from messages");
  for (n = 0; n < 51; n++)
    {
      char id[8], body[40];
      snprintf (id, sizeof (id), "H%c%02d", 'A' + n / 26, n);
      snprintf (body, sizeof (body), "ZCZC %s\nTEST %d.\n", id, n);
      add_message (id, body, 490, 1);
    }
  {
    char *ml = get_messages_since (0, 500);
    cJSON *all = cJSON_Parse (ml), *e;
    int ok = 0, refused = 0;
    cJSON_ArrayForEach (e, all)
      {
        int rc = set_keep ((unsigned int) cJSON_GetObjectItem (e, "id")->valueint, 1);
        if (rc == 0) ok++; else if (rc == -3) refused++;
      }
    CHECK (ok == 50 && refused == 1, "at most 50 tagged: a quarter of the 200 of the standard");
    cJSON_Delete (all);
    free (ml);
  }

  /* --- bridge alert management: ALF, ALC, HBT, ARC out; ACN in */
  {
    char b[96];
    long id = -1, inst = -1;
    char cmd = 0;
    alf_sentence (b, sizeof (b), 140200, 'V', 1);
    CHECK (strncmp (b, "$CRALF,1,1,0,140200.00,B,W,V,,3,1,1,0,NAVTEX SAR*", 49) == 0 && checksum_ok (b) && strlen (b) <= 82, "ALF: active, not acknowledged");
    alf_sentence (b, sizeof (b), 140300, 'N', 2);
    CHECK (strncmp (b, "$CRALF,1,1,0,140300.00,B,W,N,,3,1,2,0,NAVTEX SAR*", 49) == 0 && checksum_ok (b), "ALF: normal again, revision 2");
    alc_sentence (b, sizeof (b), 1, 2);
    CHECK (strncmp (b, "$CRALC,01,01,00,1,,3,1,2*", 25) == 0 && checksum_ok (b), "ALC: one alert in the list");
    alc_sentence (b, sizeof (b), 0, 2);
    CHECK (strncmp (b, "$CRALC,01,01,00,0*", 18) == 0 && checksum_ok (b), "ALC: empty list");
    hbt_sentence (b, sizeof (b), 30, 13);
    CHECK (strncmp (b, "$CRHBT,30,A,3*", 14) == 0 && checksum_ok (b), "HBT");
    arc_sentence (b, sizeof (b), 140400, 3, 1, 'S');
    CHECK (strncmp (b, "$CRARC,140400.00,,3,1,S*", 24) == 0 && checksum_ok (b), "ARC");
    CHECK (acn_parse ("$IIACN,140500.00,,3,1,A,C*62\r\n", &id, &inst, &cmd) == 1 && id == 3 && inst == 1 && cmd == 'A', "ACN: acknowledge");
    CHECK (acn_parse ("noise $IIACN,,,0,,Q,C*6E\r\n", &id, &inst, &cmd) == 1 && id == 0 && inst == 0 && cmd == 'Q', "ACN: repeat all, empty time and instance");
    CHECK (acn_parse ("$IIACN,140500.00,,3,1,A,C*00", &id, &inst, &cmd) == 0, "ACN: wrong checksum");
    CHECK (acn_parse ("$IIACN,140500.00,FEC,3,1,A,C*22", &id, &inst, &cmd) == 0, "ACN: another manufacturer's alert");
    CHECK (acn_parse ("$IIACN,140500.00,,3,1,X,C*7B", &id, &inst, &cmd) == 0 && acn_parse ("$IIACK,003*56", &id, &inst, &cmd) == 0, "ACN: unknown command, another sentence");
    CHECK (set_setting ("nrx.bam", "on") == 0 && set_setting ("nrx.bam", "off") == 0 && set_setting ("nrx.bam", "yes") != 0, "bridge alert setting");
  }
  CHECK (set_setting ("nrx.format", "iec") == 0 && set_setting ("nrx.format", "furuno") == 0 && set_setting ("nrx.format", "x") != 0, "NRX form");

  unlink (DATA_FILE);
  printf ("%d checks, %d failed\n", checks, failed);
  return failed != 0;
}
