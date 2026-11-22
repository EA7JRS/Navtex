#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <string.h>
#include <cjson/cJSON.h>
#include <sqlite3.h>
#include <time.h>
#include <stdlib.h>
#include "message_store.h"

extern char* strdup(const char*);

#define TS_SIZE 20

int navtex_verbose = 1;
int navtex_hf = 0;
static int schema_checked = 0;

#define MAX_KEPT_MESSAGES 50				// 25 % of the 200 messages of the standard (IMO MSC.508(105), 6.1.2)
#define IDENTITY_MEMORY_AGE (60*60*72)			// a message identity is remembered 72 hours (IMO MSC.508(105), 6.2.2: 60 to 72)
#define MESSAGE_WEAHTER_FCST_AGE_LIMIT 60*60*28  	//28 hours in seconds mark weather fcsts as old when reaching this age



// Timestamps are stored in UTC. timegm() converts without looking at the
// local time zone (the previous mktime()-timezone was one hour off during DST).
time_t my_timegm(struct tm *tm) {
    return timegm(tm);
} 

void get_current_timestamp(char *cts)
{
   struct tm timeinfo;
   time_t timestamp;

   timestamp = time(NULL);
   gmtime_r(&timestamp, &timeinfo); 
   strftime(cts, TS_SIZE, "%Y-%m-%d %H:%M", &timeinfo);
}


// Function to calculate the difference in seconds between timestamp and current time
long calculate_time_difference(char *timestamp) {
    struct tm tm;
    time_t given_time, current_time;

    // Parse the input timestamp string into struct tm
    memset(&tm, 0, sizeof(struct tm)); // Initialize the structure to zero
    strptime(timestamp, "%Y-%m-%d %H:%M", &tm);

    // Convert struct tm to time_t (seconds since epoch)
    given_time = my_timegm(&tm);

    // Get the current time in UTC
    current_time = time(NULL);

    // Calculate and return the difference in seconds
    return difftime(current_time, given_time);
}


// The receiver and the web server share the database: wait instead of failing
// when the other process holds the lock. *db is NULL when it cannot be opened.
void open_db(sqlite3 **db) {
        if (sqlite3_open(DATA_FILE, db) != SQLITE_OK) {
                fprintf(stderr, "Cannot open %s: %s\n", DATA_FILE, *db ? sqlite3_errmsg(*db) : "out of memory");
                sqlite3_close(*db);
                *db = NULL;
                return;
        }
        sqlite3_busy_timeout(*db, 5000);
        // Once per process: bring a database of an earlier version up to date.
        // The "alter" fails harmlessly when the column is already there.
        if (!schema_checked) {
                sqlite3_exec(*db, "alter table messages add column complete integer default 1", NULL, NULL, NULL);
                // errors: characters received corrupted ('*'); deleted: hidden by the user, its identity is
                // still remembered; heard: last time the message was received (unix time, UTC)
                sqlite3_exec(*db, "alter table messages add column errors integer default 0", NULL, NULL, NULL);
                sqlite3_exec(*db, "alter table messages add column deleted integer default 0", NULL, NULL, NULL);
                // Since 1.7.0 the user cannot delete messages (IMO MSC.508(105), 6.1.1): what an earlier
                // version hid comes back, and leaves as any other message, when its time is up.
                sqlite3_exec(*db, "update messages set deleted = 0 where deleted = 1", NULL, NULL, NULL);
                sqlite3_exec(*db, "alter table messages add column heard integer", NULL, NULL, NULL);
                // alarm: 1 while a search and rescue message (B2 = D) waits to be acknowledged by hand
                sqlite3_exec(*db, "alter table messages add column alarm integer default 0", NULL, NULL, NULL);
                // keep: 1 when the user tagged the message for permanent retention (IMO MSC.508(105), 6.1.2)
                sqlite3_exec(*db, "alter table messages add column keep integer default 0", NULL, NULL, NULL);
                sqlite3_exec(*db, "create table if not exists status (tag text primary key, value text)", NULL, NULL, NULL);
                sqlite3_exec(*db, "create table if not exists settings (tag text primary key, value text)", NULL, NULL, NULL);
                // Receiver 1 keeps doing what a fresh install did; receiver 2 waits to be switched on
                sqlite3_exec(*db, "insert or ignore into settings (tag,value) values"
                                  " ('rx1.enabled','1'),('rx1.band','mf'),('rx1.port','A'),('rx1.bias','0'),"
                                  " ('rx2.enabled','0'),('rx2.band','mf'),('rx2.port','A'),('rx2.bias','0'),('rx2.source','rtlsdr')", NULL, NULL, NULL);
                // settings for the HF channel (4209.5 kHz), for databases older than it
                sqlite3_exec(*db, "insert into config (tag,value) select 'stations4209','ABCDEFGHIJKLMNOPQRSTUVWXYZ' where not exists (select 1 from config where tag='stations4209')", NULL, NULL, NULL);
                sqlite3_exec(*db, "insert into config (tag,value) select 'messages4209','ABCDEFL' where not exists (select 1 from config where tag='messages4209')", NULL, NULL, NULL);
                schema_checked = 1;
        }
}

// Only these settings exist, and their values are lists of NAVTEX B1/B2 letters.
static int valid_config(const char *tag, const char *value) {
        static const char *tags[] = { "stations518", "messages518", "stations490", "messages490", "stations4209", "messages4209" };
        size_t i, n;
        int known = 0;

        if (tag == NULL || value == NULL) return 0;
        for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++)
                if (strcmp(tag, tags[i]) == 0) known = 1;
        if (!known) return 0;
        n = strlen(value);
        if (n > 26) return 0;
        for (i = 0; i < n; i++)
                if (value[i] < 'A' || value[i] > 'Z') return 0;
        return 1;
}


