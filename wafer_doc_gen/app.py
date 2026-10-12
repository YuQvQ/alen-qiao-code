# -*- coding: utf-8 -*-
"""
晶圆探针配方文档生成器 - 向导版 (exe)  现代化UI
PDF 中 Y 偏移等数值显式带正负号。
"""
import os, json, tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.colors import HexColor, black
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Preformatted,
                                Image as RLImage, Table, TableStyle)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

try:
    from PIL import Image as PILImage
    HAVE_PIL = True
except Exception:
    HAVE_PIL = False

C_BG = "#eef1f5"; C_HEADER = "#16324f"; C_ACCENT = "#2f80ed"; C_DONE = "#27ae60"
C_GRAY = "#9aa5b1"; C_CARD = "#ffffff"; C_BORDER = "#dde3ea"; C_TEXT = "#1f2d3d"
C_SUB = "#5b6b7c"; C_WARN = "#e74c3c"

def reg_font():
    for p in [r"C:\Windows\Fonts\msyh.ttc", r"C:\Windows\Fonts\simsun.ttc"]:
        if os.path.exists(p):
            pdfmetrics.registerFont(TTFont("CJK", p, subfontIndex=0)); return "CJK"
    return "Helvetica"
CJK = reg_font()
PRIMARY = HexColor("#1a365d"); ACCENT = HexColor("#2b6cb0")
def st(n, sz, c=black, a=0, sa=6, ld=None):
    return ParagraphStyle(n, fontName=CJK, fontSize=sz, leading=ld or sz*1.5,
                          textColor=c, alignment=a, spaceAfter=sa, wordWrap="CJK")
S_TITLE=st("t",22,PRIMARY,1,4); S_H1=st("h1",15,PRIMARY,0,8); S_H2=st("h2",12,ACCENT,0,6)
S_BODY=st("b",10.5); S_BULLET=st("bu",10.5,black,0,4); S_LBL=st("l",10.5,HexColor("#4a5568"))
S_CODE=ParagraphStyle("c",fontName=CJK,fontSize=8.5,leading=11,textColor=HexColor("#1a202c"),wordWrap="CJK")
PROBE_RULE = "探针卡右侧第一个针扎的是Mapping图右侧第一列晶圆"

# 数值显式带正负号: 0.705 -> +0.705, -0.705 -> -0.705, 0 -> 0
def fmt_signed(v):
    try:
        v = float(v)
    except Exception:
        return str(v)
    if abs(v) < 1e-9:
        return "0"
    s = ("%+.3f" % v).rstrip("0").rstrip(".")
    return s

def calc_offsets(die_w, base, rule, direction):
    n = 3 if rule == "1/3" else 4
    step = die_w / n
    if direction == "Y-":
        g1 = base - step; g2 = base - die_w + step
    else:
        g1 = base + step; g2 = base + die_w - step
    return round(g1, 3), round(g2, 3)

def num(v, label):
    v = (v or "").strip()
    if v == "": raise ValueError(f"「{label}」为空，请填写一个数字")
    try: return float(v)
    except ValueError: raise ValueError(f"「{label}」的值 “{v}” 不是有效数字")
def integ(v, label):
    v = (v or "").strip()
    if v == "": raise ValueError(f"「{label}」为空，请填写一个整数")
    try: return int(float(v))
    except ValueError: raise ValueError(f"「{label}」的值 “{v}” 不是有效整数")

def fit_image(path, max_w=80*mm, max_h=110*mm):
    if not HAVE_PIL: return RLImage(path, width=max_w)
    im = PILImage.open(path); w, h = im.size
    ratio = min(max_w / w, max_h / h, 1.0)
    return RLImage(path, width=w*ratio, height=h*ratio)

def side_table(left_flowables, img_path, left_w=70*mm, right_w=100*mm, img_max_h=115*mm):
    if img_path and os.path.exists(img_path):
        try: right = fit_image(img_path, max_w=right_w, max_h=img_max_h)
        except Exception: right = Paragraph("(图片加载失败)", S_LBL)
    else: right = Paragraph("", S_BODY)
    t = Table([[left_flowables, right]], colWidths=[left_w, right_w])
    t.setStyle(TableStyle([("VALIGN",(0,0),(-1,-1),"TOP"),("LEFTPADDING",(0,0),(-1,-1),0),
                           ("RIGHTPADDING",(0,0),(-1,-1),4),("TOPPADDING",(0,0),(-1,-1),0),
                           ("BOTTOMPADDING",(0,0),(-1,-1),0)]))
    return t

