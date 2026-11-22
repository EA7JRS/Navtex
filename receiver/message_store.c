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


int add_message(char *bbbb, char *message, int freq) {
        char timestamp_str[TS_SIZE];

        sqlite3 *db;
        sqlite3_stmt *stmt;


        get_current_timestamp(timestamp_str);
         
	open_db(&db);
        if (db == NULL)
        {
                printf("Failed to open DB\n");
                return -1;
        }
	sqlite3_prepare_v2(db, "delete from messages where bbbb = ?1 ", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, bbbb, -1, SQLITE_STATIC);
	sqlite3_step(stmt); 
        sqlite3_finalize(stmt);

	sqlite3_prepare_v2(db, "insert into messages (bbbb,message,timestamp,age,freq) values ( ?1 , ?2 , ?3 ,'NEW',?4)", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, bbbb,          -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, message,       -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, timestamp_str, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, freq);
 
	if (sqlite3_step(stmt) == SQLITE_DONE) 
	{ 
 	       sqlite3_finalize(stmt);
		sqlite3_close(db);
		return 0; 
	}
	else
	{ 
       		sqlite3_finalize(stmt);
		sqlite3_close(db);
		return -2; 
	}
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
        sqlite3_prepare_v2(db, "select messages.id as id,bbbb,timestamp,age,freq from messages, config AS CO1, config AS CO2 where (freq=518 and CO1.tag='stations518' and instr(CO1.value,substr(bbbb,1,1))>0 and CO2.tag='messages518' and instr(CO2.value,substr(bbbb,2,1)) > 0) OR  (freq=490 and CO1.tag='stations490' and instr(CO1.value,substr(bbbb,1,1))>0 and CO2.tag='messages490' and instr(CO2.value,substr(bbbb,2,1)) > 0) order by timestamp desc", -1, &stmt, NULL);



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