// ---------------------------------------------------------------------------
// What a NAVTEX receiver does with a message it has already received
// (IMO MSC.508(105), 6.2; NAVTEX Manual, 6.2 and 7.5):
//  - a message is "satisfactorily received" when its character error rate is
//    below 4 %; only then is its identity B1B2B3B4 remembered;
//  - an identity is remembered for 60 to 72 hours: a repeat of the broadcast
//    within that time is not shown again; after that time it is a new message;
//  - a message with B3B4 = 00 is always shown.
// Here, on top of that: a better copy replaces a worse one (a cut or corrupted
// reception), and a message the user deleted stays hidden while its identity
// is remembered, so the next broadcast does not bring it back.

// Characters the decoder could not receive are stored as '*'
int count_bad_characters(const char *message) {
        int n = 0;
        if (message == NULL) return 0;
        for (; *message; message++) if (*message == '*') n++;
        return n;
}

// 1 when the character error rate is below 4 % and the message reached its NNNN
int satisfactorily_received(int complete, int errors, int length) {
        return complete && length > 0 && errors * 100 < 4 * length;
}

// 2 satisfactory, 1 complete with too many errors, 0 cut short
static int quality(int complete, int errors, int length) {
        return satisfactorily_received(complete, errors, length) ? 2 : complete ? 1 : 0;
}

// 1 when the new copy is worth more than the stored one
int better_copy(int new_complete, int new_errors, int new_len, int old_complete, int old_errors, int old_len) {
        int qn = quality(new_complete, new_errors, new_len), qo = quality(old_complete, old_errors, old_len);
        if (qn != qo) return qn > qo;
        if (qn == 0) return new_len - new_errors > old_len - old_errors;      // cut short: the one with more good text
        return (long) new_errors * old_len < (long) old_errors * new_len;     // complete: the lower error rate
}

// Each B1 letter owns ten minutes every four hours: A at 0000, B at 0010 ... X at 0350
// (NAVTEX Manual, 5.2.5 and table 2). Minute of that slot within the four hours, or -1.
int slot_minute(char b1) {
        return (b1 >= 'A' && b1 <= 'X') ? (b1 - 'A') * 10 : -1;
}

// 1 when a message stored at that UTC time ("YYYY-MM-DD HH:MM") arrived within the slot of its
// station: the message is stored when it ends, up to ten minutes after the slot starts, and
// a broadcast may overrun a little. 0 when it did not; -1 when it cannot be told.
int in_own_slot(char b1, const char *timestamp) {
        int h, m, slot = slot_minute(b1), offset;
        if (slot < 0 || timestamp == NULL || strlen(timestamp) != 16 || sscanf(timestamp + 11, "%2d:%2d", &h, &m) != 2) return -1;
        offset = ((h * 60 + m) % 240 - slot + 240) % 240;
        return offset <= 13;
}

int add_message(char *bbbb, char *message, int freq, int complete) {
        char timestamp_str[TS_SIZE];
        sqlite3 *db;
        sqlite3_stmt *stmt;
        int rc;
        int keep_existing = 0;
        int carry_keep = 0;      // the stored copy was tagged for retention and this one replaces it
        int errors, length;
        const char *name = freq == 4209 ? "4209.5" : freq == 518 ? "518" : "490";

        if (bbbb == NULL || message == NULL || strlen(bbbb) != 4 || message[0] == '\0') return -1;
        errors = count_bad_characters(message);
        length = (int) strlen(message);

        get_current_timestamp(timestamp_str);

	open_db(&db);
        if (db == NULL)
        {
                printf("Failed to open DB\n");
                return -1;
        }

        // What is already stored for this message identity on this frequency?
        if (sqlite3_prepare_v2(db, "select complete, length(message), timestamp, coalesce(errors,0), id, coalesce(keep,0) from messages where bbbb = ?1 and freq = ?2 order by id desc limit 1", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, bbbb, -1, SQLITE_STATIC);
                sqlite3_bind_int(stmt, 2, freq);
                if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 2) != NULL) {
                        int old_complete = sqlite3_column_type(stmt, 0) == SQLITE_NULL ? 1 : sqlite3_column_int(stmt, 0);
                        int old_len = sqlite3_column_int(stmt, 1);
                        int old_errors = sqlite3_column_int(stmt, 3);
                        int old_id = sqlite3_column_int(stmt, 4);
                        long age = calculate_time_difference((char *) sqlite3_column_text(stmt, 2));
                        int always = bbbb[2] == '0' && bbbb[3] == '0';       // B3B4 = 00: always shown

                        // Within the time its identity is remembered it is the same message: a better copy
                        // inherits the tag. Later, the number has been used again for another message.
                        if (age < IDENTITY_MEMORY_AGE) carry_keep = sqlite3_column_int(stmt, 5);
                        if (age < IDENTITY_MEMORY_AGE && !always) {
                                if (satisfactorily_received(old_complete, old_errors, old_len)) keep_existing = 1;          // a repeat of the broadcast
                                else if (!better_copy(complete, errors, length, old_complete, old_errors, old_len)) keep_existing = 1;
                        }
                        if (keep_existing) {
                                // it is still being broadcast: remember when it was last heard
                                sqlite3_stmt *up;
                                if (sqlite3_prepare_v2(db, "update messages set heard = strftime('%s','now') where id = ?1", -1, &up, NULL) == SQLITE_OK) {
                                        sqlite3_bind_int(up, 1, old_id);
                                        sqlite3_step(up);
                                        sqlite3_finalize(up);
                                }
                        }
                }
                sqlite3_finalize(stmt);
        }
        if (keep_existing) {
                sqlite3_close(db);
                printf("%s kHz: %s received again (%s, %d characters, %d bad), keeping the stored copy\n", name, bbbb, complete ? "complete" : "incomplete", length, errors);
                return 0;
        }

	// What the user tagged for retention is never replaced by a later message with the same number
	sqlite3_prepare_v2(db, "delete from messages where bbbb = ?1 and freq = ?2 and (coalesce(keep,0) = 0 or ?3 = 1)", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, bbbb, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, freq);
        sqlite3_bind_int(stmt, 3, carry_keep ? 1 : 0);
	sqlite3_step(stmt); 
        sqlite3_finalize(stmt);

	sqlite3_prepare_v2(db, "insert into messages (bbbb,message,timestamp,age,freq,complete,errors,deleted,heard,alarm,keep) values ( ?1 , ?2 , ?3 ,'NEW',?4,?5,?6,0,strftime('%s','now'),?7,?8)", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, bbbb,          -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, message,       -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, timestamp_str, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, freq);
        sqlite3_bind_int(stmt, 5, complete ? 1 : 0);
        sqlite3_bind_int(stmt, 6, errors);
        // Search and rescue information gives an alarm that can only be reset by hand
        // (IMO MSC.508(105), 7; IEC 61097-6, 4.3.9)
        sqlite3_bind_int(stmt, 7, bbbb[1] == 'D' ? 1 : 0);
        sqlite3_bind_int(stmt, 8, carry_keep ? 1 : 0);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
	sqlite3_close(db);
        if (rc != SQLITE_DONE) {
                printf("%s kHz: %s could not be stored\n", name, bbbb);
                return -2;
        }
        printf("%s kHz: %s stored, %s, %d characters, %d bad (%.1f %%)%s\n", name, bbbb, complete ? "complete" : "incomplete", length, errors, 100.0 * errors / length,
               satisfactorily_received(complete, errors, length) ? "" : ", not satisfactorily received");
        return 0;
}



