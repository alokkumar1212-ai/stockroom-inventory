"""Integration tests invoke the actual C binary through its CGI and operator interfaces."""
import concurrent.futures
import os
import re
import sqlite3
import subprocess
import tempfile
import unittest
from pathlib import Path
from urllib.parse import urlencode
from secrets import token_hex

BIN = Path(__file__).resolve().parents[1] / "inventory"


class Inventory(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "stock.sqlite"
        self.env = dict(os.environ, INVENTORY_DB=str(self.path))
        self.cli("init", "owner", data="test-password-123\n")
        self.cookie = self.login()
        self.csrf = self.read("select csrf from session")[0][0]

    def cli(self, *args, data="", ok=True):
        p = subprocess.run([BIN, *args], input=data, env=self.env,
                           text=True, capture_output=True, timeout=10)
        if ok:
            self.assertEqual(p.returncode, 0, p.stderr)
        return p

    def read(self, sql):
        with sqlite3.connect(self.path) as db:
            return db.execute(sql).fetchall()

    def request(self, form=None, query="", cookie=None):
        data = urlencode(form or {})
        env = dict(self.env, GATEWAY_INTERFACE="CGI/1.1", REQUEST_METHOD="POST" if form else "GET",
                   CONTENT_LENGTH=str(len(data)), CONTENT_TYPE="application/x-www-form-urlencoded",
                   QUERY_STRING=query, HTTP_COOKIE=cookie if cookie is not None else self.cookie)
        return subprocess.run([BIN], input=data, text=True, env=env, capture_output=True, timeout=15).stdout

    def login(self, name="owner", password="test-password-123"):
        response = self.request(dict(action="login", name=name, password=password), cookie="")
        return re.search(r"Set-Cookie: (inventory=[a-f0-9]+)", response)[1]

    def mutate(self, action, **kwargs):
        form = dict(action=action, csrf=self.csrf, op=token_hex(32))
        form.update(kwargs)
        return self.request(form)

    def add(self, qty=1):
        r = self.mutate("new", id=1, name="Notebook", category="Stationery",
                        supplier="Local", sell="49.99", cost="15.30", qty=qty, min=2, lead=3)
        self.assertIn("303", r)

    def sell(self, **kwargs):
        return self.mutate("transaction", kind="sale", id=1, qty=1, reference="Walk-in", **kwargs)

    def test_unsold_restart_and_views(self):
        self.add()
        for page in ["home", "products", "reports", "edit&id=1", "team", "transact", "activity"]:
            page_html = self.request(query="p=" + page)
            self.assertIn("</main>", page_html)
            self.assertNotIn("Request interrupted", page_html)
        self.assertIn("Notebook", self.request(query="p=products"))
        self.assertIn("already in use", self.mutate("new", id=1, name="X", category="X", supplier="X",
                      sell="1", cost="1", qty=0, min=0, lead=0))

    def test_two_cashiers_last_unit(self):
        self.add()
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            results = list(pool.map(lambda _: self.sell(), range(2)))
        self.assertEqual(sum("303" in r for r in results), 1)
        self.assertTrue(any("Insufficient stock" in r for r in results))
        self.assertEqual(self.read("select qty from product"), [(0,)])
        self.assertEqual(self.read("select count(*) from sale"), [(1,)])

    def test_retry_and_weighted_cost(self):
        self.add()
        self.mutate("transaction", kind="purchase", id=1, cost="20.10", qty=1, reference="PO-01")
        op = token_hex(32)
        for _ in range(2):
            self.assertIn("p=receipt&id=1", self.sell(op=op))
        self.assertEqual(self.read("select total,cogs from sale"), [(4999,1770)])
        self.assertEqual(self.read("select qty,value from product"), [(1,1770)])
        receipt = self.request(query="p=receipt&id=1")
        self.assertIn("₹49.99", receipt)
        self.assertNotIn("profit", receipt.lower())

    def test_interrupted_sale_rolls_back(self):
        self.add()
        with sqlite3.connect(self.path) as db:
            db.execute("CREATE TRIGGER injected BEFORE INSERT ON movement WHEN NEW.kind='Sale' BEGIN SELECT RAISE(ABORT,'interruption'); END")
        self.assertNotIn("303", self.sell())
        self.assertEqual(self.read("select qty from product"), [(1,)])
        self.assertEqual(self.read("select count(*) from sale"), [(0,)])
        self.assertEqual(self.read("select count(*) from movement"), [(1,)])

    def test_snapshot_and_repeat_restore(self):
        self.add(3)
        self.sell()
        backup = str(self.path.with_suffix(".bak"))
        self.cli("backup", backup)
        self.sell()
        for _ in range(2):
            self.cli("restore", backup)
            self.assertEqual(self.read("select qty from product"), [(2,)])
            self.assertEqual(self.read("select sum(total) from sale"), [(4999,)])
            self.assertEqual(self.read("select sum(delta) from movement"), [(2,)])
            self.assertEqual(self.read("select count(*) from session"), [(0,)])

    def test_access_and_archive(self):
        self.add()
        self.assertIn("expired", self.sell(csrf="bad"))
        self.assertIn("303", self.mutate("user", name="staff", password="another-password", role="staff"))
        self.cookie = self.login("staff", "another-password")
        self.csrf = self.read("select csrf from session where user='staff'")[0][0]
        self.sell()
        self.assertNotIn("303", self.mutate("archive", id=1))
        self.assertNotIn("Gross profit", self.request(query="p=reports"))
        self.assertNotIn("303", self.mutate("user", name="bad", password="pw", role="admin"))

    def test_legacy_import(self):
        folder = Path(self.temp.name)
        (folder / "inventory.dat").write_text("INV2\n7|Pen|Office|2|1.2000|2.0000|1|1|ACME|Available|\n")
        (folder / "movements.dat").write_text("MOV1\n7|2|OPENING|1.2000|2026-01-01 00:00:00|opening stock\n")
        self.cli("import", str(folder))
        self.assertEqual(self.read("select id,qty,value from product"), [(7,2,240)])

    def test_import_rejects_partial_data(self):
        folder = Path(self.temp.name)
        (folder / "inventory.dat").write_text("INV2\n7|Pen|Office|2|1.2000|2.0000|1|1|ACME|Available|\n")
        self.assertNotEqual(self.cli("import", str(folder), ok=False).returncode, 0)
        self.assertEqual(self.read("select count(*) from product"), [(0,)])

    def test_legacy_sale_and_password_reset(self):
        folder = Path(self.temp.name)
        (folder / "inventory.dat").write_text("INV2\n7|Pen|Office|1|1.2000|2.0000|1|1|ACME|Low Stock|2026-01-02 00:00:00\n")
        (folder / "movements.dat").write_text("MOV1\n7|2|OPENING|1.2000|2026-01-01 00:00:00|opening stock\n7|-1|SALE|2.0000|2026-01-02 00:00:00|Customer\n")
        (folder / "sales.dat").write_text("SALE1\n41|7|1|1.2000|2.0000|2.0000|0.8000|2026-01-02 00:00:00|Customer\n")
        (folder / "users.dat").write_text("member|old-password|2|0\n")
        self.cli("import", str(folder))
        self.assertEqual(self.read("select id,total,cogs from sale"), [(41,200,120)])
        self.cli("reset", "member", data="new-member-password\n")
        self.assertTrue(self.login("member","new-member-password"))

    def test_terminal_web_interoperability(self):
        self.add()
        p = self.cli(data="owner\ntest-password-123\n5\n1\n1\nTerminal buyer\n0\n")
        self.assertIn("INV-000001", p.stdout)
        self.assertIn("Terminal buyer", self.request(query="p=receipt&id=1"))

    def test_archived_identity_and_invalid_amounts(self):
        self.add()
        self.assertNotIn("303",self.mutate("transaction",kind="purchase",id=1,qty=3,cost="nan",reference="X"))
        self.sell()
        self.assertIn("303",self.mutate("archive", id=1))
        self.assertIn("already in use", self.mutate("new", id=1, name="Other",category="Office",supplier="X",qty=1,cost=1,sell=1,min=1,lead=1))
        self.assertIn("Notebook",self.request(query="p=receipt&id=1"))
