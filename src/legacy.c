#include "app.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static int file_no,line_no;
static const char *files[]={"inventory.dat","movements.dat","sales.dat","users.dat"};
static void bad(void){fprintf(stderr,"%s line %d: ",files[file_no],line_no);fail("Invalid or inconsistent legacy data. Import rolled back.");}
static Amount cents(const char *v){
 const char *d=strchr(v,'.');if(!d)return number(v,0,90000000000000LL)*100;
 size_t len=strlen(d+1);if(!len||len>4||strspn(d+1,"0123456789")!=len)bad();
 char b[48];if(strlen(v)>=sizeof b)bad();strcpy(b,v);
 if(len>2)b[(d-v)+3]=0;
 b[d-v]=0;
 return number(b,0,90000000000000LL)*100+(d[1]-'0')*10+(len>=2?d[2]-'0':0)+(len>=3 && d[3]>='5');
}
static void date_ok(const char *s){if(strlen(s)!=19 || !scalar("SELECT datetime(%Q) IS NOT NULL",s))bad();}
static void import_row(char **v){
 if(file_no==3){
  if(!*v[0] || strlen(v[0])>64)bad();
  if(scalar("SELECT count(*) FROM imported_user WHERE name=%Q",v[0]))bad();
  execute("INSERT INTO imported_user VALUES(%Q)",v[0]);
  int role=number(v[2],1,2)==1;number(v[3],0,1);
  if(scalar("SELECT count(*) FROM account WHERE name=%Q",v[0])){
   if(!role || !scalar("SELECT admin FROM account WHERE name=%Q",v[0]))bad();
   return;
  }
  char disabled[65];random_hex(disabled);make_user(v[0],disabled,role);return;
 }
 Amount id=number(v[file_no==2?1:0],1,2147483647);
 if(file_no==0){
  if(!*v[1]||!*v[2]||!*v[8]||strlen(v[1])>120||strlen(v[2])>120||strlen(v[8])>120)bad();
  Amount qty=number(v[3],0,2147483647),cost=cents(v[4]),sell=cents(v[5]);if(qty && cost>9000000000000000LL/qty)bad();
  execute("INSERT INTO product VALUES(%lld,%Q,%Q,%Q,%lld,%lld,%lld,%lld,%lld,%lld,0)",id,v[1],v[2],v[8],qty,qty*cost,cost,sell,number(v[6],0,2147483647),number(v[7],0,3650));return;
 }
 if(!scalar("SELECT count(*) FROM product WHERE id=%lld",id))bad();
 if(file_no==1){
  int neg=*v[1]=='-';Amount q=number(v[1]+neg,1,2147483647);date_ok(v[4]);const char *kind=!strcmp(v[2],"SALE")?"Sale":!strcmp(v[2],"PURCHASE")?"Purchase":!strcmp(v[2],"OPENING")?"Opening":NULL;
  if(!kind || neg!=(!strcmp(kind,"Sale")))bad();
  execute("INSERT INTO movement(product,delta,kind,reference,actor,at,unit_price) VALUES(%lld,%lld,%Q,%Q,'Legacy import',%Q,%lld)",id,neg?-q:q,kind,v[5],v[4],cents(v[3]));return;
 }
 Amount inv=number(v[0],1,9000000000000000LL),qty=number(v[2],1,2147483647),cost=cents(v[3]),unit=cents(v[4]),total=cents(v[5]);date_ok(v[7]);
 if(cost>9000000000000000LL/qty || unit>9000000000000000LL/qty)bad();
 /* Preserve the historical sale total and profit; the unit amount may round. */
 int neg=*v[6]=='-';Amount profit=cents(v[6]+neg)*(neg?-1:1);if(profit>total)bad();
 execute("INSERT INTO sale SELECT %lld,id,name,%lld,%lld,%lld,%lld,%Q,%Q FROM product WHERE id=%lld",inv,qty,unit,total,total-profit,v[8],v[7],id);
}
void migrate(const char *dir){
 execute("BEGIN IMMEDIATE");if(scalar("SELECT count(*) FROM product")||scalar("SELECT count(*) FROM account")!=1)fail("Import requires a freshly initialized database.");
 execute("CREATE TEMP TABLE imported_user(name TEXT PRIMARY KEY)");
 for(file_no=0;file_no<4;file_no++){
  char path[4096];if(snprintf(path,sizeof path,"%s/%s",dir,files[file_no])>=(int)sizeof path)fail("Path too long.");
  FILE *f=fopen(path,"r");if(!f){if(file_no && errno==ENOENT)continue;fail("Cannot read legacy source.");}
  char *line=NULL;size_t cap=0;ssize_t n;line_no=0;const int cols[]={11,6,9,4};const char *headers[]={"INV2","MOV1","SALE1"};
  while((n=getline(&line,&cap,f))>=0){
   line_no++;if(n>2048 || memchr(line,0,(size_t)n))bad();line[strcspn(line,"\r\n")]=0;
   if(line_no==1 && file_no<3){if(strcmp(line,headers[file_no]))bad();continue;}
   char *v[12];int count=1;v[0]=line;for(char *p=line;*p;p++)if(*p=='|'){if(count>=cols[file_no])bad();*p=0;v[count++]=p+1;}
   if(count!=cols[file_no])bad();
   import_row(v);
  }
  if(ferror(f)||(!line_no && file_no<3))bad();
  free(line);fclose(f);
 }
 if(scalar("SELECT count(*) FROM product p WHERE qty!=(SELECT coalesce(sum(delta),0) FROM movement WHERE product=p.id) OR (SELECT coalesce(sum(qty),0) FROM sale WHERE product=p.id)!=-(SELECT coalesce(sum(delta),0) FROM movement WHERE product=p.id AND kind='Sale')"))fail("Legacy stock, movement and sale totals disagree. Correct a copy of the legacy files before importing.");
 execute("COMMIT");
}