char *get_message_list() {
// create a cJSON object
cJSON *json = cJSON_CreateArray();

	long td;
        sqlite3 *db;
        sqlite3_stmt *stmt;


        purge_old_messages();
        open_db(&db);
        if (db == NULL)
        {
                printf("Failed to open DB\n");
                return ((char *) 0);
        }

        //sqlite3_prepare_v2(db, "select id,bbbb,timestamp,age,freq from messages order by timestamp desc", -1, &stmt, NULL);
        sqlite3_prepare_v2(db, "select messages.id as id,bbbb,timestamp,age,freq,complete from messages, config AS CO1, config AS CO2 where coalesce(deleted,0)=0 and ((freq=518 and CO1.tag='stations518' and instr(CO1.value,substr(bbbb,1,1))>0 and CO2.tag='messages518' and instr(CO2.value,substr(bbbb,2,1)) > 0) OR  (freq=490 and CO1.tag='stations490' and instr(CO1.value,substr(bbbb,1,1))>0 and CO2.tag='messages490' and instr(CO2.value,substr(bbbb,2,1)) > 0)) order by timestamp desc", -1, &stmt, NULL);



        while (sqlite3_step(stmt) == SQLITE_ROW) {
                int i;
                int num_cols = sqlite3_column_count(stmt);

		cJSON *element = cJSON_CreateObject();
                for (i = 0; i < num_cols; i++)
                {
                        if(strcmp(sqlite3_column_name(stmt,i),"timestamp") == 0)
                        {
				//printf("time diff: %ld\n",calculate_time_difference((char*)sqlite3_column_text(stmt, i) ) ) ;
			}
                        switch (sqlite3_column_type(stmt, i))
                        {
                        case (SQLITE3_TEXT):
				if(strcmp(sqlite3_column_name(stmt,i),"age")==0) {
        				td = calculate_time_difference((char*)sqlite3_column_text(stmt, 2) );
					if (td > MESSAGE_WEAHTER_FCST_AGE_LIMIT && sqlite3_column_text(stmt, 1)[1] == 'E' ) {
						cJSON_AddStringToObject(element, sqlite3_column_name(stmt,i), "old" );
					}
					else { 
						cJSON_AddStringToObject(element, sqlite3_column_name(stmt,i), (const char *) sqlite3_column_text(stmt, i));
					}
				}
				else {
					cJSON_AddStringToObject(element, sqlite3_column_name(stmt,i), (const char *) sqlite3_column_text(stmt, i));
				}
				if(strcmp(sqlite3_column_name(stmt,i),"bbbb")==0) {
					switch( sqlite3_column_text(stmt, i)[1]) {
				  	case 'A': 	
						cJSON_AddStringToObject(element, "type","NAV Warning" );
						break;	
				  	case 'B': 	
						cJSON_AddStringToObject(element, "type","MET Warning" );
						break;	
				  	case 'C': 	
						cJSON_AddStringToObject(element, "type","ICE report" );
						break;	
				  	case 'D': 	
						cJSON_AddStringToObject(element, "type","Search and Rescue" );
						break;	
				  	case 'E': 	
						cJSON_AddStringToObject(element, "type","MET Forecast" );
						break;	
				  	case 'F': 	
						cJSON_AddStringToObject(element, "type","Pilot message" );
						break;	
				  	case 'G': 	
						cJSON_AddStringToObject(element, "type","AIS message" );
						break;	
				  	case 'H': 	
						cJSON_AddStringToObject(element, "type","Loran C message" );
						break;	
				  	case 'J': 	
						cJSON_AddStringToObject(element, "type","SatNav message" );
						break;	
				  	case 'L': 	
						cJSON_AddStringToObject(element, "type","NAV Warning" );
						break;	
				  	case 'V': 	
						cJSON_AddStringToObject(element, "type","Ntc to Fisherman" );
						break;	
					default:
						cJSON_AddStringToObject(element, "type","Unknown" );
						break;	

					}
				}

                                //printf("sqlite_CT: %s, ", sqlite3_column_text(stmt, i));
                                break;
                        case (SQLITE_INTEGER):
 				cJSON_AddNumberToObject(element, sqlite3_column_name(stmt,i), sqlite3_column_int(stmt, i));
                 //               printf("%d, ", sqlite3_column_int(stmt, i));
                                break;
                        case (SQLITE_FLOAT):
                                break;
                        default:
                                break;
                        }
                }
		cJSON_AddItemToArray(json,element);
        }

        sqlite3_finalize(stmt);

sqlite3_close(db);


char *json_str = cJSON_Print(json);
char *result = json_str ? strdup(json_str) : NULL;
cJSON_free(json_str);
cJSON_Delete(json);
return result;
}




