# -*- coding: utf-8 -*-
"""Check the 6MIC layout manifest against a 4-byte sequential layout.

The firmware rule used here is the one confirmed for ES-008: fields are
4-byte aligned and packed in struct declaration order from address 0.
Offsets come from param/param.h at CHNB-NEW-6MIC fbecac9. This tool does
not compile the header and does not grant device writes.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import sys
from pathlib import Path

SCALAR_SIZE = {"float": 4, "int32_t": 4, "uint32_t": 4}
ALIGNMENT = 4
ROOT_STRUCT = "ERNCUppInit_Para"
SOURCE_REVISION = "fbecac9f54d378d212794fd937e950d9d2acd58d"

UNRESOLVED = [
    {
        "name": "OrderFour_Flag",
        "status": "unresolved",
        "writable": False,
        "reason": "ENC_order 只声明了 OrderOne_Flag 到 OrderThree_Flag。ENC_params 里的 TachofAmp4 等数组有布局，这个控制名没有。",
    },
    {
        "name": "OrderFive_Flag",
        "status": "unresolved",
        "writable": False,
        "reason": "ENC_order 没有 OrderFive_Flag。ENC_params 里的 5 阶系数数组有布局，这个控制名没有。",
    },
    {
        "name": "Can_Msg slot map",
        "status": "unresolved",
        "writable": False,
        "reason": "Can_Msg 是 float[72]，起始偏移由结构顺序确定。Excel 的 36 个名称没有对应到这些槽位。",
    },
]


class LayoutError(Exception):
    """The manifest or header does not satisfy the pack-4 rule."""


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*?$", "", text, flags=re.M)


def apply_preprocessor(text: str) -> tuple[str, dict[str, str]]:
    defines: dict[str, str] = {}
    active = [True]
    kept: list[str] = []
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("#"):
            if not active[-1] and not line.startswith(("#ifdef", "#ifndef", "#else", "#endif")):
                continue
            if line.startswith("#define") and active[-1]:
                parts = line.split(None, 2)
                defines[parts[1]] = parts[2].strip() if len(parts) > 2 else "1"
            elif line.startswith("#ifdef"):
                active.append(active[-1] and line.split()[1] in defines)
            elif line.startswith("#ifndef"):
                active.append(active[-1] and line.split()[1] not in defines)
            elif line.startswith("#else"):
                parent = active[-2] if len(active) > 1 else True
                active[-1] = parent and not active[-1]
            elif line.startswith("#endif"):
                if len(active) == 1:
                    raise LayoutError("unbalanced #endif")
                active.pop()
            continue
        if active[-1]:
            kept.append(raw)
    if len(active) != 1:
        raise LayoutError("unbalanced preprocessor condition")
    return "\n".join(kept), defines


def eval_expr(expr: str, defines: dict[str, str], seen: set[str] | None = None) -> int:
    if seen is None:
        seen = set()

    def replace(match: re.Match[str]) -> str:
        name = match.group(0)
        if name in seen or name not in defines:
            raise LayoutError(f"cannot resolve layout expression {expr!r}")
        seen.add(name)
        return str(eval_expr(defines[name], defines, seen))

    expanded = re.sub(r"\b[A-Za-z_][A-Za-z0-9_]*\b", replace, expr)
    expanded = re.sub(r"(\d+)[uUlL]+", r"\1", expanded)
    if not re.fullmatch(r"[0-9+\-*/() ]+", expanded):
        raise LayoutError(f"unsupported layout expression {expr!r}")
    return int(eval(expanded, {"__builtins__": {}}, {}))


def parse_shape(brackets: str, defines: dict[str, str]) -> list[int]:
    return [eval_expr(item, defines) for item in re.findall(r"\[([^\]]+)\]", brackets)]


def parse_structs(text: str, defines: dict[str, str]) -> dict[str, list[dict]]:
    structs: dict[str, list[dict]] = {}
    for body, name in re.findall(r"typedef\s+struct\s*\{(.*?)\}\s*(\w+)\s*;", text, flags=re.S):
        members = []
        for statement in body.split(";"):
            field = " ".join(statement.split())
            if not field:
                continue
            match = re.fullmatch(
                r"(float|int32_t|uint32_t|[A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*((?:\[[^\]]+\])*)",
                field,
            )
            if not match:
                raise LayoutError(f"unparsed field in {name}: {field}")
            ctype, member_name, brackets = match.groups()
            members.append(
                {
                    "name": member_name,
                    "type": ctype,
                    "shape": parse_shape(brackets, defines),
                }
            )
        structs[name] = members
    if ROOT_STRUCT not in structs:
        raise LayoutError(f"missing {ROOT_STRUCT}")
    return structs


def element_count(shape: list[int]) -> int:
    count = 1
    for item in shape:
        if item <= 0:
            raise LayoutError(f"invalid dimension {item}")
        count *= item
    return count


def member_size(member: dict, structs: dict[str, list[dict]]) -> int:
    if member["type"] in SCALAR_SIZE:
        base = SCALAR_SIZE[member["type"]]
    elif member["type"] in structs:
        if member["shape"]:
            raise LayoutError(f"array of struct is not used: {member['name']}")
        base = struct_size(member["type"], structs)
    else:
        raise LayoutError(f"unknown type {member['type']}")
    return base * element_count(member["shape"])


def struct_size(name: str, structs: dict[str, list[dict]]) -> int:
    return sum(member_size(member, structs) for member in structs[name])


def emit_struct(name: str, base: int, prefix: str, structs: dict[str, list[dict]]) -> list[dict]:
    nodes = []
    cursor = base
    for member in structs[name]:
        size = member_size(member, structs)
        path = f"{prefix}.{member['name']}" if prefix else member["name"]
        node = {
            "path": path,
            "offset": cursor,
            "size": size,
            "slot": cursor // ALIGNMENT,
            "slot_count": size // ALIGNMENT,
            "type": member["type"],
            "shape": member["shape"],
            "status": "pack4-sequential",
            "writable": False,
        }
        if member["type"] in structs:
            node["children"] = emit_struct(member["type"], cursor, path, structs)
        nodes.append(node)
        cursor += size
    return nodes


def build_manifest(header_text: str, header_sha256: str) -> dict:
    cleaned = strip_comments(header_text)
    body, defines = apply_preprocessor(cleaned)
    structs = parse_structs(body, defines)
    fields = emit_struct(ROOT_STRUCT, 0, "", structs)
    total = struct_size(ROOT_STRUCT, structs)
    paths = collect_paths(fields)
    for item in UNRESOLVED:
        if item["name"] in paths:
            raise LayoutError(f"{item['name']} unexpectedly has a layout path")
    return {
        "profile": "6mic",
        "alignment": ALIGNMENT,
        "base": 0,
        "status": "pack4-sequential",
        "writable": False,
        "total_size": total,
        "total_slots": total // ALIGNMENT,
        "evidence": {
            "kind": "pack4-sequential",
            "header": "param/param.h",
            "struct": ROOT_STRUCT,
            "repository": "SamhandsomeLee/CHNB",
            "branch": "CHNB-NEW-6MIC",
            "revision": SOURCE_REVISION,
            "header_sha256": header_sha256,
            "rule": "四字节对齐，从地址 0 按结构体声明顺序连续排列。",
        },
        "landmarks": {
            "ANC_OFF": 0,
            "Step": 28,
            "check_data": 2672,
            "Can_Msg": 59476,
            "ERNC_TF": 59764,
            "init_param_flag": 123604,
        },
        "unresolved": UNRESOLVED,
        "fields": fields,
    }


def collect_paths(nodes: list[dict]) -> set[str]:
    found = set()
    for node in nodes:
        found.add(node["path"])
        found.update(collect_paths(node.get("children", [])))
    return found


def find_path(nodes: list[dict], path: str) -> dict | None:
    for node in nodes:
        if node["path"] == path:
            return node
        found = find_path(node.get("children", []), path)
        if found is not None:
            return found
    return None


def check_node(node: dict, cursor: int) -> int:
    path = node.get("path", "<missing path>")
    if node.get("writable") is not False:
        raise LayoutError(f"{path} is writable")
    if node.get("status") != "pack4-sequential":
        raise LayoutError(f"{path} status is not pack4-sequential")
    if node.get("offset") != cursor:
        raise LayoutError(f"{path} offset {node.get('offset')} != {cursor}")
    if node.get("slot") != cursor // ALIGNMENT:
        raise LayoutError(f"{path} slot does not match offset")
    size = node.get("size")
    if not isinstance(size, int) or size <= 0 or size % ALIGNMENT != 0:
        raise LayoutError(f"{path} size is not a positive multiple of 4")
    if node.get("slot_count") != size // ALIGNMENT:
        raise LayoutError(f"{path} slot_count does not match size")
    shape = node.get("shape")
    if not isinstance(shape, list):
        raise LayoutError(f"{path} shape is missing")
    children = node.get("children", [])
    if children:
        if shape:
            raise LayoutError(f"{path} combines a struct with an array shape")
        child_cursor = cursor
        for child in children:
            child_cursor = check_node(child, child_cursor)
        if child_cursor - cursor != size:
            raise LayoutError(f"{path} children do not fill its size")
    else:
        if node.get("type") not in SCALAR_SIZE:
            raise LayoutError(f"{path} has no scalar type")
        expect = SCALAR_SIZE[node["type"]] * element_count(shape)
        if size != expect:
            raise LayoutError(f"{path} size {size} != {expect}")
    return cursor + size


def check_manifest(manifest: dict) -> None:
    if manifest.get("writable") is not False:
        raise LayoutError("manifest writable must be false")
    if manifest.get("alignment") != ALIGNMENT or manifest.get("base") != 0:
        raise LayoutError("manifest alignment or base is not the pack-4 rule")
    fields = manifest.get("fields")
    if not isinstance(fields, list) or not fields:
        raise LayoutError("manifest has no fields")
    end = 0
    for node in fields:
        end = check_node(node, end)
    if end != manifest.get("total_size"):
        raise LayoutError(f"total_size {manifest.get('total_size')} != walked {end}")
    if manifest.get("total_slots") != end // ALIGNMENT:
        raise LayoutError("total_slots does not match total_size")
    for path, offset in manifest.get("landmarks", {}).items():
        node = find_path(fields, path)
        if node is None or node["offset"] != offset:
            raise LayoutError(f"landmark {path} is not at offset {offset}")
    if find_path(fields, "init_param_flag")["offset"] != end - ALIGNMENT:
        raise LayoutError("init_param_flag is not the last 4 bytes")
    paths = collect_paths(fields)
    for item in manifest.get("unresolved", []):
        if item.get("status") != "unresolved" or item.get("writable") is not False:
            raise LayoutError(f"unresolved item is not blocked: {item.get('name')}")
        if item.get("name") in paths:
            raise LayoutError(f"unresolved name has a layout path: {item.get('name')}")
    required = {"OrderFour_Flag", "OrderFive_Flag", "Can_Msg slot map"}
    present = {item.get("name") for item in manifest.get("unresolved", [])}
    if not required.issubset(present):
        raise LayoutError(f"missing unresolved entries: {sorted(required - present)}")
    can_msg = find_path(fields, "Can_Msg")
    if can_msg is None or can_msg["shape"] != [72] or can_msg["type"] != "float":
        raise LayoutError("Can_Msg is not float[72]")


def load_manifest(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_manifest(path: Path, manifest: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def self_test(manifest: dict) -> None:
    shifted = copy.deepcopy(manifest)
    shifted["fields"][0]["offset"] = ALIGNMENT
    try:
        check_manifest(shifted)
    except LayoutError:
        pass
    else:
        raise LayoutError("self-test did not reject a shifted offset")

    writable = copy.deepcopy(manifest)
    writable["fields"][0]["writable"] = True
    try:
        check_manifest(writable)
    except LayoutError:
        pass
    else:
        raise LayoutError("self-test did not reject a writable field")


def parse_args(argv: list[str]) -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description="Check the pack-4 firmware layout manifest")
    parser.add_argument("--manifest", type=Path, default=root / "firmware" / "layout_manifest.json")
    parser.add_argument("--header", type=Path)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        if args.write:
            if args.header is None:
                raise LayoutError("--write requires --header")
            header_bytes = args.header.read_bytes()
            manifest = build_manifest(header_bytes.decode("utf-8"), hashlib.sha256(header_bytes).hexdigest())
            check_manifest(manifest)
            write_manifest(args.manifest, manifest)
        manifest = load_manifest(args.manifest)
        check_manifest(manifest)
        if args.header is not None:
            header_bytes = args.header.read_bytes()
            rebuilt = build_manifest(header_bytes.decode("utf-8"), hashlib.sha256(header_bytes).hexdigest())
            if rebuilt["fields"] != manifest["fields"] or rebuilt["total_size"] != manifest["total_size"]:
                raise LayoutError("manifest does not match the header")
            if rebuilt["evidence"]["header_sha256"] != manifest["evidence"]["header_sha256"]:
                raise LayoutError("manifest header hash does not match the header file")
        if args.self_test:
            self_test(manifest)
    except (LayoutError, OSError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
        print(f"layout check failed: {exc}", file=sys.stderr)
        return 1
    print(
        "layout ok "
        f"size={manifest['total_size']} "
        f"slots={manifest['total_slots']} "
        f"writable=false"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
