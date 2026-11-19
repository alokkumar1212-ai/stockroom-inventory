#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <openssl/crypto.h>
/* Each command gets an independent connection and transaction, just like a CGI request. */
static char slots[18][256];
static int n;
static const char *ask(const char *key,const char *label){
 if(n>=18)fail("Too many input fields.");
 char *v=slots[n++];printf("%s: ",label);fflush(stdout);
 if(!fgets(v,256,stdin))exit(0);
 if(!strchr(v,'\n'))fail("Input too long.");
 v[strcspn(v,"\r\n")]=0;set_field(key,v);return v;
}
static void show(const char *sql,int cols){
 sqlite3_stmt *s=query("%s",sql);while(row(s)){for(int j=0;j<cols;j++){if(j)fputs(" | ",stdout);fputs(str(s,j)?str(s,j):"—",stdout);}putchar('\n');}
}
static void command(char c){
 open_store(0);
 if(c=='2'){ask("q","Search by name/category/ID (blank for all)");sqlite3_stmt *s=query("SELECT id,name,category,qty,sell FROM product WHERE archived=0 AND (instr(lower(name),lower(%Q)) OR instr(lower(category),lower(%Q)) OR cast(id as text)=%Q) ORDER BY sell,id",field("q"),field("q"),field("q"));puts("ID | Product | Category | Units | Selling price");while(row(s))printf("%lld | %s | %s | %lld | %lld.%02lld\n",val(s,0),str(s,1),str(s,2),val(s,3),val(s,4)/100,val(s,4)%100);return;}
 if(c=='7'){puts("UTC | ID | Movement | Units | Reference | User");show("SELECT at,product,kind,delta,reference,actor FROM movement ORDER BY id DESC",6);return;}
 if(c=='8'){puts("Invoice | Customer | Product | Units | Total (paise)");show("SELECT id,customer,name,qty,total FROM sale ORDER BY id DESC",5);return;}
 if(c=='9'){puts("ID | Product | Stock | Minimum");show("SELECT id,name,qty,min FROM product WHERE archived=0 AND qty<=min ORDER BY qty",4);if(admin){puts("Revenue | COGS | Gross profit (paise)");show("SELECT coalesce(sum(total),0),coalesce(sum(cogs),0),coalesce(sum(total-cogs),0) FROM sale",3);}return;}
 const char *action=NULL;
 switch(c){case '1':action="new";break;case '3':action="edit";break;case '4':action="purchase";break;case '5':action="sale";break;case '6':action="archive";break;default:puts("Choose a listed option.");return;}
 ask("id","Product ID");
 if(c=='1'||c=='3'){ask("name","Product name");ask("category","Category");ask("supplier","Supplier");ask("sell","Selling price");ask("min","Minimum stock");ask("lead","Lead time in days");}
 if(c=='1'||c=='4'||c=='5')ask("qty","Quantity");
 if(c=='1'||c=='4')ask("cost","Unit cost");
 if(c=='4'||c=='5')ask("reference",c=='4'?"Purchase reference":"Customer");
 char op[65];random_hex(op);set_field("op",op);execute("BEGIN IMMEDIATE; INSERT INTO operation(token,user) VALUES(%Q,%Q)",op,actor);
 change(action);Amount receipt=scalar("SELECT receipt FROM operation WHERE token=%Q",op);execute("COMMIT");
 if(receipt)printf("Sale recorded. Receipt INV-%06lld\n",receipt);else puts("Saved.");
}
void terminal(void){
 open_store(0);char name[80],password[132];printf("Username: ");fflush(stdout);if(!fgets(name,sizeof name,stdin))return;name[strcspn(name,"\r\n")]=0;read_pass(password,sizeof password);
 if(strlen(name)>64 || !verify(name,password))fail("Sign-in failed.");
 OPENSSL_cleanse(password,sizeof password);strcpy(actor,name);admin=(int)scalar("SELECT admin FROM account WHERE name=%Q",actor);sqlite3_close(db);db=NULL;
 for(;;){
  puts("\n1 Add  2 Search/list by price  3 Edit  4 Purchase  5 Sale  6 Archive\n7 Movement history  8 Receipts  9 Low stock and totals  0 Exit");
  char choice[40];if(!fgets(choice,sizeof choice,stdin)||choice[0]=='0')return;
  fflush(NULL);pid_t pid=fork();if(pid<0)fail("Cannot start command.");if(!pid){command(choice[0]);exit(0);}int status;if(waitpid(pid,&status,0)<0)fail("Command interrupted.");
 }
}
