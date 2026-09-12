"""按文档顺序提取 DOCX 段落和表格文本，供报告内容审计与 diff 使用。"""

from pathlib import Path
import sys
from docx import Document
from docx.table import Table
from docx.text.paragraph import Paragraph

def iter_blocks(document):
    """保持 body 中段落/表格的原始交错顺序。"""
    for child in document.element.body.iterchildren():
        if child.tag.endswith("}p"):
            yield Paragraph(child, document)
        elif child.tag.endswith("}tbl"):
            yield Table(child, document)


def main() -> int:
    """读取输入 DOCX，把可见文字写为稳定的 UTF-8 纯文本。"""
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    document = Document(source)
    lines = []
    for block in iter_blocks(document):
        if isinstance(block, Paragraph):
            text = block.text.strip()
            if text:
                lines.append(text)
        else:
            lines.append("[TABLE]")
            for row in block.rows:
                lines.append("\t".join(cell.text.replace("\n", " / ").strip() for cell in row.cells))
            lines.append("[/TABLE]")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"paragraphs_and_table_rows={len(lines)}")
    print(f"output={destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
