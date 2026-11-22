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
static int schema_checked = 0;

// An incomplete reception does not replace a complete copy of the same message
// received less than this long ago (a station repeats its messages every 4 hours).
#define KEEP_COMPLETE_COPY_AGE 60*60*24

#define MESSAGE_PURGE_AGE 60*60*72  			//72 hours in seconds
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
                sqlite3_exec(*db, "create table if not exists status (tag text primary key, value text)", NULL, NULL, NULL);
                schema_checked = 1;
        }
}

// Only these settings exist, and their values are lists of NAVTEX B1/B2 letters.
static int valid_config(const char *tag, const char *value) {
        static const char *tags[] = { "stations518", "messages518", "stations490", "messages490" };
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


int add_message(char *bbbb, char *message, int freq, int complete) {
        char timestamp_str[TS_SIZE];
        sqlite3 *db;
        sqlite3_stmt *stmt;
        int rc;
        int keep_existing = 0;

        if (bbbb == NULL || message == NULL || strlen(bbbb) != 4 || message[0] == '\0') return -1;

        get_current_timestamp(timestamp_str);

	open_db(&db);
        if (db == NULL)
        {
                printf("Failed to open DB\n");
                return -1;
        }

        // What is already stored for this message identity on this frequency?
        if (sqlite3_prepare_v2(db, "select complete, length(message), timestamp from messages where bbbb = ?1 and freq = ?2 order by id desc limit 1", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, bbbb, -1, SQLITE_STATIC);
                sqlite3_bind_int(stmt, 2, freq);
                if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 2) != NULL) {
                        int old_complete = sqlite3_column_type(stmt, 0) == SQLITE_NULL ? 1 : sqlite3_column_int(stmt, 0);
                        int old_len = sqlite3_column_int(stmt, 1);
                        long age = calculate_time_difference((char *) sqlite3_column_text(stmt, 2));
                        if (!complete && age < KEEP_COMPLETE_COPY_AGE &&
                            (old_complete || old_len >= (int) strlen(message)))
                                keep_existing = 1;   // a cut reception is not better than what we have
                }
                sqlite3_finalize(stmt);
        }
        if (keep_existing) {
                sqlite3_close(db);
                printf("%d kHz: %s received incomplete (%d characters), keeping the stored copy\n", freq, bbbb, (int) strlen(message));
                return 0;
        }

	sqlite3_prepare_v2(db, "delete from messages where bbbb = ?1 and freq = ?2", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, bbbb, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, freq);
	sqlite3_step(stmt); 
        sqlite3_finalize(stmt);

	sqlite3_prepare_v2(db, "insert into messages (bbbb,message,timestamp,age,freq,complete) values ( ?1 , ?2 , ?3 ,'NEW',?4,?5)", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, bbbb,          -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, message,       -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, timestamp_str, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, freq);
        sqlite3_bind_int(stmt, 5, complete ? 1 : 0);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
	sqlite3_close(db);
        if (rc != SQLITE_DONE) {
                printf("%d kHz: %s could not be stored\n", freq, bbbb);
                return -2;
        }
        printf("%d kHz: %s stored, %s, %d characters\n", freq, bbbb, complete ? "complete" : "incomplete", (int) strlen(message));
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
        sqlite3_prepare_v2(db, "select messages.id as id,bbbb,timestamp,age,freq,complete from messages, config AS CO1, config AS CO2 where (freq=518 and CO1.tag='stations518' and instr(CO1.value,substr(bbbb,1,1))>0 and CO2.tag='messages518' and instr(CO2.value,substr(bbbb,2,1)) > 0) OR  (freq=490 and CO1.tag='stations490' and instr(CO1.value,substr(bbbb,1,1))>0 and CO2.tag='messages490' and instr(CO2.value,substr(bbbb,2,1)) > 0) order by timestamp desc", -1, &stmt, NULL);



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




void purge_old_messages() {

        sqlite3 *db;
        sqlite3_stmt *stmt;
	int purge_list[100];
	int pl_index;
	long td;
	int i;
	int rc;

	open_db(&db);
        if (db == NULL)
        {
                printf("Failed to open DB\n");
		return;
        }
        pl_index=0;
        rc = sqlite3_prepare_v2(db, "select id,bbbb,timestamp from messages order by timestamp asc", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
       		fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        	sqlite3_close(db);
        	return;
    	}

        while ((sqlite3_step(stmt) == SQLITE_ROW) && (pl_index<(99))) {
        	td = calculate_time_difference((char*)sqlite3_column_text(stmt, 2) );
		if (td > MESSAGE_PURGE_AGE) {
			purge_list[pl_index]=sqlite3_column_int(stmt, 0);
			pl_index++;
		}
        }
        sqlite3_finalize(stmt);
	i=0;
	while ( i < pl_index) {
        	sqlite3_prepare_v2(db, "delete from messages where id = ?1 ", -1, &stmt, NULL);
		sqlite3_bind_int(stmt, 1,purge_list[i]);
		sqlite3_step(stmt); 
		sqlite3_finalize(stmt);
		i++;
        }
	sqlite3_close(db);


return;
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

        if (sqlite3_prepare_v2(db, "select message from messages where id = ?1", -1, &stmt, NULL) == SQLITE_OK) {
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
        cJSON *json;
        sqlite3 *db;
        sqlite3_stmt *stmt;
        char *json_str, *result;

        if (limit < 1) limit = 1;
        if (limit > 500) limit = 500;
        open_db(&db);
        if (db == NULL) return NULL;
        json = cJSON_CreateArray();
        if (sqlite3_prepare_v2(db, "select id,bbbb,freq,timestamp,complete,message,age from messages where id > ?1 order by id asc limit ?2", -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(stmt, 1, (sqlite3_int64) since_id);
                sqlite3_bind_int(stmt, 2, limit);
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
                        cJSON_AddStringToObject(m, "message", text);
                        // not yet opened in the web interface
                        cJSON_AddBoolToObject(m, "unread", sqlite3_column_text(stmt, 6) != NULL && strcmp((const char *) sqlite3_column_text(stmt, 6), "NEW") == 0);
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
        if (sqlite3_prepare_v2(db, "select tag,value from status", -1, &stmt, NULL) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW)
                        if (sqlite3_column_text(stmt, 0) != NULL && sqlite3_column_text(stmt, 1) != NULL)
                                cJSON_AddStringToObject(json, (const char *) sqlite3_column_text(stmt, 0), (const char *) sqlite3_column_text(stmt, 1));
                sqlite3_finalize(stmt);
        }
        if (sqlite3_prepare_v2(db, "select count(*), coalesce(max(id),0) from messages", -1, &stmt, NULL) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                        cJSON_AddNumberToObject(json, "messages", sqlite3_column_int(stmt, 0));
                        cJSON_AddNumberToObject(json, "last_id", sqlite3_column_int(stmt, 1));
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
