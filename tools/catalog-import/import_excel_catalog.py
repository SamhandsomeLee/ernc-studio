# -*- coding: utf-8 -*-
"""Import the three ERNC优化.xlsx sheets into catalog/source.

The workbook stays outside git. This script copies cell text as stored and
writes a separate forward-filled grouping for the 94-item count. It does not
rewrite spelling, merge aliases, or assign CAN slots.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

MAIN_NS = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
REL_NS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
PKG_NS = "http://schemas.openxmlformats.org/package/2006/relationships"

EXPECTED_SHEETS = ("工作表1", "ERNC参数说明", "can_msg信息")
EXPECTED_GROUP_COUNTS = {
    "ERNC_Flag": 6,
    "ERNC_IN_OUT": 12,
    "Check": 5,
    "ENC_Param": 32,
    "RNC_Param": 23,
    "Threshold": 16,
}
EXPECTED_CATEGORY_ONLY = ("ALL", "ERNC_Switch_State")
EXPECTED_GROUPED = 94
EXPECTED_DESCRIPTIONS = 175
EXPECTED_CAN = 36


class ImportError(RuntimeError):
    """The workbook does not match the ES-006 acceptance counts."""


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def column_index(column: str) -> int:
    value = 0
    for char in column:
        value = value * 26 + (ord(char.upper()) - 64)
    return value


def split_ref(cell_ref: str) -> tuple[str, int]:
    column = "".join(char for char in cell_ref if char.isalpha())
    row = int("".join(char for char in cell_ref if char.isdigit()))
    return column, row


def qualified(namespace: str, name: str) -> str:
    return f"{{{namespace}}}{name}"


def shared_strings(archive: zipfile.ZipFile) -> list[str]:
    if "xl/sharedStrings.xml" not in archive.namelist():
        return []
    root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
    values: list[str] = []
    for item in root.findall(qualified(MAIN_NS, "si")):
        texts = [
            node.text or ""
            for node in item.iter(qualified(MAIN_NS, "t"))
        ]
        values.append("".join(texts))
    return values


def cell_text(cell: ET.Element, strings: list[str]) -> str | None:
    kind = cell.attrib.get("t")
    if kind == "e":
        ref = cell.attrib.get("r", "?")
        raise ImportError(f"cell {ref} contains an Excel error")
    value = cell.find(qualified(MAIN_NS, "v"))
    if kind == "s":
        # A styled shared-string cell can be stored with no index. That is an empty cell.
        if value is None or value.text is None:
            return ""
        return strings[int(value.text)]
    if kind == "inlineStr":
        texts = [
            node.text or ""
            for node in cell.iter(qualified(MAIN_NS, "t"))
        ]
        return "".join(texts)
    if value is not None and value.text is not None:
        return value.text
    return None


def sheet_rows(archive: zipfile.ZipFile, target: str, strings: list[str]) -> list[dict]:
    root = ET.fromstring(archive.read(target))
    rows: list[dict] = []
    for row in root.findall(f"{qualified(MAIN_NS, 'sheetData')}/{qualified(MAIN_NS, 'row')}"):
        row_number = int(row.attrib["r"])
        cells: dict[str, str] = {}
        for cell in row.findall(qualified(MAIN_NS, "c")):
            ref = cell.attrib.get("r")
            if not ref:
                raise ImportError(f"sheet {target} has a cell without a reference on row {row_number}")
            column, ref_row = split_ref(ref)
            if ref_row != row_number:
                raise ImportError(f"cell {ref} does not match row {row_number}")
            text = cell_text(cell, strings)
            if text is not None:
                cells[column] = text
        ordered = {
            column: cells[column]
            for column in sorted(cells, key=column_index)
        }
        rows.append({"row": row_number, "cells": ordered})
    return rows


def workbook_sheets(archive: zipfile.ZipFile) -> list[tuple[str, str]]:
    workbook = ET.fromstring(archive.read("xl/workbook.xml"))
    relationships = ET.fromstring(archive.read("xl/_rels/workbook.xml.rels"))
    targets = {
        rel.attrib["Id"]: rel.attrib["Target"]
        for rel in relationships.findall(qualified(PKG_NS, "Relationship"))
    }
    sheets: list[tuple[str, str]] = []
    for sheet in workbook.findall(f"{qualified(MAIN_NS, 'sheets')}/{qualified(MAIN_NS, 'sheet')}"):
        name = sheet.attrib["name"]
        relationship = sheet.attrib[qualified(REL_NS, "id")]
        target = targets[relationship]
        if target.startswith("/"):
            target = target.lstrip("/")
        elif not target.startswith("xl/"):
            target = "xl/" + target.lstrip("/")
        sheets.append((name, target))
    return sheets


def nonempty(value: str | None) -> bool:
    return bool(value and value.strip())


def cell(row: dict, column: str) -> str:
    return row["cells"].get(column, "")


def require_sheets(sheets: list[tuple[str, str]]) -> dict[str, str]:
    names = [name for name, _target in sheets]
    if tuple(names) != EXPECTED_SHEETS:
        raise ImportError(f"sheet order is {names!r}, expected {list(EXPECTED_SHEETS)!r}")
    return dict(sheets)


def require_header(rows: list[dict], expected: dict[str, str], sheet: str) -> dict:
    if not rows or rows[0]["row"] != 1:
        raise ImportError(f"{sheet} does not start at row 1")
    header = rows[0]
    actual = {column: text for column, text in header["cells"].items() if nonempty(text)}
    if actual != expected:
        raise ImportError(f"{sheet} header is {actual!r}, expected {expected!r}")
    return header


def group_worksheet(rows: list[dict]) -> tuple[list[dict], list[dict], list[int]]:
    require_header(
        rows,
        {"A": "分类列表", "C": "备注", "E": "1.分类修改一下"},
        "工作表1",
    )
    category = ""
    items: list[dict] = []
    category_only: list[dict] = []
    empty_rows: list[int] = []
    for row in rows:
        if row["row"] == 1:
            continue
        values = row["cells"]
        if not any(nonempty(text) for text in values.values()):
            empty_rows.append(row["row"])
            continue
        label = cell(row, "A")
        name = cell(row, "B")
        if nonempty(label):
            category = label
        if nonempty(name):
            if not category:
                raise ImportError(f"工作表1 row {row['row']} has a name and no category")
            items.append({"row": row["row"], "category": category, "name": name})
        elif nonempty(label):
            category_only.append({"row": row["row"], "category": label})
    counts: dict[str, int] = {}
    for item in items:
        counts[item["category"]] = counts.get(item["category"], 0) + 1
    if counts != EXPECTED_GROUP_COUNTS:
        raise ImportError(f"grouped counts are {counts!r}, expected {EXPECTED_GROUP_COUNTS!r}")
    if tuple(item["category"] for item in category_only) != EXPECTED_CATEGORY_ONLY:
        found = [item["category"] for item in category_only]
        raise ImportError(f"category-only rows are {found!r}, expected {list(EXPECTED_CATEGORY_ONLY)!r}")
    if len(items) != EXPECTED_GROUPED:
        raise ImportError(f"grouped item count is {len(items)}, expected {EXPECTED_GROUPED}")
    return items, category_only, empty_rows


def description_rows(rows: list[dict]) -> list[dict]:
    require_header(
        rows,
        {"A": "参数名称", "B": "参数说明", "C": "备注"},
        "ERNC参数说明",
    )
    data = []
    for row in rows:
        if row["row"] == 1:
            continue
        if not any(nonempty(text) for text in row["cells"].values()):
            continue
        name = cell(row, "A")
        if not nonempty(name):
            raise ImportError(f"ERNC参数说明 row {row['row']} has no parameter name")
        extra = [column for column in row["cells"] if column not in {"A", "B", "C"} and nonempty(row["cells"][column])]
        if extra:
            raise ImportError(f"ERNC参数说明 row {row['row']} has extra columns {extra}")
        data.append(row)
    if len(data) != EXPECTED_DESCRIPTIONS:
        raise ImportError(f"description count is {len(data)}, expected {EXPECTED_DESCRIPTIONS}")
    return data


def can_rows(rows: list[dict]) -> list[dict]:
    data = []
    for row in rows:
        if not any(nonempty(text) for text in row["cells"].values()):
            continue
        extra = [column for column in row["cells"] if column not in {"J", "K"} and nonempty(row["cells"][column])]
        if extra:
            raise ImportError(f"can_msg信息 row {row['row']} has extra columns {extra}")
        number = cell(row, "J")
        name = cell(row, "K")
        if not nonempty(number) or not nonempty(name):
            raise ImportError(f"can_msg信息 row {row['row']} is missing J or K")
        data.append(row)
    if len(data) != EXPECTED_CAN:
        raise ImportError(f"CAN count is {len(data)}, expected {EXPECTED_CAN}")
    numbers = [cell(row, "J") for row in data]
    expected_numbers = [str(number) for number in range(1, EXPECTED_CAN + 1)]
    if numbers != expected_numbers:
        raise ImportError(f"CAN numbers are {numbers!r}, expected {expected_numbers!r}")
    return data


def names_only(left: list[tuple[int, str]], right: set[str]) -> list[tuple[int, str]]:
    return [(row, name) for row, name in left if name not in right]


def duplicate_names(entries: list[tuple[int, str]]) -> list[str]:
    seen: dict[str, int] = {}
    duplicates: list[str] = []
    for _row, name in entries:
        seen[name] = seen.get(name, 0) + 1
    for name, count in seen.items():
        if count > 1 and name not in duplicates:
            duplicates.append(name)
    return duplicates


def write_json(path: Path, payload: dict) -> None:
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def bullet_names(entries: list[tuple[int, str]], sheet: str) -> str:
    if not entries:
        return "无。\n"
    return "".join(f"- `{name}`（{sheet} 行 {row}）\n" for row, name in entries)


def render_report(context: dict) -> str:
    group_lines = []
    for group in context["groups"]:
        names = "、".join(f"`{item['name']}`" for item in group["items"])
        group_lines.append(f"- `{group['category']}`：{group['count']}。{names}")
    category_only = "、".join(
        f"`{item['category']}`（行 {item['row']}）" for item in context["category_only"]
    )
    empty_rows = "、".join(str(row) for row in context["empty_rows"]) or "无"
    description_names = "\n".join(
        f"{index}. `{name}`" for index, (_row, name) in enumerate(context["description_entries"], start=1)
    )
    can_lines = "\n".join(
        f"| {cell(row, 'J')} | {cell(row, 'K')} |" for row in context["can_data"]
    )
    note_lines = []
    for row in context["worksheet_rows"]:
        if row["row"] == 1:
            continue
        note = cell(row, "E")
        side = cell(row, "C")
        if nonempty(note) or nonempty(side):
            note_lines.append(
                f"| {row['row']} | {cell(row, 'B')} | {side} | {note} |"
            )
    notes = "\n".join(note_lines) if note_lines else "| — | — | — | — |"
    duplicate_text = "、".join(f"`{name}`" for name in context["duplicate_descriptions"]) or "无"
    blank_explanations = context["blank_explanations"]
    blank_text = "、".join(f"`{name}`" for _row, name in blank_explanations) or "无"
    return f"""# Excel 三表导入核对

