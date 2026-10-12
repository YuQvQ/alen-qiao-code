# -*- coding: utf-8 -*-
"""
晶圆探针配方文档生成器
用法: py generate.py config_318-8.json
输出: <product_name>.pdf  与  <product_name>_wafer.json
"""
import sys, os, json
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.colors import HexColor, black
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer,
                                 Preformatted, Table, TableStyle)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

# ---------- 中文字体 ----------
def reg_font():
    for p in [r"C:\Windows\Fonts\msyh.ttc", r"C:\Windows\Fonts\simsun.ttc"]:
        if os.path.exists(p):
            pdfmetrics.registerFont(TTFont("CJK", p, subfontIndex=0))
            return "CJK"
    return "Helvetica"
CJK = reg_font()

PRIMARY = HexColor("#1a365d")
ACCENT  = HexColor("#2b6cb0")

def st(name, size, color=black, bold=False, align=0, space_after=6, leading=None):
    return ParagraphStyle(name, fontName=CJK, fontSize=size,
                          leading=leading or size*1.5, textColor=color,
                          alignment=align, spaceAfter=space_after, wordWrap="CJK")

S_TITLE = st("title", 22, PRIMARY, align=1, space_after=4)
S_H1    = st("h1", 15, PRIMARY, space_after=8)
S_H2    = st("h2", 12, ACCENT, space_after=6)
S_BODY  = st("body", 10.5, space_after=5)
S_BULLET= st("bullet", 10.5, space_after=4)
S_CODE  = ParagraphStyle("code", fontName=CJK, fontSize=8.5, leading=11,
                         textColor=HexColor("#1a202c"), wordWrap="CJK")
S_LBL   = st("lbl", 10.5, HexColor("#4a5568"))

# ---------- 构建晶圆信息 JSON ----------
def build_wafer_json(cfg):
    wi = cfg["wafer_info"]
    rows = open(os.path.join(os.path.dirname(sys.argv[1]) if len(sys.argv)>1 else ".",
                             wi["rows_file"]), encoding="utf-8").read().splitlines()
    rows = [r for r in rows if r.strip() != ""]
    die = cfg["die"]; sc = cfg["scribe_line"]
    obj = {
        "name": wi["name"],
        "wafer_id": wi["wafer_id"],
        "version_id": wi["version_id"],
        "metadata": wi["metadata"],
        "wafer_info": {
            "product_id": wi["name"],
            "wafer_size": wi["wafer_size"],
            "wafer_diameter": wi["wafer_diameter"],
            "dimensions": {
                "h_x": die["height"],
                "w_y": die["width"],
                "gdpw": wi["gdpw"],
                "rows": rows,
            },
            "scribe_line": {
                "width": sc["width"],
                "material": sc["material"],
                "has_test_key": sc["has_test_key"],
            },
            "thickness": cfg["thickness"],
        },
        "id": cfg["id"],
    }
    return obj

def build_vm_params(cfg):
    p = dict(cfg["vm_params"])
    rounds = cfg["puncture"]["rounds"]
    p["die_idxs"] = [rounds[0]["A"], rounds[0]["B"], rounds[0]["C"]]
    return {"vm_params": p}

# ---------- PDF 内容 ----------
def build_story(cfg):
    story = []
    P = lambda t, s: story.append(Paragraph(t, s))
    die = cfg["die"]; sc = cfg["scribe_line"]
    pc = cfg["probe_card"]; pun = cfg["puncture"]

    # 标题
    P(f"{cfg['product_name']}{cfg['doc_suffix']}", S_TITLE)
    P("晶圆信息 结构提供", S_H1)

    # DIE 宽高 / 切割道
    P("● DIE宽高", S_BULLET)
    P(f"高: {die['height']}", S_BODY); P(f"宽: {die['width']}", S_BODY)
    P("● 切割道宽高", S_BULLET)
    P(f"宽: {sc['width']}", S_BODY); P(f"高: {sc['height']}", S_BODY)

    # 探卡图
    P("● 探卡图", S_BULLET)
    P(f"规则: {pc['rule']}", S_BODY)
    if pc.get("extra_note"):
        P(pc["extra_note"], S_BODY)

    # 晶圆信息 JSON
    P("晶圆信息", S_H2)
    wj = build_wafer_json(cfg)
    story.append(Preformatted(json.dumps(wj, ensure_ascii=False, indent=2), S_CODE))
    story.append(Spacer(1, 6))

    # 探针晶圆映射图
    P("● 探针晶圆映射图 (多次扎针)(结构提供)", S_BULLET)
    P("最终输出", S_H2)
    P("a. 探针拍照位置信息", S_BODY)
    P("b. 晶圆拍照点序号", S_BODY)
    P("基于探针晶圆映射图", S_LBL)
    for r in pun["rounds"]:
        P(f"{r['label']}", S_BODY)
        P(f"A点序号 {r['A']}<br/>B点序号 {r['B']}<br/>C点序号 {r['C']}", S_BODY)
    P(pun["rule_desc"], S_LBL)

    # c. 分组偏移
    P("c. 多次扎针Chuck接料位偏移, 圆边识别偏移, 晶圆Mark偏移", S_BODY)
    for g in cfg["groups"]:
        head = g["name"] + (f"（离晶圆圆心偏移 {g['center_offset']}）" if g.get("center_offset") else "")
        P(head, S_H2)
        P(f"1. 接料位 Chuck接料位偏移 Y {g['chuck_y']}", S_BODY)
        P(f"2. 晶圆拍照点新增分组, 晶圆拍照点点位不变, 序号改成A点序号 {g['A']} B点序号 {g['B']} C点序号 {g['C']}", S_BODY)
        P(f"3. 圆边拍照点新增分组 圆边A,B,C拍照点 Y {g['edge_y']}", S_BODY)
        P(f"4. 晶圆Mark新增分组, 晶圆Mark偏移 Y {g['mark_y']}", S_BODY)

    # d / e
    P("d. 探针拍照点数据(探针纠偏用", S_BODY)
    P("参考f.配方参数的 probe_point_set", S_LBL)
    P("e. DIE0相对于探卡中心偏移量", S_BODY)
    d0 = cfg["die0_offset"]
    P(f"偏差值X,Y [{d0['x']},{d0['y']}] {d0.get('remark','')}", S_BODY)

    # f. 配方参数
    P("f. 配方参数", S_H2)
    vp = build_vm_params(cfg)
    story.append(Preformatted(json.dumps(vp, ensure_ascii=False, indent=2), S_CODE))
    return story

def main():
    cfg_path = sys.argv[1]
    cfg = json.load(open(cfg_path, encoding="utf-8"))
    base = os.path.splitext(cfg_path)[0]
    out_pdf = base + ".pdf"

    doc = SimpleDocTemplate(out_pdf, pagesize=A4,
                            leftMargin=18*mm, rightMargin=18*mm,
                            topMargin=15*mm, bottomMargin=15*mm)
    doc.build(build_story(cfg))
    print("生成 PDF:", out_pdf)

    # 同时导出晶圆 JSON 供程序使用
    wj = build_wafer_json(cfg)
    json.dump(wj, open(base + "_wafer.json", "w", encoding="utf-8"),
              ensure_ascii=False, indent=2)
    print("导出 wafer JSON:", base + "_wafer.json")

if __name__ == "__main__":
    main()
