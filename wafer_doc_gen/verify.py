import pdfplumber
with pdfplumber.open(r"D:\unisic_vision\wafer_doc_gen\config_318-8.pdf") as pdf:
    print("pages:", len(pdf.pages))
    for i,p in enumerate(pdf.pages):
        t = p.extract_text() or ""
        print(f"--- page {i+1} ({len(t)} chars) ---")
        print(t[:400])