本报告由 `tools/catalog-import/import_excel_catalog.py` 根据工作簿单元格生成。导入器没有修改拼写，没有合并别名，也没有把 CAN 名称写入 `Can_Msg[72]`。

## 来源

| 项 | 值 |
|---|---|
| 工作簿 | `docs/baseline/requirements/ERNC优化.xlsx` |
| SHA-256 | `{context["sha256"]}` |
| 工作表 | `工作表1`、`ERNC参数说明`、`can_msg信息` |

`docs/baseline/` 被本克隆的 `.git/info/exclude` 排除，工作簿本身不进入 Git。上面的哈希钉住本次抽取所用的文件。

## 计数

| 检查 | 期望 | 实际 |
|---|---:|---:|
| 分组项（B 列非空） | {EXPECTED_GROUPED} | {len(context["items"])} |
| 参数说明 | {EXPECTED_DESCRIPTIONS} | {len(context["description_data"])} |
| CAN 名称 | {EXPECTED_CAN} | {len(context["can_data"])} |
| 参数说明列非空 | {EXPECTED_DESCRIPTIONS} | {context["explanation_count"]} |
| 备注列非空 | 0 | {context["remark_count"]} |

分组计数：

{chr(10).join(group_lines)}

没有参数名的分类行：{category_only}。这些行保留在 `categories.raw.json`，不计入 94。