def collect(E, rows_text, vm_text, probe_img, map_img, pos_img, wafer_raw_json, nround):
    g = lambda k: E[k].get().strip()
    die_w = num(g("die_w"), "DIE 宽"); die_h = num(g("die_h"), "DIE 高")
    sc_w = num(g("sc_w"), "切割道宽"); sc_h = num(g("sc_h"), "切割道高")
    rows = [r for r in rows_text.splitlines() if r.strip()]
    try: vm = json.loads(vm_text) if vm_text.strip() else {}
    except Exception as ex: raise ValueError(f"配方参数 JSON 解析失败: {ex}")
    rounds = []
    for i in range(nround):
        rounds.append({"label": f"第{i+1}扎针",
                       "A": integ(g(f"A{i}"), f"第{i+1}次 A"),
                       "B": integ(g(f"B{i}"), f"第{i+1}次 B"),
                       "C": integ(g(f"C{i}"), f"第{i+1}次 C")})
    cfg = {"product_name": g("name"), "doc_suffix": f"({nround})", "id": g("id"),
        "die": {"height": die_h, "width": die_w},
        "scribe_line": {"width": sc_w, "height": sc_h, "material": g("material"), "has_test_key": True},
        "thickness": num(g("thick"), "厚度"),
        "wafer_info": {"name": g("name"), "wafer_id": g("wafer_id"), "version_id": 2147483647,
            "metadata": {"note":"","created_by":"","manufacturer":g("manu"),"process_node":g("node")},
            "wafer_size": integ(g("wafer_size"), "晶圆尺寸"), "wafer_diameter": num(g("wafer_dia"), "晶圆直径"),
            "gdpw": integ(g("gdpw"), "gdpw"), "rows": rows},
        "probe_card": {"rule": PROBE_RULE, "extra_note": g("pc_extra")},
        "puncture": {"rule_desc": g("pun_desc"), "rounds": rounds}, "groups": [],
        "die0_offset": {"x": num(g("d0x"), "DIE0 X"), "y": num(g("d0y"), "DIE0 Y"), "remark": g("d0rmk")},
        "vm_params": vm, "probe_img": probe_img, "map_img": map_img, "pos_img": pos_img,
        "wafer_raw_json": wafer_raw_json}
    for i in range(nround):
        y = num(g(f"gy{i}"), f"分组{i+1} Y偏移")
        cfg["groups"].append({"name": f"分组{i+1}", "center_offset": g(f"gco{i}"),
                              "chuck_y": y, "edge_y": y, "mark_y": y,
                              "A": rounds[i]["A"], "B": rounds[i]["B"], "C": rounds[i]["C"]})
    return cfg

def _wafer_obj(cfg):
    wi = cfg["wafer_info"]; die = cfg["die"]; sc = cfg["scribe_line"]
    return {"name": wi["name"], "wafer_id": wi["wafer_id"], "version_id": wi["version_id"], "metadata": wi["metadata"],
            "wafer_info": {"product_id": wi["name"], "wafer_size": wi["wafer_size"], "wafer_diameter": wi["wafer_diameter"],
                "dimensions": {"h_x": die["height"], "w_y": die["width"], "gdpw": wi["gdpw"], "rows": wi["rows"]},
                "scribe_line": {"width": sc["width"], "material": sc["material"], "has_test_key": sc["has_test_key"]},
                "thickness": cfg["thickness"]}, "id": cfg["id"]}

def _vm_obj(cfg):
    p = dict(cfg["vm_params"]); r = cfg["puncture"]["rounds"][0]
    p["die_idxs"] = [r["A"], r["B"], r["C"]]
    return {"vm_params": p}

def build_story(cfg):
    s = []; P = lambda t, st: s.append(Paragraph(t, st))
    P(f"{cfg['product_name']}{cfg['doc_suffix']}", S_TITLE)
    P("晶圆信息 结构提供", S_H1)
    die = cfg["die"]; sc = cfg["scribe_line"]
    P("● DIE宽高", S_BULLET); P(f"高: {die['height']}", S_BODY); P(f"宽: {die['width']}", S_BODY)
    P("● 切割道宽高", S_BULLET); P(f"宽: {sc['width']}", S_BODY); P(f"高: {sc['height']}", S_BODY)
    pc = [Paragraph("● 探卡图", S_BULLET), Paragraph(f"规则: {PROBE_RULE}", S_BODY)]
    if cfg["probe_card"].get("extra_note"): pc.append(Paragraph(cfg["probe_card"]["extra_note"], S_BODY))
    s.append(side_table(pc, cfg.get("probe_img"))); s.append(Spacer(1, 4))
    P("晶圆信息", S_H2)
    raw = (cfg.get("wafer_raw_json") or "").strip()
    s.append(Preformatted(raw if raw else json.dumps(_wafer_obj(cfg), ensure_ascii=False, indent=2), S_CODE))
    s.append(Spacer(1, 6))
    P("● 探针晶圆映射图 (多次扎针)(结构提供)", S_BULLET)
    P("最终输出", S_H2)
    s.append(side_table([Paragraph("a. 探针拍照位置信息", S_BODY)], cfg.get("pos_img"))); s.append(Spacer(1, 4))
    b_left = [Paragraph("b. 晶圆拍照点序号", S_BODY), Paragraph("基于探针晶圆映射图", S_LBL)]
    for r in cfg["puncture"]["rounds"]:
        b_left.append(Paragraph(r["label"], S_BODY))
        b_left.append(Paragraph(f"A点序号 {r['A']}<br/>B点序号 {r['B']}<br/>C点序号 {r['C']}", S_BODY))
    if cfg["puncture"].get("rule_desc"): b_left.append(Paragraph(cfg["puncture"]["rule_desc"], S_LBL))
    s.append(side_table(b_left, cfg.get("map_img"))); s.append(Spacer(1, 4))
    P("c. 多次扎针Chuck接料位偏移, 圆边识别偏移, 晶圆Mark偏移", S_BODY)
    for g in cfg["groups"]:
        h = g["name"] + (f"（离晶圆圆心偏移 {g['center_offset']}）" if g.get("center_offset") else "")
        P(h, S_H2)
        Y = fmt_signed(g["chuck_y"])
        P(f"1. 接料位 Chuck接料位偏移 Y {Y}", S_BODY)
        P(f"2. 晶圆拍照点新增分组, 晶圆拍照点点位不变, 序号改成A点序号 {g['A']} B点序号 {g['B']} C点序号 {g['C']}", S_BODY)
        P(f"3. 圆边拍照点新增分组 圆边A,B,C拍照点 Y {fmt_signed(g['edge_y'])}", S_BODY)
        P(f"4. 晶圆Mark新增分组, 晶圆Mark偏移 Y {fmt_signed(g['mark_y'])}", S_BODY)
    P("d. 探针拍照点数据(探针纠偏用", S_BODY); P("参考f.配方参数的 probe_point_set", S_LBL)
    P("e. DIE0相对于探卡中心偏移量", S_BODY)
    d0 = cfg["die0_offset"]
    P(f"偏差值X,Y [{fmt_signed(d0['x'])},{fmt_signed(d0['y'])}] {d0.get('remark','')}", S_BODY)
    P("f. 配方参数", S_H2)
    s.append(Preformatted(json.dumps(_vm_obj(cfg), ensure_ascii=False, indent=2), S_CODE))
    return s