// Hours a message is kept after it was last heard: the "retention_hours" setting,
// never less than the 72 hours its identity has to be remembered.
long retention_seconds(void) {
        char v[16];
        long h = 72;
        if (get_setting("retention_hours", v, sizeof(v)) && v[0] != '\0') h = atol(v);
        if (h < 72) h = 72;
        if (h > 8760) h = 8760;
        return h * 3600;
}

// A message goes when it has not been heard for the retention time. One the user
// deleted goes as soon as its identity no longer has to be remembered.
void purge_old_messages() {
        sqlite3 *db;
        sqlite3_stmt *stmt;
        long keep = retention_seconds();

	open_db(&db);
        if (db == NULL)
        {
                printf("Failed to open DB\n");
		return;
        }
        if (sqlite3_prepare_v2(db, "delete from messages where"
                                   " ((strftime('%s','now') - coalesce(heard, strftime('%s', timestamp))) > ?1 and coalesce(keep,0) = 0)"
                                   " or (coalesce(deleted,0) = 1 and (strftime('%s','now') - strftime('%s', timestamp)) > ?2)", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, (sqlite3_int64) keep);
                sqlite3_bind_int64(stmt, 2, (sqlite3_int64) IDENTITY_MEMORY_AGE);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
        }
	sqlite3_close(db);
}