工作表 1 中单元格全空的行：{empty_rows}。这些行不计入 94。

参数说明里的重复名称：{duplicate_text}。说明为空的名称：{blank_text}。

## 抽取规则

- `categories.raw.json`、`descriptions.raw.json`、`can_names.raw.json` 按单元格原文字母列保存。空分类格保持为空。
- `categories.grouped.json` 是派生视图。从第 2 行起，遇到非空 A 列就更新当前分类；B 列去掉首尾空白后仍非空的行记为一个分组项。填充结果不写回原始行。
- `can_msg信息` 没有表头。非空内容只在 J 列和 K 列，J 列原文是 `1` 到 `36`。工作簿里已存在的空单元格仍写入 `can_names.raw.json`。
- C 列和 E 列的句子只归档，不换算成时间、阈值或写入策略。

## 只出现在一张表中的名称

这些字符串按单元格原文保留。

### 只在工作表 1

{bullet_names(context["only_worksheet"], "工作表1")}
### 只在 ERNC参数说明

{bullet_names(context["only_descriptions"], "ERNC参数说明")}
## 参数说明名称顺序

{description_names}

逐字说明和备注在 `descriptions.raw.json`。

## CAN 名称

| J | K |
|---:|---|
{can_lines}

## 工作表 1 的 C 列和 E 列原文

