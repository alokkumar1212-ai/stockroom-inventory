#ifndef APP_H
#define APP_H
#include <sqlite3.h>
#include <stdio.h>
typedef long long Amount;
extern sqlite3 *db;
extern int web, admin;
extern char actor[65], csrf[65];
void terminal(void);
void read_pass(char *buf,size_t n);
void set_field(const char *key,char *v);
void fail(const char *message);
void internal(void);
sqlite3_stmt *query(const char *sql,...);
int row(sqlite3_stmt *s);
void execute(const char *sql,...);
Amount scalar(const char *sql,...);
#define val(s,i) sqlite3_column_int64(s,i)
#define str(s,i) ((const char *)sqlite3_column_text(s,i))
void open_store(int create);
void schema(void);
void random_hex(char *out);
void make_user(const char *name,const char *password,int role);
int verify(const char *name,const char *password);
void snapshot(const char *path,int restore);
void migrate(const char *dir);
Amount number(const char *v,Amount min,Amount max);
Amount price(const char *v);
const char *field(const char *key);
void change(const char *action);
void pages(const char *page);
void esc(const char *s);
void login_page(const char *error);
void shell_top(const char *title);
void end_page(void);
void redirect(const char *page);
#endif