// Tags a message for permanent retention, or removes the tag (IMO MSC.508(105), 6.1.2: "the user
// should be able to tag individual messages for permanent retention", up to 25 % of the memory;
// of the 200 messages the standard asks for, that is 50). 0 done, -1 no such message, -2 database
// error, -3 too many tagged already.
int set_keep(unsigned int id, int keep) {
        sqlite3 *db;
        sqlite3_stmt *stmt;
        int rc = -2, tagged = 0;

        open_db(&db);
        if (db == NULL) return -2;
        if (keep && sqlite3_prepare_v2(db, "select count(*) from messages where coalesce(keep,0) = 1 and coalesce(deleted,0) = 0 and id <> ?1", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, (sqlite3_int64) id);
                if (sqlite3_step(stmt) == SQLITE_ROW) tagged = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
        }
        if (keep && tagged >= MAX_KEPT_MESSAGES) { sqlite3_close(db); return -3; }
        if (sqlite3_prepare_v2(db, "update messages set keep = ?2 where id = ?1 and coalesce(deleted,0) = 0", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, (sqlite3_int64) id);
                sqlite3_bind_int(stmt, 2, keep ? 1 : 0);
                if (sqlite3_step(stmt) == SQLITE_DONE) rc = sqlite3_changes(db) > 0 ? 0 : -1;
                sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return rc;
}

// Search and rescue messages (B2 = D) still waiting to be acknowledged
int pending_alarms(void) {
        sqlite3 *db;
        sqlite3_stmt *stmt;
        int n = 0;

        open_db(&db);
        if (db == NULL) return 0;
        if (sqlite3_prepare_v2(db, "select count(*) from messages where coalesce(alarm,0) = 1 and coalesce(deleted,0) = 0", -1, &stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return n;
}

// The operator has seen them: the alarm stops. Returns how many, or -1.
int acknowledge_alarms(void) {
        sqlite3 *db;
        int n = -1;

        open_db(&db);
        if (db == NULL) return -1;
        if (sqlite3_exec(db, "update messages set alarm = 0 where coalesce(alarm,0) = 1", NULL, NULL, NULL) == SQLITE_OK) n = sqlite3_changes(db);
        sqlite3_close(db);
        return n;
}

// Appends to a growing buffer; returns 0 when memory runs out
static int grow(char **buf, size_t *len, size_t *cap, const char *text, size_t n) {
        if (*len + n + 1 > *cap) {
                size_t want = (*cap + n + 1) * 2;
                char *nb = (char *) realloc(*buf, want);
                if (nb == NULL) return 0;
                *buf = nb; *cap = want;
        }
        memcpy(*buf + *len, text, n);
        *len += n;
        (*buf)[*len] = '\0';
        return 1;
}

// Every message not deleted, oldest first, as text or as CSV (RFC 4180). freq 0 = all.
// To be freed by the caller; NULL on error.
char *export_messages(int csv, int freq) {
        sqlite3 *db;
        sqlite3_stmt *stmt;
        char *out = NULL;
        size_t len = 0, cap = 0;
        char line[256];
        int ok = 1;

        purge_old_messages();
        open_db(&db);
        if (db == NULL) return NULL;
        if (csv) {
                static const char header[] = "id,bbbb,khz,received_utc,last_heard_utc,complete,characters,bad_characters,satisfactory,message\r\n";
                ok = grow(&out, &len, &cap, header, sizeof(header) - 1);
        }
        else ok = grow(&out, &len, &cap, "", 0);
        if (ok && sqlite3_prepare_v2(db, "select id,bbbb,freq,timestamp,complete,coalesce(errors,0),message,"
                                         "strftime('%Y-%m-%d %H:%M', coalesce(heard, strftime('%s', timestamp)), 'unixepoch')"
                                         " from messages where coalesce(deleted,0) = 0 and (?1 = 0 or freq = ?1) order by id asc", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, freq);
                while (ok && sqlite3_step(stmt) == SQLITE_ROW) {
                        const char *bbbb = (const char *) sqlite3_column_text(stmt, 1);
                        const char *ts = (const char *) sqlite3_column_text(stmt, 3);
                        const char *text = (const char *) sqlite3_column_text(stmt, 6);
                        const char *heard = (const char *) sqlite3_column_text(stmt, 7);
                        int f = sqlite3_column_int(stmt, 2);
                        int complete = sqlite3_column_type(stmt, 4) == SQLITE_NULL ? 1 : sqlite3_column_int(stmt, 4) != 0;
                        int errors = sqlite3_column_int(stmt, 5);
                        int n;
                        const char *khz = f == 4209 ? "4209.5" : f == 490 ? "490" : "518";

                        if (bbbb == NULL || ts == NULL || text == NULL) continue;
                        n = (int) strlen(text);
                        if (csv) {
                                const char *p;
                                snprintf(line, sizeof(line), "%d,%.4s,%s,%.16s,%.16s,%d,%d,%d,%d,\"", sqlite3_column_int(stmt, 0), bbbb, khz, ts, heard ? heard : ts,
                                         complete, n, errors, satisfactorily_received(complete, errors, n));
                                ok = grow(&out, &len, &cap, line, strlen(line));
                                for (p = text; ok && *p; p++) {
                                        // a cell that starts with = + - @ would be run as a formula by a spreadsheet
                                        if (p == text && strchr("=+-@", *p) != NULL) ok = grow(&out, &len, &cap, "'", 1);
                                        if (*p == '"') ok = ok && grow(&out, &len, &cap, "\"\"", 2);
                                        else if (*p != '\r') ok = ok && grow(&out, &len, &cap, p, 1);
                                }
                                ok = ok && grow(&out, &len, &cap, "\"\r\n", 3);
                        } else {
                                snprintf(line, sizeof(line), "==== %.4s  %s kHz  received %.16s UTC  last heard %.16s UTC  %s, %d characters, %d bad ====\r\n",
                                         bbbb, khz, ts, heard ? heard : ts, complete ? "complete" : "cut short", n, errors);
                                ok = grow(&out, &len, &cap, line, strlen(line));
                                {
                                        const char *p;
                                        for (p = text; ok && *p; p++) {
                                                if (*p == '\n') ok = grow(&out, &len, &cap, "\r\n", 2);
                                                else if (*p != '\r') ok = grow(&out, &len, &cap, p, 1);
                                        }
                                }
                                ok = ok && grow(&out, &len, &cap, "\r\n\r\n", 4);
                        }
                }
                sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        if (!ok) { free(out); return NULL; }
        return out;
}


char *get_message_text(unsigned int m_id) {
        cJSON *json;
        sqlite3 *db;
        sqlite3_stmt *stmt;
        char *json_str, *result;

        open_db(&db);
        if (db == NULL)
        {
                printf("Failed to open DB\n");
                return ((char *) 0);
        }
        json = cJSON_CreateObject();

        if (sqlite3_prepare_v2(db, "select message from messages where id = ?1 and coalesce(deleted,0) = 0", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m_id);
                if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0) != NULL) {
                        cJSON_AddStringToObject(json, "message", (const char *) sqlite3_column_text(stmt, 0));
                }
                sqlite3_finalize(stmt);
        }

        // reading a message marks it as seen
        if (sqlite3_prepare_v2(db, "update messages set age='' where id = ?1", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m_id);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
        }
        sqlite3_close(db);

        json_str = cJSON_Print(json);
        result = json_str ? strdup(json_str) : NULL;
        cJSON_free(json_str);
        cJSON_Delete(json);
        return result;
}


void free_json_str(char *json_str)
{
	cJSON_free(json_str);
}


char *get_config() {
// create a cJSON object
cJSON *json = cJSON_CreateArray();


        sqlite3 *db;
        sqlite3_stmt *stmt;
        open_db(&db);

        if (db == NULL)
        {
                printf("Failed to open DB\n");
                return ((char *) 0);
        }

        sqlite3_prepare_v2(db, "select * from config", -1, &stmt, NULL);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
                int i;
                int num_cols = sqlite3_column_count(stmt);

		cJSON *element = cJSON_CreateObject();
                for (i = 0; i < num_cols; i++)
                {
                        switch (sqlite3_column_type(stmt, i))
                        {
                        case (SQLITE3_TEXT):
				cJSON_AddStringToObject(element, sqlite3_column_name(stmt,i), (const char *) sqlite3_column_text(stmt, i));
                                break;
                        case (SQLITE_INTEGER):
 				cJSON_AddNumberToObject(element, sqlite3_column_name(stmt,i), sqlite3_column_int(stmt, i));
                                break;
                        case (SQLITE_FLOAT):
                                break;
                        default:
                                break;
                        }
                }
		cJSON_AddItemToArray(json,element);
        }

        sqlite3_finalize(stmt);

sqlite3_close(db);


char *json_str = cJSON_Print(json);
char *result = json_str ? strdup(json_str) : NULL;
cJSON_free(json_str);
cJSON_Delete(json);
return result;
}


// Copies the value of one of the six lists of followed letters ("stations518", "messages490"...).
// Returns 1 when found.
int get_config_value(const char *tag, char *out, size_t out_size) {
        sqlite3 *db;
        sqlite3_stmt *stmt;
        int found = 0;

        if (out_size == 0) return 0;
        out[0] = '\0';
        open_db(&db);
        if (db == NULL) return 0;
        if (sqlite3_prepare_v2(db, "select value from config where tag = ?1", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, tag, -1, SQLITE_STATIC);
                if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0) != NULL) {
                        snprintf(out, out_size, "%s", (const char *) sqlite3_column_text(stmt, 0));
                        found = 1;
                }
                sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return found;
}

