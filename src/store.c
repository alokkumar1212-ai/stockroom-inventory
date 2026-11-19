#include "app.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
sqlite3 *db;
static sqlite3_stmt *prepare(const char *sql,va_list args){
 char *q=sqlite3_vmprintf(sql,args); sqlite3_stmt *s=NULL;
 if(!q || sqlite3_prepare_v2(db,q,-1,&s,NULL)!=SQLITE_OK)internal();
 sqlite3_free(q); return s;
}
sqlite3_stmt *query(const char *sql,...){
 va_list a; va_start(a,sql); sqlite3_stmt *s=prepare(sql,a); va_end(a); return s;
}
int row(sqlite3_stmt *s){
 int r=sqlite3_step(s); if(r==SQLITE_ROW)return 1;
 if(r!=SQLITE_DONE)internal();
 sqlite3_finalize(s); return 0;
}
Amount scalar(const char *sql,...){
 va_list a; va_start(a,sql); sqlite3_stmt *s=prepare(sql,a); va_end(a);
 if(!row(s))return 0;
 Amount n=val(s,0); sqlite3_finalize(s); return n;
}
void execute(const char *sql,...){
 va_list a; va_start(a,sql); char *q=sqlite3_vmprintf(sql,a); va_end(a);
 if(!q || sqlite3_exec(db,q,NULL,NULL,NULL)!=SQLITE_OK)internal();
 sqlite3_free(q);
}
void open_store(int create){
 const char *path=getenv("INVENTORY_DB"); if(!path)path="inventory.sqlite";
 if(sqlite3_open_v2(path,&db,SQLITE_OPEN_READWRITE|(create?SQLITE_OPEN_CREATE:0),NULL)!=SQLITE_OK)internal();
 sqlite3_busy_timeout(db,5000); execute("PRAGMA foreign_keys=ON");
}
void schema(void){
 execute("CREATE TABLE product(id INTEGER PRIMARY KEY CHECK(id>0),name TEXT NOT NULL,category TEXT NOT NULL,supplier TEXT NOT NULL,qty INTEGER NOT NULL CHECK(qty BETWEEN 0 AND 2147483647),value INTEGER NOT NULL CHECK(typeof(value)='integer' AND value>=0),cost INTEGER NOT NULL,sell INTEGER NOT NULL,min INTEGER NOT NULL,lead INTEGER NOT NULL,archived INTEGER NOT NULL DEFAULT 0);"
 "CREATE TABLE movement(id INTEGER PRIMARY KEY,product REFERENCES product(id),delta INTEGER NOT NULL,kind TEXT,reference TEXT,actor TEXT,at TEXT DEFAULT CURRENT_TIMESTAMP,unit_price INTEGER);"
 "CREATE TABLE sale(id INTEGER PRIMARY KEY AUTOINCREMENT,product REFERENCES product(id),name TEXT,qty INTEGER,price INTEGER,total INTEGER,cogs INTEGER,customer TEXT,at TEXT DEFAULT CURRENT_TIMESTAMP);"
 "CREATE INDEX movement_product ON movement(product,id);CREATE INDEX sale_product ON sale(product,at);"
 "CREATE TABLE account(name TEXT PRIMARY KEY,salt TEXT,hash TEXT,admin INTEGER CHECK(admin IN(0,1)),fails INTEGER DEFAULT 0,locked INTEGER DEFAULT 0);"
 "CREATE TABLE session(token TEXT PRIMARY KEY,user REFERENCES account(name),csrf TEXT,expires INTEGER);"
 "CREATE TABLE operation(token TEXT PRIMARY KEY,user TEXT,receipt INTEGER);PRAGMA user_version=1;");
}
void random_hex(char *out){
 unsigned char bytes[32]; if(RAND_bytes(bytes,sizeof bytes)!=1)internal();
 for(size_t i=0;i<sizeof bytes;i++)sprintf(out+i*2,"%02x",bytes[i]);
}
static void digest(const char *pass,const char *salt,char *out){
 unsigned char h[32]; if(!PKCS5_PBKDF2_HMAC(pass,(int)strlen(pass),(const unsigned char*)salt,(int)strlen(salt),600000,EVP_sha256(),32,h))internal();
 for(size_t i=0;i<32;i++)sprintf(out+i*2,"%02x",h[i]);
 OPENSSL_cleanse(h,sizeof h);
}
void make_user(const char *name,const char *pass,int role){
 if(!*name || strlen(name)>64 || strlen(pass)<12 || strlen(pass)>128)fail("Use a username of 1–64 characters and a password of 12–128 characters.");
 if(scalar("SELECT count(*) FROM account WHERE name=%Q",name))fail("Username already exists.");
 char salt[65],hash[65];random_hex(salt);digest(pass,salt,hash);
 execute("INSERT INTO account(name,salt,hash,admin) VALUES(%Q,%Q,%Q,%d)",name,salt,hash,role);
}
int verify(const char *name,const char *pass){
 sqlite3_stmt *s=query("SELECT salt,hash,locked>unixepoch() FROM account WHERE name=%Q",name);
 char hash[65];if(!row(s)){digest(pass,"absent-account",hash);return 0;}
 if(val(s,2)){sqlite3_finalize(s);return 0;}
 digest(pass,str(s,0),hash); int ok=strlen(str(s,1))==64 && CRYPTO_memcmp(hash,str(s,1),64)==0;
 sqlite3_finalize(s);
 if(ok)execute("UPDATE account SET fails=0,locked=0 WHERE name=%Q",name);
 else execute("UPDATE account SET fails=(fails+1)%%5,locked=CASE WHEN fails=4 THEN unixepoch()+300 ELSE 0 END WHERE name=%Q",name);
 return ok;
}
Amount number(const char *s,Amount min,Amount max){
 Amount n=0; if(!*s)fail("Enter a whole number.");
 for(;*s;s++){if(!isdigit((unsigned char)*s)||n>(max-(*s-'0'))/10)fail("Number outside the allowed range.");n=n*10+*s-'0';}
 if(n<min || n>max)fail("Number outside the allowed range.");
 return n;
}
Amount price(const char *s){
 char whole[16];size_t n=strcspn(s,".");if(!n || n>=sizeof whole)fail("Invalid price.");
 memcpy(whole,s,n);whole[n]=0;Amount p=number(whole,0,1000000000)*100;
 if(s[n]){const char *f=s+n+1;size_t len=strlen(f);if(!len||len>2)fail("Use up to two decimal places.");p+=number(f,0,99)*(len==1?10:1);}
 return p;
}
static Amount product_id(void){return number(field("id"),1,2147483647);}
static const char *required(const char *key){const char *s=field(key);if(!*s || strlen(s)>120)fail("Complete all fields (maximum 120 bytes each).");return s;}
void change(const char *action){
 if(!strcmp(action,"user")){if(!admin)fail("Administrator access required.");make_user(required("name"),field("password"),!strcmp(field("role"),"admin"));return;}
 Amount id=product_id();
 if(!strcmp(action,"new") || !strcmp(action,"edit")){
  const char *name=required("name"),*cat=required("category"),*sup=required("supplier");
  Amount sell=price(field("sell")),min=number(field("min"),0,2147483647),lead=number(field("lead"),0,3650);
  if(!strcmp(action,"edit")){execute("UPDATE product SET name=%Q,category=%Q,supplier=%Q,sell=%lld,min=%lld,lead=%lld WHERE id=%lld AND archived=0",name,cat,sup,sell,min,lead,id);if(!sqlite3_changes(db))fail("Product unavailable.");return;}
  if(scalar("SELECT count(*) FROM product WHERE id=%lld",id))fail("This product ID is already in use.");
  Amount qty=number(field("qty"),0,2147483647),cost=price(field("cost"));
  if(qty && cost>9000000000000000LL/qty)fail("Inventory value exceeds the supported limit.");
  execute("INSERT INTO product VALUES(%lld,%Q,%Q,%Q,%lld,%lld,%lld,%lld,%lld,%lld,0)",id,name,cat,sup,qty,qty*cost,cost,sell,min,lead);
  if(qty)execute("INSERT INTO movement(product,delta,kind,reference,actor,unit_price) VALUES(%lld,%lld,'Opening','Opening balance',%Q,%lld)",id,qty,actor,cost);
  return;
 }
 sqlite3_stmt *s=query("SELECT qty,value,sell,name FROM product WHERE id=%lld AND archived=0",id);if(!row(s))fail("Product unavailable.");
 Amount stock=val(s,0),value=val(s,1),sell=val(s,2);char name[121];snprintf(name,sizeof name,"%s",str(s,3));sqlite3_finalize(s);
 if(!strcmp(action,"archive")){if(!admin || stock)fail("Only an administrator may archive a product with zero stock.");execute("UPDATE product SET archived=1 WHERE id=%lld",id);return;}
 Amount qty=number(field("qty"),1,2147483647);const char *ref=required("reference");
 Amount unit_price=sell;
 if(!strcmp(action,"purchase")){
  Amount cost=price(field("cost"));unit_price=cost;if(qty>2147483647-stock || cost>(9000000000000000LL-value)/qty)fail("Stock or value limit exceeded.");
  execute("UPDATE product SET qty=qty+%lld,value=value+%lld,cost=%lld WHERE id=%lld",qty,qty*cost,cost,id);
 }else if(!strcmp(action,"sale")){
  if(qty>stock)fail("Insufficient stock. The sale was not recorded.");
  if(sell>9000000000000000LL/qty)fail("Sale exceeds the supported limit.");
  Amount cogs=(value/stock)*qty+((value%stock)*qty+stock/2)/stock;
  execute("UPDATE product SET qty=qty-%lld,value=value-%lld WHERE id=%lld",qty,cogs,id);
  execute("INSERT INTO sale(product,name,qty,price,total,cogs,customer) VALUES(%lld,%Q,%lld,%lld,%lld,%lld,%Q)",id,name,qty,sell,qty*sell,cogs,ref);
  execute("UPDATE operation SET receipt=%lld WHERE token=%Q",sqlite3_last_insert_rowid(db),field("op"));qty=-qty;
 }else fail("Unknown action.");
 execute("INSERT INTO movement(product,delta,kind,reference,actor,unit_price) VALUES(%lld,%lld,%Q,%Q,%Q,%lld)",id,qty,qty<0?"Sale":"Purchase",ref,actor,unit_price);
}
static void validate(sqlite3 *source){
 sqlite3_stmt *s=NULL;
 const char *checks[]={"PRAGMA integrity_check","PRAGMA foreign_key_check","SELECT 1 WHERE (SELECT user_version FROM pragma_user_version)!=1 OR (SELECT count(*) FROM account WHERE admin=1)=0 OR EXISTS(SELECT 1 FROM product p WHERE qty!=(SELECT coalesce(sum(delta),0) FROM movement WHERE product=p.id))"};
 const char *structure[]={
  "SELECT id,name,category,supplier,qty,value,cost,sell,min,lead,archived FROM product LIMIT 0",
  "SELECT id,product,delta,kind,reference,actor,at,unit_price FROM movement LIMIT 0",
  "SELECT id,product,name,qty,price,total,cogs,customer,at FROM sale LIMIT 0",
  "SELECT name,salt,hash,admin,fails,locked FROM account LIMIT 0",
  "SELECT token,user,csrf,expires FROM session LIMIT 0",
  "SELECT token,user,receipt FROM operation LIMIT 0"};
 for(size_t i=0;i<sizeof structure/sizeof structure[0];i++){
  if(sqlite3_prepare_v2(source,structure[i],-1,&s,NULL)!=SQLITE_OK)fail("Incomplete backup schema.");
  sqlite3_finalize(s);
 }
 for(int i=0;i<3;i++){
  if(sqlite3_prepare_v2(source,checks[i],-1,&s,NULL)!=SQLITE_OK)fail("Invalid backup schema.");
  int rc=sqlite3_step(s);int ok=i==0?(rc==SQLITE_ROW && !strcmp(str(s,0),"ok")):rc==SQLITE_DONE;sqlite3_finalize(s);if(!ok)fail("Backup failed integrity checks.");
 }
}
void snapshot(const char *path,int restore){
 sqlite3 *other=NULL;
 if(!restore){int fd=open(path,O_CREAT|O_EXCL|O_WRONLY,0600);if(fd<0)fail("Choose a new backup filename.");close(fd);}
 if(sqlite3_open_v2(path,&other,restore?SQLITE_OPEN_READONLY:SQLITE_OPEN_READWRITE,NULL)!=SQLITE_OK)fail("Cannot open backup.");
 sqlite3 *source=restore?other:db,*dest=restore?db:other;
 validate(source);sqlite3_backup *b=sqlite3_backup_init(dest,"main",source,"main");if(!b)fail("Backup initialization failed.");
 int rc=sqlite3_backup_step(b,-1);int done=sqlite3_backup_finish(b);
 if(rc!=SQLITE_DONE || done!=SQLITE_OK){if(!restore)unlink(path);fail("Database busy or copy failed. Retry.");}
 if(restore)execute("DELETE FROM session");
 else if(sqlite3_exec(other,"DELETE FROM session",NULL,NULL,NULL)!=SQLITE_OK)fail("Failed to clear backup sessions.");
 sqlite3_close(other);
}
