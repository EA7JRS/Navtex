char *get_message_list();
void get_current_timestamp(char *cts);
char *get_message_text(unsigned int m_id);
char *get_config();
int update_config(char* tag,char* value);

/* complete: 1 when the message reached its NNNN, 0 when reception was cut */
int add_message(char *bbbb,char *message, int freq, int complete);
/* 1: print every received character (for a terminal); 0: only events (for a service) */
extern int navtex_verbose;
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