FONT_BASE = ("微软雅黑", 10); FONT_BOLD = ("微软雅黑", 10, "bold"); FONT_H = ("微软雅黑", 13, "bold")

class App:
    def __init__(self, root):
        self.root = root
        root.title("晶圆探针配方文档生成器")
        root.geometry("1040x740"); root.minsize(940, 660)
        root.configure(bg=C_BG)
        self.entries = {}
        self.probe_img = ""; self.map_img = ""; self.pos_img = ""; self.wafer_raw_json = ""
        self.nround = tk.IntVar(value=2)
        self.direct = tk.StringVar(value="往左 (Y偏移为负, 后续序号 +1)")
        self._build_header()
        self.body = tk.Frame(root, bg=C_BG); self.body.pack(fill="both", expand=True, padx=18, pady=(0,8))
        self.card = tk.Frame(self.body, bg=C_CARD, highlightbackground=C_BORDER, highlightthickness=1)
        self.card.pack(fill="both", expand=True)
        self._build_footer()
        self.steps = self._define_steps()
        self.total = len(self.steps); self.cur = 0
        self.pages = []
        for st_ in self.steps:
            f = tk.Frame(self.card, bg=C_CARD); st_["build"](f); self.pages.append(f)
        self.show(0)

    def _build_header(self):
        h = tk.Frame(self.root, bg=C_HEADER, height=96); h.pack(fill="x"); h.pack_propagate(False)
        tk.Label(h, text="晶圆探针配方文档生成器", bg=C_HEADER, fg="white", font=("微软雅黑", 17, "bold")).pack(anchor="w", padx=20, pady=(10,0))
        self.step_var = tk.StringVar(value="")
        tk.Label(h, textvariable=self.step_var, bg=C_HEADER, fg="#b8c5d6", font=("微软雅黑", 10)).pack(anchor="w", padx=20)
        nav = tk.Frame(h, bg=C_HEADER); nav.pack(anchor="w", padx=16, pady=(4,0))
        self.dot_labels = []; self.txt_labels = []
        titles = ["晶圆信息","参数","图片","扎针序号","偏移","DIE0","配方"]
        for i, t in enumerate(titles):
            cell = tk.Frame(nav, bg=C_HEADER); cell.pack(side="left", padx=6)
            d = tk.Label(cell, text=str(i+1), bg=C_GRAY, fg="white", width=2, font=("微软雅黑",9,"bold"))
            d.pack(); self.dot_labels.append(d)
            l = tk.Label(cell, text=t, bg=C_HEADER, fg="#b8c5d6", font=("微软雅黑",8)); l.pack(); self.txt_labels.append(l)
        barf = tk.Frame(self.root, bg=C_BG); barf.pack(fill="x", padx=18, pady=(8,0))
        self.bar = ttk.Progressbar(barf, length=200, mode="determinate", maximum=100)
        self.bar.pack(fill="x")
        self.pct_var = tk.StringVar(value="0%")
        tk.Label(self.root, textvariable=self.pct_var, bg=C_BG, fg=C_SUB, font=("微软雅黑",9)).pack(anchor="e", padx=20)

    def _build_footer(self):
        foot = tk.Frame(self.root, bg=C_BG, height=58); foot.pack(fill="x", padx=18, pady=(0,10)); foot.pack_propagate(False)
        self.status_var = tk.StringVar(value="")
        tk.Label(foot, textvariable=self.status_var, bg=C_BG, fg=C_WARN, font=FONT_BOLD).pack(side="left", padx=4)
        self.btn_back = self.mkbtn(foot, "◀ 上一步", self.go_back, "#8a97a5")
        self.btn_next = self.mkbtn(foot, "下一步 ▶", self.go_next, C_ACCENT)
        self.btn_gen  = self.mkbtn(foot, "生成 PDF 文档", self.generate, C_DONE)

    def mkbtn(self, parent, text, cmd, color):
        return tk.Button(parent, text=text, command=cmd, bg=color, fg="white", font=FONT_BOLD, relief="flat",
                         padx=18, pady=6, cursor="hand2", activebackground=color, activeforeground="white")

    def field(self, parent, label, key, val="", row=0, col=0, width=18, ro=False):
        tk.Label(parent, text=label, bg=C_CARD, fg=C_TEXT, font=FONT_BASE).grid(row=row, column=col*2, sticky="e", padx=8, pady=6)
        e = tk.Entry(parent, width=width, textvariable=tk.StringVar(value=val), font=("Consolas", 10), relief="solid", bd=1,
                     disabledbackground="#f0f3f7", disabledforeground=C_TEXT)
        if ro: e.config(state="disabled")
        e.grid(row=row, column=col*2+1, sticky="w", padx=8, pady=6)
        self.entries[key] = e; return e

    def page_title(self, parent, text):
        tk.Label(parent, text=text, bg=C_CARD, fg=C_HEADER, font=FONT_H).pack(anchor="w", padx=4, pady=(10,2))
        tk.Frame(parent, bg=C_ACCENT, height=2).pack(fill="x", padx=4, pady=(0,8))

    def note(self, parent, text):
        tk.Label(parent, text=text, bg=C_CARD, fg=C_SUB, font=("微软雅黑",9), wraplength=900, justify="left").pack(anchor="w", padx=4, pady=(2,4))

    def set_readonly(self, key, val):
        e = self.entries[key]; e.config(state="normal"); e.delete(0,"end"); e.insert(0, str(val)); e.config(state="disabled")

    def _define_steps(self):
        return [
            {"title":"上传晶圆信息 JSON", "build":self.page1, "validate":self.v1},
            {"title":"基本信息 / DIE / 切割道", "build":self.page2, "validate":self.v2},
            {"title":"上传 3 张图片", "build":self.page3, "validate":self.v3},
            {"title":"扎针序号与晶圆方向", "build":self.page4, "validate":self.v4},
            {"title":"分组 Y 偏移", "build":self.page5, "validate":self.v5},
            {"title":"DIE0 偏移", "build":self.page6, "validate":self.v6},
            {"title":"配方参数", "build":self.page7, "validate":self.v7},
        ]

    def page1(self, f):
        self.page_title(f, "步骤 1  上传晶圆信息 JSON")
        self.note(f, "上传后自动填充：产品ID、DIE高/宽、切割道宽、晶圆尺寸、rows 映射图 等。")
        bf = tk.Frame(f, bg=C_CARD); bf.pack(fill="x", pady=8)
        tk.Button(bf, text="  选择 晶圆信息 JSON 文件  ", command=self.load_wafer_json, bg=C_ACCENT, fg="white",
                  font=FONT_BOLD, relief="flat", padx=14, pady=7, cursor="hand2").pack(side="left", padx=4)
        self.wafer_status = tk.StringVar(value="尚未上传")
        tk.Label(bf, textvariable=self.wafer_status, bg=C_CARD, fg=C_ACCENT, font=FONT_BOLD).pack(side="left", padx=10)
        self.rows_txt = scrolledtext.ScrolledText(f, height=12, font=("Consolas",8), relief="solid", bd=1)
        self.rows_txt.pack(fill="both", expand=True, pady=6, padx=4)

    def v1(self):
        if not self.wafer_raw_json.strip(): return False, "请先上传晶圆信息 JSON 文件"
        return True, ""

    def page2(self, f):
        self.page_title(f, "步骤 2  基本信息 / DIE / 切割道（已自动带入，可修改）")
        g = tk.Frame(f, bg=C_CARD); g.pack(fill="x", padx=4)
        self.field(g, "产品ID", "name", "341-6", 0, 0)
        tk.Label(g, text="标题预览", bg=C_CARD, fg=C_TEXT, font=FONT_BASE).grid(row=0, column=2, sticky="e", padx=8)
        self.title_preview = tk.StringVar(value="341-6(2)")
        tk.Label(g, textvariable=self.title_preview, bg=C_CARD, fg=C_ACCENT, font=FONT_BOLD).grid(row=0, column=3, sticky="w", padx=8)
        self.field(g, "内部序号 id", "id", "10", 1, 0); self.field(g, "晶圆尺寸(寸)", "wafer_size", "6", 1, 1)
        self.field(g, "wafer_id", "wafer_id", "341-6", 2, 0); self.field(g, "晶圆直径", "wafer_dia", "200", 2, 1)
        self.field(g, "manufacturer", "manu", "", 3, 0); self.field(g, "process_node", "node", "", 3, 1)
        self.field(g, "厚度 thickness", "thick", "0.725", 4, 0); self.field(g, "gdpw", "gdpw", "1296", 4, 1)
        tk.Label(f, text="DIE 与切割道", bg=C_CARD, fg=C_HEADER, font=("微软雅黑",11,"bold")).pack(anchor="w", padx=4, pady=(10,0))
        g2 = tk.Frame(f, bg=C_CARD); g2.pack(fill="x", padx=4)
        self.field(g2, "DIE 高 h_x", "die_h", "3.84", 0, 0); self.field(g2, "DIE 宽 w_y", "die_w", "2.86", 0, 1)
        self.field(g2, "切割道宽", "sc_w", "0.1", 1, 0); self.field(g2, "切割道高", "sc_h", "0.1", 1, 1)
        self.field(g2, "切割道材料", "material", "Si", 2, 0)
        tk.Label(f, text="探卡图说明（规则固定：居中设计）", bg=C_CARD, fg=C_HEADER, font=("微软雅黑",11,"bold")).pack(anchor="w", padx=4, pady=(10,0))
        g3 = tk.Frame(f, bg=C_CARD); g3.pack(fill="x", padx=4)
        tk.Label(g3, text="规则(固定)", bg=C_CARD, fg=C_TEXT, font=FONT_BASE).grid(row=0, column=0, sticky="e", padx=8, pady=6)
        tk.Label(g3, text=PROBE_RULE, bg=C_CARD, fg=C_ACCENT, font=FONT_BASE).grid(row=0, column=1, sticky="w", padx=8, pady=6)
        self.field(g3, "额外说明(可选)", "pc_extra", "", 1, 0, 48)

    def v2(self):
        try:
            g = lambda k: self.entries[k].get().strip()
            if not g("name"): return False, "产品ID 不能为空"
            num(g("die_h"), "DIE 高"); num(g("die_w"), "DIE 宽")
            num(g("sc_w"), "切割道宽"); num(g("sc_h"), "切割道高"); integ(g("wafer_size"), "晶圆尺寸")
            return True, ""
        except ValueError as e: return False, str(e)

    def page3(self, f):
        self.page_title(f, "步骤 3  上传 3 张图片")
        self.note(f, "探卡图 → 探卡说明右侧；Mapping图 → 晶圆拍照点序号右侧；探针信息位置图 → 探针拍照位置右侧。")
        for which, lbl, attr in [("probe","探卡图","probe_lbl"),("map","Mapping图","map_lbl"),("pos","探针信息位置图","pos_lbl")]:
            rowf = tk.Frame(f, bg=C_CARD); rowf.pack(fill="x", pady=8, padx=4)
            tk.Button(rowf, text=f"  上传 {lbl}  ", command=lambda w=which: self.load_img(w), bg="#5a8dee",
                      fg="white", font=FONT_BOLD, relief="flat", padx=12, pady=6, cursor="hand2").pack(side="left")
            sv = tk.StringVar(value="未上传"); setattr(self, attr, sv)
            tk.Label(rowf, textvariable=sv, bg=C_CARD, fg=C_DONE, font=FONT_BOLD).pack(side="left", padx=12)

    def v3(self):
        if not self.probe_img: return False, "请上传 探卡图"
        if not self.map_img: return False, "请上传 Mapping图"
        if not self.pos_img: return False, "请上传 探针信息位置图"
        return True, ""

    def page4(self, f):
        self.page_title(f, "步骤 4  扎针次数 / 晶圆方向（只填第一次，其余自动计算）")
        self.note(f, "只需填写【第1次】A/B/C 序号，选择晶圆移动方向，第2/3次序号会按公式自动算出。标题后缀 = 扎针次数。")
        g = tk.Frame(f, bg=C_CARD); g.pack(fill="x", padx=4, pady=4)
        tk.Label(g, text="扎针次数", bg=C_CARD, fg=C_TEXT, font=FONT_BASE).grid(row=0, column=0, sticky="e", padx=8, pady=6)
        cb = ttk.Combobox(g, width=5, textvariable=self.nround, values=[1,2,3], state="readonly", font=FONT_BASE)
        cb.grid(row=0, column=1, sticky="w", padx=8, pady=6); cb.bind("<<ComboboxSelected>>", lambda e: self.on_round_change())
        tk.Label(g, text="晶圆移动方向", bg=C_CARD, fg=C_TEXT, font=FONT_BASE).grid(row=0, column=2, sticky="e", padx=8)
        dcb = ttk.Combobox(g, width=30, textvariable=self.direct,
            values=["往左 (Y偏移为负, 后续序号 +1)", "往右 (Y偏移为正, 后续序号 -1)"], state="readonly", font=FONT_BASE)
        dcb.grid(row=0, column=3, sticky="w", padx=8); dcb.bind("<<ComboboxSelected>>", lambda e: self.refresh_abc())
        self.round_frame = tk.Frame(f, bg=C_CARD); self.round_frame.pack(fill="x", padx=4, pady=6)
        self.build_round_rows(); self.update_title()

    def build_round_rows(self):
        for w in self.round_frame.winfo_children(): w.destroy()
        n = self.nround.get()
        heads = ["A","B","C"]
        for ci, h in enumerate([""]+heads):
            tk.Label(self.round_frame, text=h, bg=C_CARD, fg=C_HEADER, font=FONT_BOLD, width=8).grid(row=0, column=ci, padx=4, pady=2)
        for i in range(n):
            tag = "第1次(填写)" if i==0 else f"第{i+1}次(自动)"
            fg = C_TEXT if i==0 else C_DONE
            tk.Label(self.round_frame, text=tag, bg=C_CARD, fg=fg, font=FONT_BOLD, width=12).grid(row=i+1, column=0, padx=4, pady=2)
            for ci, k in enumerate(heads):
                e = self.field(self.round_frame, "", f"{k}{i}", "0", row=i+1, col=0, width=9, ro=(i!=0))
                e.grid_forget(); e.grid(row=i+1, column=ci+1, padx=4, pady=2)
        self.refresh_abc()

    def on_round_change(self):
        self.build_round_rows(); self.update_title()

    def refresh_abc(self):
        n = self.nround.get()
        left = self.direct.get().startswith("往左"); sign = +1 if left else -1
        for k in ("A","B","C"):
            try: first = integ(self.entries[f"{k}0"].get(), f"第1次 {k}")
            except ValueError: first = None
            for i in range(1, n):
                self.set_readonly(f"{k}{i}", first + sign*i if first is not None else "--")

    def v4(self):
        try:
            for k in ("A","B","C"): integ(self.entries[f"{k}0"].get(), f"第1次 {k}")
            self.refresh_abc(); return True, ""
        except ValueError as e: return False, str(e)

    def page5(self, f):
        self.page_title(f, "步骤 5  分组 Y 偏移（方向已在第4步选定）")
        self.note(f, "Y 正负由第4步晶圆移动方向决定：往左为负、往右为正。点自动计算按 1/3 或 1/4 公式填充。")
        ff = tk.Frame(f, bg=C_CARD); ff.pack(fill="x", padx=4, pady=4)
        tk.Label(ff, text="原则", bg=C_CARD, fg=C_TEXT, font=FONT_BASE).grid(row=0, column=0, sticky="e", padx=8)
        self.rule = tk.StringVar(value="1/3")
        ttk.Combobox(ff, width=5, textvariable=self.rule, values=["1/3","1/4"], state="readonly", font=FONT_BASE).grid(row=0, column=1, sticky="w", padx=8)
        tk.Label(ff, text="base", bg=C_CARD, fg=C_TEXT, font=FONT_BASE).grid(row=0, column=2, padx=8)
        self.base = tk.StringVar(value="0")
        tk.Entry(ff, width=8, textvariable=self.base, font=("Consolas",10), relief="solid", bd=1).grid(row=0, column=3, padx=8)
        tk.Button(ff, text="  自动计算 Y 偏移  ", command=self.auto_calc, bg=C_ACCENT, fg="white",
                  font=FONT_BOLD, relief="flat", padx=12, pady=5, cursor="hand2").grid(row=0, column=4, padx=12)
        tk.Label(f, text="分组 Y 偏移结果（可微调，最终PDF自动带正负号）", bg=C_CARD, fg=C_HEADER, font=("微软雅黑",11,"bold")).pack(anchor="w", padx=4, pady=(8,0))
        self.group_frame = tk.Frame(f, bg=C_CARD); self.group_frame.pack(fill="x", padx=4, pady=4)
        self.redraw_groups()
        tk.Label(f, text="扎针规则描述(可选，写入文档)：", bg=C_CARD, fg=C_SUB, font=("微软雅黑",9)).pack(anchor="w", padx=4, pady=(6,0))
        self.pun_desc_e = tk.Entry(f, width=72, font=FONT_BASE, relief="solid", bd=1, textvariable=tk.StringVar(value=""))
        self.pun_desc_e.pack(anchor="w", padx=4, pady=2); self.entries["pun_desc"] = self.pun_desc_e

    def v5(self):
        try:
            for i in range(self.nround.get()): num(self.entries[f"gy{i}"].get(), f"分组{i+1} Y偏移")
            return True, ""
        except ValueError as e: return False, str(e)

    def page6(self, f):
        self.page_title(f, "步骤 6  DIE0 偏移")
        g = tk.Frame(f, bg=C_CARD); g.pack(fill="x", padx=4, pady=4)
        self.field(g, "DIE0 X", "d0x", "0", 0, 0); self.field(g, "DIE0 Y", "d0y", "0", 0, 1)
        self.field(g, "备注", "d0rmk", "", 1, 0, 42)
        bf = tk.Frame(f, bg=C_CARD); bf.pack(fill="x", padx=4, pady=6)
        self.die_plus_lbl = tk.StringVar(value="die宽+切割道 = --")
        tk.Label(bf, textvariable=self.die_plus_lbl, bg=C_CARD, fg=C_ACCENT, font=FONT_BOLD).pack(side="left", padx=4)
        for txt, key, sgn in [("X +","d0x",+1),("X −","d0x",-1),("Y +","d0y",+1),("Y −","d0y",-1)]:
            tk.Button(bf, text=txt, command=lambda k=key,s=sgn: self.die_plus(k,s), bg="#5a8dee",
                      fg="white", font=FONT_BOLD, relief="flat", padx=10, pady=4, cursor="hand2").pack(side="left", padx=4)
        self.update_die_plus()

    def v6(self):
        try:
            num(self.entries["d0x"].get(), "DIE0 X"); num(self.entries["d0y"].get(), "DIE0 Y")
            return True, ""
        except ValueError as e: return False, str(e)

    def page7(self, f):
        self.page_title(f, "步骤 7  配方参数（最后一步，完成即可生成 PDF）")
        bf = tk.Frame(f, bg=C_CARD); bf.pack(fill="x", padx=4, pady=4)
        tk.Button(bf, text="上传配方 JSON", command=self.load_vm, bg="#5a8dee", fg="white",
                  font=FONT_BOLD, relief="flat", padx=12, pady=5, cursor="hand2").pack(side="left", padx=4)
        tk.Button(bf, text="载入默认模板", command=self.load_vm_default, bg="#8a97a5", fg="white",
                  font=FONT_BOLD, relief="flat", padx=12, pady=5, cursor="hand2").pack(side="left", padx=4)
        self.vm_txt = scrolledtext.ScrolledText(f, height=20, font=("Consolas",8), relief="solid", bd=1)
        self.vm_txt.pack(fill="both", expand=True, padx=4, pady=4)
        self.load_vm_default()

    def v7(self):
        try:
            txt = self.vm_txt.get("1.0","end").strip()
            if not txt: return False, "配方参数不能为空"
            json.loads(txt); return True, ""
        except Exception as e: return False, f"配方参数 JSON 格式错误: {e}"

    def show(self, idx):
        for p in self.pages: p.pack_forget()
        self.pages[idx].pack(fill="both", expand=True, padx=16, pady=8)
        self.cur = idx
        self.step_var.set(f"步骤 {idx+1} / {self.total}：{self.steps[idx]['title']}")
        self.bar["value"] = (idx/self.total)*100
        self.pct_var.set(f"{int((idx/self.total)*100)}%")
        self.status_var.set("")
        for i in range(self.total):
            col = C_DONE if i < idx else (C_ACCENT if i == idx else C_GRAY)
            self.dot_labels[i].config(bg=col)
            self.txt_labels[i].config(fg=("white" if i==idx else "#b8c5d6"))
        for b in (self.btn_back, self.btn_next, self.btn_gen):
            try: b.pack_forget()
            except Exception: pass
        if idx > 0: self.btn_back.pack(side="left", padx=4)
        if idx < self.total-1: self.btn_next.pack(side="right", padx=4)
        else: self.btn_gen.pack(side="right", padx=4)

    def go_next(self):
        ok, msg = self.steps[self.cur]["validate"]()
        if not ok: self.status_var.set("✗  " + msg); return
        self.status_var.set("")
        if self.cur < self.total-1: self.show(self.cur+1)

    def go_back(self):
        if self.cur > 0: self.show(self.cur-1)

    def update_title(self):
        nm = self.entries.get("name"); name = nm.get().strip() if nm else ""
        self.title_preview.set(f"{name}({self.nround.get()})")

    def update_die_plus(self):
        try:
            dw = float(self.entries["die_w"].get().strip() or 0); sw = float(self.entries["sc_w"].get().strip() or 0)
            self.die_plus_lbl.set(f"die宽+切割道 = {round(dw+sw,3)}")
        except Exception: self.die_plus_lbl.set("die宽+切割道 = --")

    def die_plus(self, key, sign):
        try:
            dw = float(self.entries["die_w"].get().strip() or 0); sw = float(self.entries["sc_w"].get().strip() or 0)
            cur = float(self.entries[key].get().strip() or 0)
            v = round(cur + sign*(dw+sw), 3)
            self.entries[key].delete(0,"end"); self.entries[key].insert(0, str(v))
        except Exception as ex: messagebox.showerror("错误", str(ex))

    def auto_calc(self):
        try:
            die_w = num(self.entries["die_w"].get(), "DIE 宽"); base = num(self.base.get(), "base")
            rule = self.rule.get(); direction = "Y-" if self.direct.get().startswith("往左") else "Y+"
            if die_w > 3.0 and rule == "1/4":
                if messagebox.askyesno("提示", f"DIE宽 {die_w} > 3.0, 建议用 1/3 原则。是否切换?"):
                    self.rule.set("1/3"); rule = "1/3"
            g1, g2 = calc_offsets(die_w, base, rule, direction)
            ys = [g1, g2] + [g2]*5
            for i in range(self.nround.get()):
                self.entries[f"gy{i}"].delete(0,"end"); self.entries[f"gy{i}"].insert(0, str(ys[i]))
            messagebox.showinfo("完成", f"晶圆往{'左' if direction=='Y-' else '右'} (Y{'负' if direction=='Y-' else '正'}) 按 {rule} 计算:\n分组1 Y = {fmt_signed(g1)}\n分组2 Y = {fmt_signed(g2)}")
        except Exception as ex: messagebox.showerror("错误", str(ex))

    def redraw_groups(self):
        if not hasattr(self, "group_frame"): return
        for w in self.group_frame.winfo_children(): w.destroy()
        n = self.nround.get()
        for i in range(n):
            tk.Label(self.group_frame, text=f"分组{i+1}  Y", bg=C_CARD, fg=C_TEXT, font=FONT_BASE).grid(row=i, column=0, padx=8, pady=3)
            ey = tk.Entry(self.group_frame, width=12, textvariable=tk.StringVar(value="0"), font=("Consolas",10), relief="solid", bd=1)
            ey.grid(row=i, column=1, padx=8, pady=3); self.entries[f"gy{i}"] = ey
            tk.Label(self.group_frame, text="离圆心偏移", bg=C_CARD, fg=C_TEXT, font=FONT_BASE).grid(row=i, column=2, padx=8, pady=3)
            eco = tk.Entry(self.group_frame, width=14, textvariable=tk.StringVar(value=""), font=("Consolas",10), relief="solid", bd=1)
            eco.grid(row=i, column=3, padx=8, pady=3); self.entries[f"gco{i}"] = eco

    def load_wafer_json(self):
        f = filedialog.askopenfilename(filetypes=[("JSON","*.json"),("文本","*.txt"),("所有","*.*")])
        if not f: return
        try:
            txt = open(f, encoding="utf-8").read(); data = json.loads(txt)
        except Exception as ex: messagebox.showerror("失败", f"JSON 解析失败: {ex}"); return
        self.wafer_raw_json = txt.strip()
        self.wafer_status.set("✓ " + os.path.basename(f) + "  已导入")
        wi = data.get("wafer_info", {}); dim = wi.get("dimensions", {}); sc = wi.get("scribe_line", {}); meta = data.get("metadata", {})
        def setv(k, v):
            if k in self.entries and v is not None:
                self.entries[k].delete(0,"end"); self.entries[k].insert(0, str(v))
        setv("name", data.get("name")); setv("wafer_id", data.get("wafer_id")); setv("id", data.get("id"))
        setv("die_h", dim.get("h_x")); setv("die_w", dim.get("w_y")); setv("gdpw", dim.get("gdpw"))
        setv("sc_w", sc.get("width")); setv("sc_h", sc.get("width")); setv("material", sc.get("material"))
        setv("thick", wi.get("thickness")); setv("wafer_dia", wi.get("wafer_diameter"))
        setv("manu", meta.get("manufacturer")); setv("node", meta.get("process_node"))
        ws = wi.get("wafer_size")
        if not ws:
            nm = str(data.get("name","")); ws = nm[-1] if nm and nm[-1].isdigit() else ""
        setv("wafer_size", ws)
        self.rows_txt.delete("1.0","end"); self.rows_txt.insert("1.0", "\n".join(dim.get("rows", [])))
        self.update_title(); self.update_die_plus()
        messagebox.showinfo("完成", "晶圆信息已导入，相关字段已自动填充。")

    def load_img(self, which):
        f = filedialog.askopenfilename(filetypes=[("图片","*.png;*.jpg;*.jpeg;*.bmp"),("所有","*.*")])
        if not f: return
        if which == "probe": self.probe_img = f; self.probe_lbl.set("✓ " + os.path.basename(f))
        elif which == "map": self.map_img = f; self.map_lbl.set("✓ " + os.path.basename(f))
        else: self.pos_img = f; self.pos_lbl.set("✓ " + os.path.basename(f))

    def load_vm(self):
        f = filedialog.askopenfilename(filetypes=[("JSON","*.json"),("所有","*.*")])
        if f: self.vm_txt.delete("1.0","end"); self.vm_txt.insert("1.0", open(f, encoding="utf-8").read())

    def load_vm_default(self):
        d = {"gate_roi_width_px":300,"gate_roi_height_px":260,"gate_roi_angle_deg":1.0,
             "high_mag_camera_exposure":200,"die_exposure":400,"gate_threshold":50,
             "probe_exposure":3000,"probe_roi_width_px":600,"probe_roi_height_px":600,
             "probe_dynamic_tracking":1,"gate_width":600.0,"gate_height":600.0,"gate_algorithm":0,
             "probe_point_set":{"A":[69.65,69.65],"B":[-69.65,-69.65],"C":[58.22,27.69],"D":[58.22,-38.62],"E":[-69.78,27.69]},
             "wafer_mark_recognition":0,"wafer_mark_exposure":200}
        self.vm_txt.delete("1.0","end"); self.vm_txt.insert("1.0", json.dumps(d, ensure_ascii=False, indent=2))

    def generate(self):
        try:
            for i in range(self.total):
                ok, msg = self.steps[i]["validate"]()
                if not ok:
                    messagebox.showwarning("步骤未完成", f"第 {i+1} 步：{self.steps[i]['title']}\n{msg}")
                    self.show(i); return
            nround = int(self.nround.get())
            cfg = collect(self.entries, self.rows_txt.get("1.0","end"), self.vm_txt.get("1.0","end"),
                          self.probe_img, self.map_img, self.pos_img, self.wafer_raw_json, nround)
            out = filedialog.asksaveasfilename(defaultextension=".pdf",
                initialfile=f"{cfg['product_name']}{cfg['doc_suffix']}.pdf", filetypes=[("PDF","*.pdf")])
            if not out: return
            doc = SimpleDocTemplate(out, pagesize=A4, leftMargin=18*mm, rightMargin=18*mm, topMargin=15*mm, bottomMargin=15*mm)
            doc.build(build_story(cfg))
            json.dump(_wafer_obj(cfg), open(out.replace(".pdf","_wafer.json"),"w",encoding="utf-8"), ensure_ascii=False, indent=2)
            self.bar["value"] = 100; self.pct_var.set("100%")
            messagebox.showinfo("成功", f"已生成:\n{out}")
        except Exception as ex: messagebox.showerror("生成失败", str(ex))

if __name__ == "__main__":
    root = tk.Tk(); App(root); root.mainloop()
