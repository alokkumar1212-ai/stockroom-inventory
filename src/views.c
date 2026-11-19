#include "app.h"
#include <stdlib.h>
#include <string.h>
void esc(const char *s){for(;s && *s;s++)switch(*s){case '&':fputs("&amp;",stdout);break;case '<':fputs("&lt;",stdout);break;case '>':fputs("&gt;",stdout);break;case '"':fputs("&quot;",stdout);break;case 39:fputs("&#39;",stdout);break;default:putchar(*s);}}
static void money(Amount n){if(n<0){putchar('-');n=-n;}printf("₹%lld.%02lld",n/100,n%100);}
void shell_top(const char *title){
 puts("<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><link rel=stylesheet href='/style.css'><script src='/app.js' defer></script><title>Stockroom • Inventory</title><body>");
 if(*actor){
 puts("<a class=skip href='#main'>Skip to content</a><aside><a class=brand href='?p=home'><b>▦</b> Stockroom</a><nav aria-label='Workspace'>");
 const char *nav[][3]={{"Overview","home","◫"},{"Products","products","▦"},{"Movements","activity","⇄"},{"Receipts","receipts","▤"},{"Reports","reports","▥"},{"Team","team","♧"}};
 for(int i=0;i<(admin?6:5);i++)printf("<a %s href='?p=%s'><span>%s</span>%s</a>",!strcmp(title,nav[i][0])?"aria-current=page":"",nav[i][1],nav[i][2],nav[i][0]);
 puts("</nav><footer>");esc(actor);printf("<small>%s</small><form method=post><input type=hidden name=action value=logout><input type=hidden name=csrf value='%s'><button class=quiet>Sign out</button></form></footer></aside>",admin?"Administrator":"Staff",csrf);
 }
 printf("<main id=main class='%s'>",*actor?"workspace":"entry");
 if(*actor){puts("<div class=crumb>Workspace <span>/</span> ");esc(title);puts("</div>");}
}
void end_page(void){puts("</main></body></html>");}
void login_page(const char *error){
 shell_top("Sign in");puts("<section class=login><div class=brand><b>▦</b> Stockroom</div><h1>Welcome back.</h1><p>Sign in to your inventory workspace.</p>");
 if(*error){printf("<p role=alert>");esc(error);puts("</p>");}
 puts("<form method=post><input type=hidden name=action value=login><label>Username<input name=name maxlength=64 autocomplete=username required autofocus></label><label>Password<input type=password name=password maxlength=128 autocomplete=current-password required></label><button>Sign in →</button></form></section>");end_page();
}
static void hidden(const char *k,const char *v){printf("<input type=hidden name='%s' value='",k);esc(v);puts("'>");}
static void form(const char *action){char token[65];random_hex(token);puts("<form method=post>");hidden("action",action);hidden("csrf",csrf);hidden("op",token);}
static void input(const char *label,const char *name,const char *value,const char *attrs){
 printf("<label>%s<input name='%s' value='",label,name);esc(value);printf("' %s required></label>",attrs);
}
static void heading(const char *title,const char *link,const char *label){
 puts("<header><h1>");esc(title);puts("</h1>");if(link)printf("<a class=button href='?p=%s'>%s</a>",link,label);puts("</header>");
}
static void state(Amount qty,Amount min){printf("<span class='pill %s'>%s</span>",!qty?"out":qty<=min?"low":"ok",!qty?"Out of stock":qty<=min?"Low stock":"In stock");}
static void blank(const char *s,int cols){printf("<tr><td class=empty colspan=%d>%s</td></tr>",cols,s);}
static void metric(const char *label,Amount value,int cash){printf("<div class=metric><small>%s</small><strong>",label);if(cash)money(value);else printf("%lld",value);puts("</strong></div>");}
static void overview(void){
 heading("Inventory overview","transact","Record transaction ↗");
 puts("<div class=metrics>");metric("Active products",scalar("SELECT count(*) FROM product WHERE archived=0"),0);
 metric("Units on hand",scalar("SELECT coalesce(sum(qty),0) FROM product WHERE archived=0"),0);
 metric("At or below minimum",scalar("SELECT count(*) FROM product WHERE archived=0 AND qty<=min"),0);
 metric("Units sold · 7 days",scalar("SELECT coalesce(sum(qty),0) FROM sale WHERE at>=datetime('now','-7 days')"),0);puts("</div>");
 puts("<div class=split><section class=panel><div class=sectionhead><h2>Stock activity</h2><small>Last 7 days · UTC</small></div><div class=legend><i></i>In <i></i>Out</div><div class=chart>");
 sqlite3_stmt *s=query("WITH RECURSIVE days(d) AS (VALUES(date('now','-6 days')) UNION ALL SELECT date(d,'+1 day') FROM days WHERE d<date('now')), totals AS (SELECT d,coalesce(sum(max(delta,0)),0) a,coalesce(sum(max(-delta,0)),0) b FROM days LEFT JOIN movement ON date(at)=d GROUP BY d) SELECT strftime('%%d/%%m',d),a,b,max(1,max(max(a,b)) OVER()) FROM totals ORDER BY d");
 while(row(s)){printf("<div class=chartrow><small>%s</small><div><meter aria-label='Units received' min=0 max='%lld' value='%lld'></meter><meter aria-label='Units sold' min=0 max='%lld' value='%lld'></meter></div><small>%lld / %lld</small></div>",str(s,0),val(s,3),val(s,1),val(s,3),val(s,2),val(s,1),val(s,2));}
 puts("</div></section><section class=panel><h2>Replenishment queue</h2>");
 s=query("SELECT id,name,qty,min FROM product WHERE archived=0 AND qty<=min ORDER BY qty=0 DESC,min-qty DESC,id LIMIT 6");int found=0;while(row(s)){found=1;printf("<a class=item href='?p=transact&amp;id=%lld'><div><strong>",val(s,0));esc(str(s,1));printf("</strong><small>%lld on hand / %lld minimum</small></div>",val(s,2),val(s,3));state(val(s,2),val(s,3));puts("</a>");}
 if(!found)puts("<p class=empty>No active products need replenishment.</p>");
 puts("</section></div><section class=panel><div class=sectionhead><h2>Latest movements</h2><a href='?p=activity'>View all →</a></div>");
}
static Amount offset(void){return *field("offset")?number(field("offset"),0,1000000000):0;}
static void pager(const char *page,Amount off,int more){
 printf("<form class=pager method=get>");hidden("p",page);hidden("q",field("q"));hidden("sort",field("sort"));
 if(off)printf("<button class=secondary name=offset value='%lld'>Previous</button>",off-25);
 if(more)printf("<button class=secondary name=offset value='%lld'>Next</button>",off+25);
 puts("</form>");
}
static void ledger(int receipts,int brief){
 puts("<div class=scroll><table><thead><tr><th>Date · UTC</th><th>Product</th>");puts(receipts?"<th>Customer</th><th>Total</th><th>Receipt</th>":"<th>Movement</th><th>Units</th><th>Reference</th><th>Recorded by</th>");puts("</tr></thead><tbody>");
 Amount off=brief?0:offset();sqlite3_stmt *s=receipts?query("SELECT at,name,customer,total,id FROM sale ORDER BY id DESC LIMIT 26 OFFSET %lld",off):query("SELECT m.at,p.name,m.kind,m.delta,m.reference,m.actor FROM movement m JOIN product p ON p.id=m.product ORDER BY m.id DESC LIMIT %d OFFSET %lld",brief?5:26,off);
 int i=0;while(row(s)){if(++i>25){sqlite3_finalize(s);break;}printf("<tr><td>");esc(str(s,0));printf("</td><td>");esc(str(s,1));printf("</td><td>");esc(str(s,2));printf("</td><td>");
 if(receipts){money(val(s,3));printf("</td><td><a href='?p=receipt&amp;id=%lld'>INV-%06lld</a>",val(s,4),val(s,4));}
 else{printf("%+lld</td><td>",val(s,3));esc(str(s,4));puts("</td><td>");esc(str(s,5));}puts("</td></tr>");}
 if(!i)blank(receipts?"Receipts will appear after your first sale.":"No stock movements recorded yet.",6);
 puts("</tbody></table></div>");if(!brief)pager(receipts?"receipts":"activity",off,i>25);
}
static void products(void){
 heading("Products","new","＋ Add product");puts("<section class=panel><form class=tools method=get><input type=hidden name=p value=products><input aria-label='Search catalogue' name=q placeholder='Search ID, product or category' value='");esc(field("q"));printf("'><select name=sort aria-label='Sort products'><option value=name>Name A–Z</option><option value=price %s>Price · low to high</option></select><button class=secondary>Search</button></form>",!strcmp(field("sort"),"price")?"selected":"");
 puts("<div class=scroll><table><tr><th>ID</th><th>Product</th><th>Category</th><th>On hand</th><th>Unit price</th><th>Status</th><th></th></tr>");
 Amount off=offset();const char *q=field("q");sqlite3_stmt *s=query("SELECT id,name,category,qty,sell,min FROM product WHERE archived=0 AND (instr(lower(name),lower(%Q)) OR instr(lower(category),lower(%Q)) OR cast(id as text)=%Q) ORDER BY %s,id LIMIT 26 OFFSET %lld",q,q,q,!strcmp(field("sort"),"price")?"sell":"name COLLATE NOCASE",off);
 int i=0;while(row(s)){if(++i>25){sqlite3_finalize(s);break;}printf("<tr><td>#%lld</td><td><a href='?p=edit&amp;id=%lld'>",val(s,0),val(s,0));esc(str(s,1));printf("</a></td><td>");esc(str(s,2));printf("</td><td>%lld</td><td>",val(s,3));money(val(s,4));puts("</td><td>");state(val(s,3),val(s,5));printf("</td><td><a href='?p=transact&amp;id=%lld'>Record →</a></td></tr>",val(s,0));}
 if(!i)blank(*q?"No matching products.":"Your catalogue is empty. Add the first product above.",7);
 puts("</table></div>");pager("products",off,i>25);puts("</section>");
}
static void product_form(int edit){
 sqlite3_stmt *s=NULL;if(edit){s=query("SELECT id,name,category,supplier,sell,min,lead FROM product WHERE id=%lld AND archived=0",number(field("id"),1,2147483647));if(!row(s)){puts("<p>Product unavailable.</p>");return;}}
 heading(edit?"Edit product":"Add product",NULL,NULL);puts("<section class='panel narrow'>");form(edit?"edit":"new");
 if(edit)hidden("id",str(s,0));else input("Product ID","id","","type=number min=1 max=2147483647");
 input("Product name","name",edit?str(s,1):"","maxlength=120");input("Category","category",edit?str(s,2):"","maxlength=120");input("Supplier","supplier",edit?str(s,3):"","maxlength=120");
 char price[48]="";if(edit)snprintf(price,sizeof price,"%lld.%02lld",val(s,4)/100,val(s,4)%100);
 input("Selling price (₹)","sell",price,"type=number min=0 step=.01");if(!edit){input("Opening units","qty","0","type=number min=0");input("Opening unit cost (₹)","cost","","type=number min=0 step=.01");}
 input("Minimum stock","min",edit?str(s,5):"0","type=number min=0");input("Supplier lead time (days)","lead",edit?str(s,6):"0","type=number min=0 max=3650");puts("<button>Save product</button></form>");
 if(edit && admin){form("archive");hidden("id",str(s,0));puts("<button class=secondary>Archive zero-stock product</button></form>");}
 if(s)sqlite3_finalize(s);
 puts("</section>");
}
static void transaction(void){
 heading("Record transaction",NULL,NULL);puts("<section class='panel narrow'>");
 if(!scalar("SELECT count(*) FROM product WHERE archived=0")){puts("<p>Add a product before recording stock movements.</p><a class=button href='?p=new'>Add product</a></section>");return;}
 form("transaction");puts("<label>Transaction type<select id=kind name=kind><option value=purchase>Purchase · receive stock</option><option value=sale>Sale · issue receipt</option></select></label><label>Product<select required name=id>");
 sqlite3_stmt *s=query("SELECT id,name,qty FROM product WHERE archived=0 ORDER BY name,id");while(row(s)){printf("<option value='%lld' %s>",val(s,0),!strcmp(field("id"),str(s,0))?"selected":"");esc(str(s,1));printf(" · #%lld · %lld available</option>",val(s,0),val(s,2));}puts("</select></label>");
 input("Quantity","qty","1","type=number min=1");puts("<div id=cost>");input("Purchase unit cost (₹)","cost","0","type=number min=0 step=.01");puts("</div>");input("Supplier reference / customer name","reference","","maxlength=120");puts("<button>Record transaction</button></form></section>");
}
static void receipt(void){
 sqlite3_stmt *s=query("SELECT id,at,name,qty,price,total,customer FROM sale WHERE id=%lld",number(field("id"),1,2147483647));if(!row(s)){puts("<p>Receipt unavailable.</p>");return;}
 heading("Sale receipt",NULL,NULL);printf("<section class='panel receipt'><strong>Stockroom</strong><h2>INV-%06lld</h2><p>",val(s,0));esc(str(s,1));puts(" UTC</p><small>Customer</small><h3>");esc(str(s,6));puts("</h3><hr><div class=item><div><strong>");esc(str(s,2));printf("</strong><small>%lld × ",val(s,3));money(val(s,4));puts("</small></div>");money(val(s,5));puts("</div><div class=item><strong>Total</strong><strong>");money(val(s,5));puts("</strong></div><button id=print>Print receipt</button></section>");sqlite3_finalize(s);
}
static void audit_panel(void){
 if(!admin)return;
 puts("<section class=panel><h2>Ledger reconciliation</h2>");
 Amount errors=scalar("SELECT count(*) FROM product p WHERE qty!=(SELECT coalesce(sum(delta),0) FROM movement WHERE product=p.id) OR (SELECT coalesce(sum(qty),0) FROM sale WHERE product=p.id)!=-(SELECT coalesce(sum(delta),0) FROM movement WHERE product=p.id AND kind='Sale')");
 if(errors)printf("<p>%lld product balances need investigation.</p>",errors);
 else puts("<p>Stock balances and recorded sale quantities match the movement ledger.</p>");
 puts("</section>");
}
static void idle_stock(void){
 Amount days=*field("days")?number(field("days"),1,3650):90;
 puts("<section class=panel><h2>Slow-moving stock</h2><form class=tools method=get>");hidden("p","reports");
 printf("<label>Days without a sale<input type=number name=days min=1 max=3650 value='%lld' required></label><button class=secondary>Check stock</button></form>",days);
 sqlite3_stmt *s=query("SELECT p.name,p.qty,max(s.at) FROM product p LEFT JOIN sale s ON s.product=p.id WHERE p.archived=0 AND p.qty>0 GROUP BY p.id HAVING max(s.at) IS NULL OR max(s.at)<datetime('now','-%lld days') ORDER BY max(s.at),p.name LIMIT 26",days);
 int i=0;while(row(s)){if(++i>25){sqlite3_finalize(s);puts("<small>Showing the first 25 products in order of oldest sale.</small>");break;}
 puts("<div class=item><strong>");esc(str(s,0));printf("</strong><small>%lld units · ",val(s,1));esc(str(s,2)?str(s,2):"No recorded sale");puts("</small></div>");}
 if(!i)puts("<p>No on-hand products meet this condition.</p>");
 puts("</section>");
}
static void reports(void){
 heading("Reports",NULL,NULL);
 if(admin){puts("<div class=metrics>");metric("Sales revenue",scalar("SELECT coalesce(sum(total),0) FROM sale"),1);metric("Cost of goods sold",scalar("SELECT coalesce(sum(cogs),0) FROM sale"),1);metric("Gross profit",scalar("SELECT coalesce(sum(total-cogs),0) FROM sale"),1);metric("Inventory value",scalar("SELECT coalesce(sum(value),0) FROM product"),1);puts("</div>");}
 puts("<section class=panel><h2>Reorder &amp; slow-moving stock</h2><p>Reorder point uses the last 30 days of sales, supplier lead time and minimum stock. Products with no sales in the window use minimum stock.</p><div class=scroll><table><tr><th>Product</th><th>Available</th><th>Reorder point</th><th>Last sale · UTC</th><th>Action</th></tr>");
 sqlite3_stmt *s=query("SELECT p.name,p.qty,p.min+cast((coalesce(sum(CASE WHEN s.at>=datetime('now','-30 days') THEN s.qty END),0)*1.0*p.lead+29)/30 AS integer),max(s.at) FROM product p LEFT JOIN sale s ON s.product=p.id WHERE p.archived=0 GROUP BY p.id ORDER BY p.name LIMIT 26 OFFSET %lld",offset());int i=0;while(row(s)){if(++i>25){sqlite3_finalize(s);break;}puts("<tr><td>");esc(str(s,0));printf("</td><td>%lld</td><td>%lld</td><td>",val(s,1),val(s,2));esc(str(s,3)?str(s,3):"No recorded sale");printf("</td><td>%s</td></tr>",val(s,1)<=val(s,2)?"Reorder":"Hold");}if(!i)blank("Add a product to see stock planning.",5);puts("</table></div>");pager("reports",offset(),i>25);puts("</section><section class='panel narrow'><h2>What-if stock calculator</h2><form method=get>");hidden("p","reports");input("Product ID","id",field("id"),"type=number min=1");input("Expected sales","sold",field("sold"),"type=number min=0");input("Incoming units","incoming",field("incoming"),"type=number min=0");puts("<button class=secondary>Calculate</button></form>");
 if(*field("id")){s=query("SELECT qty FROM product WHERE id=%lld AND archived=0",number(field("id"),1,2147483647));if(row(s)){Amount n=val(s,0)-number(field("sold"),0,2147483647)+number(field("incoming"),0,2147483647);printf("<p>Projected balance: <strong>%lld units</strong></p>",n);sqlite3_finalize(s);}else puts("<p>Product unavailable.</p>");}puts("</section>");
}
void pages(const char *p){
 const char *title=!strcmp(p,"products")?"Products":!strcmp(p,"activity")?"Movements":!strcmp(p,"receipts")?"Receipts":!strcmp(p,"reports")?"Reports":!strcmp(p,"team")?"Team":!strcmp(p,"transact")?"Transactions":!strcmp(p,"receipt")?"Receipt":(!strcmp(p,"edit")||!strcmp(p,"new"))?"Catalogue":"Overview";shell_top(title);
 if(!*p||!strcmp(p,"home")){overview();ledger(0,1);puts("</section>");}
 else if(!strcmp(p,"products"))products();
 else if(!strcmp(p,"new")||!strcmp(p,"edit"))product_form(!strcmp(p,"edit"));
 else if(!strcmp(p,"transact"))transaction();
 else if(!strcmp(p,"receipt"))receipt();
 else if(!strcmp(p,"activity")||!strcmp(p,"receipts")){heading(title,"transact","Record transaction ↗");puts("<section class=panel>");ledger(!strcmp(p,"receipts"),0);puts("</section>");}
 else if(!strcmp(p,"reports")){reports();idle_stock();audit_panel();}
 else if(!strcmp(p,"team")&&admin){heading("Team",NULL,NULL);puts("<section class='panel narrow'><h2>Workspace access</h2>");sqlite3_stmt *s=query("SELECT name,admin FROM account ORDER BY name");while(row(s)){puts("<div class=item>");esc(str(s,0));printf("<small>%s</small></div>",val(s,1)?"Administrator":"Staff");}puts("<h2>Add team member</h2>");form("user");input("Username","name","","maxlength=64");input("Password","password","","type=password minlength=12 maxlength=128 autocomplete=new-password");puts("<label>Role<select name=role><option>staff</option><option>admin</option></select></label><button>Create account</button></form></section>");}
 else puts("<p>This page is unavailable.</p>");
 end_page();
}
