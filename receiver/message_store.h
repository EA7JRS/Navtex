char *get_message_list();
void get_current_timestamp(char *cts);
char *get_message_text(unsigned int m_id);
char *get_config();
int update_config(char* tag,char* value);

/* complete: 1 when the message reached its NNNN, 0 when reception was cut */
int add_message(char *bbbb,char *message, int freq, int complete);
/* 1: print every received character (for a terminal); 0: only events (for a service) */
extern int navtex_verbose;
/* 1: the receiver is on the HF channel (4209.5 kHz) instead of 518 and 490 kHz */
extern int navtex_hf;
char *slots_plan() ;
long calculate_time_difference(char *timestamp);
void free_json_str(char *json_str);
void purge_old_messages();

/* Read-only API for other programs */
char *get_messages_since(unsigned int since_id, int limit);
char *get_status_json(void);
/* What the receiver is doing, for get_status_json() */
void set_status(const char *tag, const char *value);
void set_status_now(const char *tag);

/* Settings shared by the web interface and the receivers */
int valid_setting(const char *tag, const char *value);
int set_setting(const char *tag, const char *value);
int get_setting(const char *tag, char *out, size_t out_size);
char *get_settings_json(void);

/* What the standards ask of a NAVTEX receiver (IMO MSC.148(77), NAVTEX Manual) */
int count_bad_characters(const char *message);
int satisfactorily_received(int complete, int errors, int length);
int better_copy(int new_complete, int new_errors, int new_len, int old_complete, int old_errors, int old_len);
int slot_minute(char b1);
int in_own_slot(char b1, const char *timestamp);
long retention_seconds(void);
/* Hide one message (id) or all (0); export them; the API with repeats */
int delete_messages(unsigned int id);
char *export_messages(int csv, int freq);
char *get_messages_changed(unsigned int since_id, long heard_after, int limit);
