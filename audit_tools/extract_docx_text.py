from pathlib import Path
import sys

from docx import Document
from docx.table import Table
from docx.text.paragraph import Paragraph


def iter_blocks(document):
    for child in document.element.body.iterchildren():
        if child.tag.endswith("}p"):
            yield Paragraph(child, document)
        elif child.tag.endswith("}tbl"):
            yield Table(child, document)


def main() -> int:
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