| 行 | B | C | E |
|---:|---|---|---|
{notes}
"""


def build(workbook: Path, output: Path) -> dict:
    if not workbook.is_file():
        raise ImportError(f"workbook not found: {workbook}")
    digest = hashlib.sha256(workbook.read_bytes()).hexdigest()
    with zipfile.ZipFile(workbook) as archive:
        strings = shared_strings(archive)
        targets = require_sheets(workbook_sheets(archive))
        worksheet_rows = sheet_rows(archive, targets["工作表1"], strings)
        description_sheet = sheet_rows(archive, targets["ERNC参数说明"], strings)
        can_sheet = sheet_rows(archive, targets["can_msg信息"], strings)

    items, category_only, empty_rows = group_worksheet(worksheet_rows)
    descriptions = description_rows(description_sheet)
    can_data = can_rows(can_sheet)

    groups = []
    for item in items:
        if not groups or groups[-1]["category"] != item["category"]:
            groups.append({"category": item["category"], "count": 0, "items": []})
        groups[-1]["items"].append({"row": item["row"], "name": item["name"]})
        groups[-1]["count"] += 1

    description_entries = [(row["row"], cell(row, "A")) for row in descriptions]
    worksheet_entries = [(item["row"], item["name"]) for item in items]
    description_name_set = {name for _row, name in description_entries}
    worksheet_name_set = {name for _row, name in worksheet_entries}
    blank_explanations = [
        (row["row"], cell(row, "A"))
        for row in descriptions
        if not nonempty(cell(row, "B"))
    ]
    remark_count = sum(1 for row in descriptions if nonempty(cell(row, "C")))
    explanation_count = sum(1 for row in descriptions if nonempty(cell(row, "B")))

    context = {
        "sha256": digest,
        "worksheet_rows": worksheet_rows,
        "items": items,
        "groups": groups,
        "category_only": category_only,
        "empty_rows": empty_rows,
        "description_data": descriptions,
        "description_entries": description_entries,
        "can_data": can_data,
        "only_worksheet": names_only(worksheet_entries, description_name_set),
        "only_descriptions": names_only(description_entries, worksheet_name_set),
        "duplicate_descriptions": duplicate_names(description_entries),
        "blank_explanations": blank_explanations,
        "remark_count": remark_count,
        "explanation_count": explanation_count,
    }
    if explanation_count != EXPECTED_DESCRIPTIONS or remark_count != 0:
        raise ImportError(
            f"description text counts are explanations={explanation_count}, remarks={remark_count}"
        )
    if context["duplicate_descriptions"]:
        raise ImportError(f"duplicate descriptions: {context['duplicate_descriptions']}")

    output.mkdir(parents=True, exist_ok=True)
    write_json(
        output / "categories.raw.json",
        {"kind": "raw", "sheet": "工作表1", "rows": worksheet_rows},
    )
    write_json(
        output / "descriptions.raw.json",
        {
            "kind": "raw",
            "sheet": "ERNC参数说明",
            "header": {"A": "参数名称", "B": "参数说明", "C": "备注"},
            "rows": description_sheet,
        },
    )
    write_json(
        output / "can_names.raw.json",
        {
            "kind": "raw",
            "sheet": "can_msg信息",
            "header": None,
            "rows": can_sheet,
        },
    )
    write_json(
        output / "categories.grouped.json",
        {
            "kind": "derived",
            "sheet": "工作表1",
            "rule": "从第 2 行起，遇到非空 A 列就更新当前分类；B 列去掉首尾空白后仍非空的行计为一个分组项。填充结果不写回原始行。",
            "groups": groups,
            "category_rows_without_name": category_only,
        },
    )
    manifest = {
        "source": {
            "path": "docs/baseline/requirements/ERNC优化.xlsx",
            "sha256": digest,
            "tracked": False,
        },
        "counts": {
            "grouped_items": len(items),
            "descriptions": len(descriptions),
            "can_names": len(can_data),
        },
        "files": [
            "categories.raw.json",
            "categories.grouped.json",
            "descriptions.raw.json",
            "can_names.raw.json",
            "IMPORT-REPORT.md",
        ],
    }
    write_json(output / "manifest.json", manifest)
    (output / "IMPORT-REPORT.md").write_text(render_report(context), encoding="utf-8", newline="\n")
    verify_written(output, digest)
    return manifest


def verify_written(output: Path, digest: str) -> None:
    grouped = json.loads((output / "categories.grouped.json").read_text(encoding="utf-8"))
    descriptions = json.loads((output / "descriptions.raw.json").read_text(encoding="utf-8"))
    can_names = json.loads((output / "can_names.raw.json").read_text(encoding="utf-8"))
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    grouped_count = sum(group["count"] for group in grouped["groups"])
    description_count = sum(
        1
        for row in descriptions["rows"]
        if row["row"] != 1 and nonempty(row["cells"].get("A", ""))
    )
    can_count = sum(
        1
        for row in can_names["rows"]
        if nonempty(row["cells"].get("J", "")) and nonempty(row["cells"].get("K", ""))
    )
    if grouped_count != EXPECTED_GROUPED or description_count != EXPECTED_DESCRIPTIONS or can_count != EXPECTED_CAN:
        raise ImportError(
            f"rewritten counts are grouped={grouped_count}, descriptions={description_count}, can={can_count}"
        )
    if manifest["source"]["sha256"] != digest:
        raise ImportError("manifest hash does not match the workbook")
    if grouped["kind"] != "derived" or descriptions["kind"] != "raw":
        raise ImportError("raw and derived files were swapped")


def parse_args(argv: list[str]) -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description="Import ERNC优化.xlsx into catalog/source")
    parser.add_argument(
        "--workbook",
        type=Path,
        default=root / "docs" / "baseline" / "requirements" / "ERNC优化.xlsx",
    )
    parser.add_argument("--output", type=Path, default=root / "catalog" / "source")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        manifest = build(args.workbook.resolve(), args.output.resolve())
    except (ImportError, OSError, KeyError, ET.ParseError, zipfile.BadZipFile, ValueError) as exc:
        print(f"import failed: {exc}", file=sys.stderr)
        return 1
    counts = manifest["counts"]
    print(
        "import ok "
        f"grouped={counts['grouped_items']} "
        f"descriptions={counts['descriptions']} "
        f"can={counts['can_names']} "
        f"sha256={manifest['source']['sha256']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
