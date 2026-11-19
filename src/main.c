#include "app.h"
#include <ctype.h>
#include <openssl/crypto.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
int web,admin;
char actor[65],csrf[65];
static char body[8193],*keys[32],*values[32],cookie[65];
static size_t count;
void fail(const char *m){
 if(web){printf("Status: 400 Bad Request\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n\r\n");shell_top("Request interrupted");printf("<section class='panel'><h2>Request interrupted</h2><p>");esc(m);puts("</p><a href='?p=home'>Return to workspace</a></section>");end_page();}
 else fprintf(stderr,"%s\n",m);
 exit(1);
}
void internal(void){fprintf(stderr,"Inventory error: %s\n",db?sqlite3_errmsg(db):"service unavailable");fail("The operation could not be completed. Check the service log and retry.");}
const char *field(const char *k){for(size_t i=0;i<count;i++)if(!strcmp(keys[i],k))return values[i];return "";}
void set_field(const char *k,char *v){if(count==32)fail("Too many fields.");keys[count]=(char*)k;values[count++]=v;}
static void decode(char *p){
 char *w=p;while(*p){int c=(unsigned char)*p++;
 if(c=='+')c=' ';else if(c=='%'){if(!p[0] || !p[1] || !isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1]))fail("Invalid request.");char h[]={p[0],p[1],0};c=(int)strtol(h,NULL,16);p+=2;}
 if(c<32 || c==127)fail("Invalid control character.");
 *w++=(char)c;}*w=0;
}
static void parse(void){
 char *save=NULL;
 for(char *part=strtok_r(body,"&",&save);part;part=strtok_r(NULL,"&",&save)){
  char *v=strchr(part,'=');if(count==32 || !v)fail("Invalid form.");*v++=0;decode(part);decode(v);
  for(size_t i=0;i<count;i++)if(!strcmp(keys[i],part))fail("Ambiguous form.");
  keys[count]=part;values[count++]=v;
 }
}
static const char *env(const char *k){const char *v=getenv(k);return v?v:"";}
static int hexkey(const char *s){return strlen(s)==64 && strspn(s,"0123456789abcdef")==64;}
static void set_cookie(const char *token,int ttl){
 printf("Set-Cookie: inventory=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%d%s\r\n",token,ttl,!strcmp(env("HTTPS"),"on")||!strcmp(env("COOKIE_SECURE"),"1")?"; Secure":"");
}
void redirect(const char *p){printf("Status: 303 See Other\r\nLocation: /cgi-bin/inventory?p=%s\r\nCache-Control: no-store\r\n\r\n",p);}
static void auth(void){
 const char *raw=env("HTTP_COOKIE");
 while(*raw){while(*raw==' '||*raw==';')raw++;size_t n=strcspn(raw,";");if(n==74&&!strncmp(raw,"inventory=",10)){memcpy(cookie,raw+10,64);break;}raw+=n;}
 if(!hexkey(cookie))return;
 sqlite3_stmt *s=query("SELECT a.name,a.admin,s.csrf FROM session s JOIN account a ON a.name=s.user WHERE token=%Q AND expires>unixepoch()",cookie);
 if(row(s)){snprintf(actor,sizeof actor,"%s",str(s,0));admin=(int)val(s,1);snprintf(csrf,sizeof csrf,"%s",str(s,2));sqlite3_finalize(s);}
}
static void render_headers(void){puts("Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; object-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'\r\n\r");}
static void gateway(void){
 open_store(0);auth();const char *method=env("REQUEST_METHOD");int post=!strcmp(method,"POST");
 if(post){
  if(strcmp(env("CONTENT_TYPE"),"application/x-www-form-urlencoded"))fail("Unsupported form type.");
  size_t n=(size_t)number(env("CONTENT_LENGTH"),1,sizeof body-1);if(fread(body,1,n,stdin)!=n || memchr(body,0,n))fail("Incomplete form.");
 }else{if(strcmp(method,"GET") || strlen(env("QUERY_STRING"))>=sizeof body)fail("Invalid request.");strcpy(body,env("QUERY_STRING"));}
 parse();
 if(post && !strcmp(field("action"),"login")){
  if(strcmp(env("HTTP_SEC_FETCH_SITE"),"cross-site")==0)fail("Open the application directly to sign in.");
  const char *name=field("name"),*pass=field("password");if(strlen(name)>64 || strlen(pass)>128)fail("Invalid credentials.");
  if(!verify(name,pass)){render_headers();login_page("Sign-in failed. Check your details or retry after five minutes.");return;}
  char token[65],key[65];random_hex(token);random_hex(key);
  execute("BEGIN IMMEDIATE; DELETE FROM session WHERE expires<=unixepoch() OR token=%Q; INSERT INTO session VALUES(%Q,%Q,%Q,unixepoch()+28800);COMMIT",cookie,token,name,key);
  set_cookie(token,28800);redirect("home");return;
 }
 if(!*actor){render_headers();login_page("");return;}
 if(!post){render_headers();pages(field("p"));return;}
 if(!hexkey(field("csrf")) || CRYPTO_memcmp(field("csrf"),csrf,64))fail("This form expired. Reload the workspace.");
 const char *a=field("action");if(!strcmp(a,"transaction")){a=field("kind");if(strcmp(a,"purchase")&&strcmp(a,"sale"))fail("Invalid transaction type.");}if(!strcmp(a,"logout")){execute("DELETE FROM session WHERE token=%Q",cookie);set_cookie("",0);redirect("home");return;}
 const char *op=field("op");if(!hexkey(op))fail("Reload the form and try again.");
 execute("BEGIN IMMEDIATE");
 if(!scalar("SELECT count(*) FROM operation WHERE token=%Q AND user=%Q",op,actor)){
  execute("INSERT INTO operation(token,user) VALUES(%Q,%Q)",op,actor);change(a);
 }
 Amount receipt=scalar("SELECT receipt FROM operation WHERE token=%Q AND user=%Q",op,actor);
 execute("COMMIT");if(receipt){char dest[90];snprintf(dest,sizeof dest,"receipt&id=%lld",receipt);redirect(dest);}
 else redirect(!strcmp(a,"user")?"team":"products");
}
void read_pass(char *buf,size_t n){
 struct termios old,t;int tty=isatty(0);if(tty){if(tcgetattr(0,&old))fail("Terminal unavailable.");t=old;t.c_lflag &= ~ECHO;if(tcsetattr(0,TCSAFLUSH,&t))fail("Terminal unavailable.");}
 fputs("Password (12–128 characters): ",stderr);fflush(stderr);char *r=fgets(buf,(int)n,stdin);
 if(tty){tcsetattr(0,TCSAFLUSH,&old);fputc('\n',stderr);}
 if(!r||!strchr(buf,'\n'))fail("Invalid password length.");
 buf[strcspn(buf,"\r\n")]=0;
}
int main(int argc,char **argv){
 umask(0077);web=getenv("GATEWAY_INTERFACE")!=NULL;
 if(web){gateway();return 0;}
 if(argc==1){setvbuf(stdin,NULL,_IONBF,0);terminal();return 0;}
 if(argc!=3){fputs("inventory init ADMIN | backup NEW_FILE | restore SNAPSHOT | import LEGACY_DIRECTORY | reset USER\n",stderr);return 2;}
 int init=!strcmp(argv[1],"init");open_store(init);
 if(init){execute("BEGIN IMMEDIATE");if(scalar("SELECT count(*) FROM sqlite_master WHERE type='table'"))fail("Database already initialized.");schema();char p[132];read_pass(p,sizeof p);make_user(argv[2],p,1);OPENSSL_cleanse(p,sizeof p);execute("COMMIT");}
 else if(!strcmp(argv[1],"backup")||!strcmp(argv[1],"restore"))snapshot(argv[2],!strcmp(argv[1],"restore"));
 else if(!strcmp(argv[1],"import"))migrate(argv[2]);
 else if(!strcmp(argv[1],"reset")){
  execute("BEGIN IMMEDIATE");sqlite3_stmt *s=query("SELECT admin FROM account WHERE name=%Q",argv[2]);if(!row(s))fail("Unknown account.");int role=(int)val(s,0);sqlite3_finalize(s);char p[132];read_pass(p,sizeof p);
  execute("DELETE FROM session WHERE user=%Q;DELETE FROM account WHERE name=%Q",argv[2],argv[2]);make_user(argv[2],p,role);OPENSSL_cleanse(p,sizeof p);execute("COMMIT");
 }else fail("Unknown command.");
 sqlite3_close(db);puts("Completed.");return 0;
}