// Returns 0 when the setting has been saved, -1 when the tag or the value are
// not acceptable and -2 on a database error.
int update_config(char* tag,char* value) {
        sqlite3 *db;
        sqlite3_stmt *stmt;
        int rc;

        if (!valid_config(tag, value)) return -1;

        open_db(&db);
        if (db == NULL)
        {
                printf("Failed to open DB\n");
                return -2;
        }
        if (sqlite3_prepare_v2(db, "update config set value = ?1 where tag = ?2", -1, &stmt, NULL) != SQLITE_OK) {
                sqlite3_close(db);
                return -2;
        }
        sqlite3_bind_text(stmt, 1, value, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, tag, -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return rc == SQLITE_DONE ? 0 : -2;
}


// ---------------------------------------------------------------------------
// Read-only API for other programs (2026 - Manuel - EA7JRS, fork EA7JRS/Navtex)
// ---------------------------------------------------------------------------

// "2026-11-22 07:12" (UTC, as stored) -> "2026-11-22T07:12:00Z"
static void iso_timestamp(const char *stored, char *out, size_t out_size)
{
        if (stored != NULL && strlen(stored) == 16)
                snprintf(out, out_size, "%.10sT%.5s:00Z", stored, stored + 11);
        else
                snprintf(out, out_size, "%s", stored != NULL ? stored : "");
}

// Every stored message with an id greater than since_id, oldest first, with its
// full text. Nothing is filtered and nothing is marked as read: this is meant
// for programs that keep their own copy and ask again with the last id they saw.
char *get_messages_since(unsigned int since_id, int limit) {
        return get_messages_changed(since_id, 0, limit);
}

// The same, and also the messages heard again after heard_after (unix time; 0 = none):
// a station repeats what is still in force, and a program that keeps its own copy
// can tell from that which messages are still being broadcast.
char *get_messages_changed(unsigned int since_id, long heard_after, int limit) {
        cJSON *json;
        sqlite3 *db;
        sqlite3_stmt *stmt;
        char *json_str, *result;

        if (limit < 1) limit = 1;
        if (limit > 500) limit = 500;
        open_db(&db);
        if (db == NULL) return NULL;
        json = cJSON_CreateArray();
        if (sqlite3_prepare_v2(db, "select id,bbbb,freq,timestamp,complete,message,age,coalesce(errors,0),coalesce(heard, strftime('%s', timestamp)),coalesce(alarm,0),coalesce(keep,0)"
                                   " from messages where coalesce(deleted,0) = 0 and (id > ?1 or (?3 > 0 and heard > ?3)) order by id asc limit ?2", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, (sqlite3_int64) since_id);
                sqlite3_bind_int(stmt, 2, limit);
                sqlite3_bind_int64(stmt, 3, (sqlite3_int64) heard_after);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                        const char *bbbb = (const char *) sqlite3_column_text(stmt, 1);
                        const char *text = (const char *) sqlite3_column_text(stmt, 5);
                        char iso[32];
                        char one[2] = { 0, 0 };
                        cJSON *m;

                        if (bbbb == NULL || strlen(bbbb) != 4 || text == NULL) continue;
                        m = cJSON_CreateObject();
                        cJSON_AddNumberToObject(m, "id", sqlite3_column_int(stmt, 0));
                        cJSON_AddStringToObject(m, "bbbb", bbbb);
                        one[0] = bbbb[0]; cJSON_AddStringToObject(m, "station", one);
                        one[0] = bbbb[1]; cJSON_AddStringToObject(m, "subject", one);
                        cJSON_AddNumberToObject(m, "number", (bbbb[2] - '0') * 10 + (bbbb[3] - '0'));
                        cJSON_AddNumberToObject(m, "freq", sqlite3_column_int(stmt, 2));
                        iso_timestamp((const char *) sqlite3_column_text(stmt, 3), iso, sizeof(iso));
                        cJSON_AddStringToObject(m, "received", iso);
                        cJSON_AddBoolToObject(m, "complete", sqlite3_column_type(stmt, 4) == SQLITE_NULL ? 1 : sqlite3_column_int(stmt, 4) != 0);
                        {
                                int errors = sqlite3_column_int(stmt, 7);
                                int n = (int) strlen(text);
                                int complete = sqlite3_column_type(stmt, 4) == SQLITE_NULL ? 1 : sqlite3_column_int(stmt, 4) != 0;
                                int slot = in_own_slot(bbbb[0], (const char *) sqlite3_column_text(stmt, 3));
                                cJSON_AddNumberToObject(m, "characters", n);
                                cJSON_AddNumberToObject(m, "errors", errors);
                                // character error rate below 4 % and complete (IMO MSC.508(105), 6.2.3)
                                cJSON_AddBoolToObject(m, "satisfactory", satisfactorily_received(complete, errors, n));
                                // last time the broadcast was received, unix time
                                cJSON_AddNumberToObject(m, "heard", (double) sqlite3_column_int64(stmt, 8));
                                // received within the ten minutes of its B1 letter
                                if (slot >= 0) cJSON_AddBoolToObject(m, "in_slot", slot);
                                // search and rescue information not yet acknowledged
                                if (sqlite3_column_int(stmt, 9)) cJSON_AddBoolToObject(m, "alarm", 1);
                                // tagged by the user for permanent retention
                                if (sqlite3_column_int(stmt, 10)) cJSON_AddBoolToObject(m, "keep", 1);
                        }
                        cJSON_AddStringToObject(m, "message", text);
                        // not yet opened in the web interface
                        // ...and for 24 hours at most (IMO MSC.508(105), 5.2.1: shown "until acknowledged or until 24 hours after receipt")
                        cJSON_AddBoolToObject(m, "unread", sqlite3_column_text(stmt, 6) != NULL && strcmp((const char *) sqlite3_column_text(stmt, 6), "NEW") == 0
                                                           && calculate_time_difference((char *) sqlite3_column_text(stmt, 3)) < 24 * 3600);
                        cJSON_AddItemToArray(json, m);
                }
                sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        json_str = cJSON_PrintUnformatted(json);
        result = json_str ? strdup(json_str) : NULL;
        cJSON_free(json_str);
        cJSON_Delete(json);
        return result;
}

// The receiver leaves here what it is doing (source, heartbeat, last phasing
// on each frequency), so the web side can tell whether it is alive.
void set_status(const char *tag, const char *value) {
        sqlite3 *db;
        sqlite3_stmt *stmt;

        if (tag == NULL || value == NULL) return;
        open_db(&db);
        if (db == NULL) return;
        if (sqlite3_prepare_v2(db, "insert into status (tag,value) values (?1,?2) on conflict(tag) do update set value = excluded.value", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, tag, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
}

void set_status_now(const char *tag) {
        char ts[TS_SIZE];
        char iso[32];

        get_current_timestamp(ts);
        iso_timestamp(ts, iso, sizeof(iso));
        set_status(tag, iso);
}

char *get_status_json(void) {
        cJSON *json;
        sqlite3 *db;
        sqlite3_stmt *stmt;
        char *json_str, *result;
        char ts[TS_SIZE], iso[32];

        open_db(&db);
        if (db == NULL) return NULL;
        json = cJSON_CreateObject();
        get_current_timestamp(ts);
        iso_timestamp(ts, iso, sizeof(iso));
        cJSON_AddStringToObject(json, "now", iso);
#ifdef PACKAGE_VERSION
        cJSON_AddStringToObject(json, "version", PACKAGE_VERSION);
#endif
        if (sqlite3_prepare_v2(db, "select tag,value from status", -1, &stmt, NULL) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW)
                        if (sqlite3_column_text(stmt, 0) != NULL && sqlite3_column_text(stmt, 1) != NULL)
                                cJSON_AddStringToObject(json, (const char *) sqlite3_column_text(stmt, 0), (const char *) sqlite3_column_text(stmt, 1));
                sqlite3_finalize(stmt);
        }
        if (sqlite3_prepare_v2(db, "select count(*), coalesce(max(id),0), coalesce(sum(coalesce(alarm,0) = 1 and coalesce(deleted,0) = 0),0) from messages", -1, &stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                        cJSON_AddNumberToObject(json, "messages", sqlite3_column_int(stmt, 0));
                        cJSON_AddNumberToObject(json, "last_id", sqlite3_column_int(stmt, 1));
                        cJSON_AddNumberToObject(json, "sar_alarms", sqlite3_column_int(stmt, 2));
                }
                sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        json_str = cJSON_PrintUnformatted(json);
        result = json_str ? strdup(json_str) : NULL;
        cJSON_free(json_str);
        cJSON_Delete(json);
        return result;
}


// ---------------------------------------------------------------------------
// Settings shared by the web interface and the receivers (fork EA7JRS/Navtex)
// ---------------------------------------------------------------------------

static int all_of(const char *v, const char *allowed, size_t max_len) {
        size_t n = strlen(v);
        if (n > max_len) return 0;
        return strspn(v, allowed) == n;
}

static int one_of(const char *v, const char *a, const char *b, const char *c, const char *d) {
        return (a && strcmp(v, a) == 0) || (b && strcmp(v, b) == 0) || (c && strcmp(v, c) == 0) || (d && strcmp(v, d) == 0);
}

// "HHMM,HHMM,..." with valid hours and minutes
static int valid_slots(const char *v) {
        size_t n = strlen(v), i;
        if (n == 0) return 1;
        if (n > 36 * 5 || (n + 1) % 5 != 0) return 0;
        for (i = 0; i < n; i += 5) {
                if (v[i] < '0' || v[i] > '2' || v[i+1] < '0' || v[i+1] > '9' || v[i+2] < '0' || v[i+2] > '5' || v[i+3] < '0' || v[i+3] > '9') return 0;
                if ((v[i] - '0') * 10 + (v[i+1] - '0') > 23) return 0;
                if (i + 4 < n && v[i+4] != ',') return 0;
        }
        return 1;
}

// "host:port" with a numeric IPv4 address or a host name, or empty
static int valid_host_port(const char *v) {
        const char *colon = strrchr(v, ':');
        int port;
        if (v[0] == '\0') return 1;
        if (colon == NULL || colon == v || !all_of(v, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-:", 80)) return 0;
        if (strchr(v, ':') != colon || !all_of(colon + 1, "0123456789", 5) || colon[1] == '\0') return 0;
        port = atoi(colon + 1);
        return port >= 1 && port <= 65535;
}

// "lat,lon" in decimal degrees
static int valid_position(const char *v) {
        double lat, lon;
        char extra;
        if (v[0] == '\0') return 1;
        if (!all_of(v, "0123456789.,-", 32)) return 0;
        if (sscanf(v, "%lf,%lf%c", &lat, &lon, &extra) != 2) return 0;
        return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

// Only known settings, each with its own rule. Returns 1 when acceptable.
int valid_setting(const char *tag, const char *value) {
        const char *key;
        if (tag == NULL || value == NULL) return 0;
        if (strcmp(tag, "position") == 0) return valid_position(value);
        if (strcmp(tag, "position_source") == 0) return one_of(value, "gps", "manual", NULL, NULL);
        // hours a message is kept after it was last heard: 72 (the time its identity is remembered) to a year
        if (strcmp(tag, "retention_hours") == 0) return all_of(value, "0123456789", 4) && value[0] != '\0' && atoi(value) >= 72 && atoi(value) <= 8760;
        // NMEA 0183 output (NRX sentences): UDP destination "host:port" and TCP server port; empty or 0 = off
        if (strcmp(tag, "nrx.udp") == 0) return valid_host_port(value);
        // a Signal K server that listens for deltas on a UDP port (data connection of type "Signal K", source UDP)
        if (strcmp(tag, "nrx.signalk") == 0) return valid_host_port(value);
        // form of the NRX sentence: the one of Furuno's receivers, or the one of IEC 61097-6 (annex C)
        if (strcmp(tag, "nrx.format") == 0) return one_of(value, "furuno", "iec", NULL, NULL);
        if (strcmp(tag, "nrx.bam") == 0) return one_of(value, "on", "off", NULL, NULL);
        // space weather now, sent by S.A.T.-APRS+: NOAA scales and the unix time they were read, "R1S0G2T1795370000"
        if (strcmp(tag, "space.wx") == 0)
            return value[0] == '\0' || (strlen(value) >= 16 && strlen(value) <= 18 && value[0] == 'R' && strchr("012345", value[1]) && value[2] == 'S' && strchr("012345", value[3])
                && value[4] == 'G' && strchr("012345", value[5]) && value[6] == 'T' && all_of(value + 7, "0123456789", 11));
        if (strcmp(tag, "nrx.tcp") == 0) return value[0] != '\0' && all_of(value, "0123456789", 5) && (atoi(value) == 0 || (atoi(value) >= 1024 && atoi(value) <= 65535));
        if ((strncmp(tag, "rx1.", 4) != 0 && strncmp(tag, "rx2.", 4) != 0)) return 0;
        key = tag + 4;
        if (strcmp(key, "enabled") == 0 || strcmp(key, "bias") == 0) return one_of(value, "0", "1", NULL, NULL);
        if (strcmp(key, "source") == 0)   return one_of(value, "sdrplay", "rtlsdr", "kiwi", NULL);
        // a KiwiSDR on the network: name or address, port, password (nothing that breaks the command it goes in)
        if (strcmp(key, "kiwi_host") == 0) return all_of(value, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-:", 80);
        if (strcmp(key, "kiwi_port") == 0) return value[0] != '\0' && all_of(value, "0123456789", 5) && atoi(value) >= 1 && atoi(value) <= 65535;
        if (strcmp(key, "kiwi_pass") == 0) return all_of(value, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-+!@*", 40);
        if (strcmp(key, "band") == 0)     return one_of(value, "mf", "hf", "auto", NULL);
        if (strcmp(key, "hf_slots") == 0) return valid_slots(value);
        if (strcmp(key, "port") == 0)     return one_of(value, "A", "B", "C", NULL);
        if (strcmp(key, "serial") == 0)   return all_of(value, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-", 32);
        if (strcmp(key, "direct") == 0)   return one_of(value, "auto", "0", "1", "2");
        if (strcmp(key, "upconv") == 0)   return all_of(value, "0123456789", 10);
        if (strcmp(key, "gain") == 0)     return strcmp(value, "auto") == 0 || (value[0] != '\0' && all_of(value, "0123456789.", 5));
        if (strcmp(key, "ppm") == 0)      return value[0] != '\0' && all_of(value, "0123456789-", 4) && strchr(value + 1, '-') == NULL;
        return 0;
}

// 0 saved, -1 not acceptable, -2 database error
int set_setting(const char *tag, const char *value) {
        sqlite3 *db;
        sqlite3_stmt *stmt;
        int rc;

        if (!valid_setting(tag, value)) return -1;
        open_db(&db);
        if (db == NULL) return -2;
        if (sqlite3_prepare_v2(db, "insert into settings (tag,value) values (?1,?2) on conflict(tag) do update set value = excluded.value", -1, &stmt, NULL) != SQLITE_OK) {
                sqlite3_close(db);
                return -2;
        }
        sqlite3_bind_text(stmt, 1, tag, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return rc == SQLITE_DONE ? 0 : -2;
}

// Copies the value of a setting into out (empty when it is not set). Returns 1 when found.
int get_setting(const char *tag, char *out, size_t out_size) {
        sqlite3 *db;
        sqlite3_stmt *stmt;
        int found = 0;

        if (out_size == 0) return 0;
        out[0] = '\0';
        open_db(&db);
        if (db == NULL) return 0;
        if (sqlite3_prepare_v2(db, "select value from settings where tag = ?1", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, tag, -1, SQLITE_STATIC);
                if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0) != NULL) {
                        snprintf(out, out_size, "%s", (const char *) sqlite3_column_text(stmt, 0));
                        found = 1;
                }
                sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return found;
}

char *get_settings_json(void) {
        cJSON *json;
        sqlite3 *db;
        sqlite3_stmt *stmt;
        char *json_str, *result;

        open_db(&db);
        if (db == NULL) return NULL;
        json = cJSON_CreateObject();
        if (sqlite3_prepare_v2(db, "select tag,value from settings", -1, &stmt, NULL) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW)
                        if (sqlite3_column_text(stmt, 0) != NULL && sqlite3_column_text(stmt, 1) != NULL)
                                cJSON_AddStringToObject(json, (const char *) sqlite3_column_text(stmt, 0), (const char *) sqlite3_column_text(stmt, 1));
                sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        json_str = cJSON_PrintUnformatted(json);
        result = json_str ? strdup(json_str) : NULL;
        cJSON_free(json_str);
        cJSON_Delete(json);
        return result;
}
