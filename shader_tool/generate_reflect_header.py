#!/usr/bin/env python3
"""Generate C++ shader-layout structs from GLSL source plus glslang reflection.

Usage:
    python generate_reflect_header.py shader.frag shader.frag.reflect output.hpp
    python generate_reflect_header.py shader.frag shader.frag.reflect output.hpp \
        --namespace PbrFrag --include-dir ../common

The GLSL source supplies the original structure, type, member, block, and array
names. The glslangValidator -q output supplies byte offsets, strides, and sizes.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


CPP_TYPES = {
    "float": ("float", 4),
    "int": ("std::int32_t", 4),
    "uint": ("std::uint32_t", 4),
    "bool": ("std::uint32_t", 4),
    "vec2": ("std::array<float, 2>", 8),
    "vec3": ("std::array<float, 3>", 12),
    "vec4": ("std::array<float, 4>", 16),
    "ivec2": ("std::array<std::int32_t, 2>", 8),
    "ivec3": ("std::array<std::int32_t, 3>", 12),
    "ivec4": ("std::array<std::int32_t, 4>", 16),
    "uvec2": ("std::array<std::uint32_t, 2>", 8),
    "uvec3": ("std::array<std::uint32_t, 3>", 12),
    "uvec4": ("std::array<std::uint32_t, 4>", 16),
    "mat2": ("std::array<float, 4>", 16),
    "mat3": ("std::array<float, 9>", 36),
    "mat4": ("std::array<float, 16>", 64),
}

REFLECT_TYPE_SIZE = {
    0x1404: 4, 0x1405: 4, 0x1406: 4,
    0x8B50: 8, 0x8B51: 12, 0x8B52: 16,
    0x8B53: 8, 0x8B54: 12, 0x8B55: 16,
    0x8DC6: 8, 0x8DC7: 12, 0x8DC8: 16,
    0x8B5A: 16, 0x8B5B: 36, 0x8B5C: 64,
}

ENTRY_RE = re.compile(
    r"^(?P<name>[^:]+):\s*offset\s+(?P<offset>-?\d+),\s*"
    r"type\s+(?P<type>[0-9a-fA-F]+),\s*size\s+(?P<size>\d+),\s*"
    r"index\s+(?P<index>-?\d+),\s*binding\s+(?P<binding>-?\d+),\s*"
    r"stages\s+(?P<stages>\d+)(?P<extra>.*)$"
)
BLOCK_RE = re.compile(
    r"^(?P<name>[^:]+):.*?\bsize\s+(?P<size>\d+),.*?"
    r"\bindex\s+(?P<index>-?\d+),.*?\bbinding\s+(?P<binding>-?\d+),"
)


@dataclass
class GlslField:
    type_name: str
    name: str
    array_expr: Optional[str] = None
    array_count: Optional[int] = None
    runtime_array: bool = False


@dataclass
class GlslStruct:
    name: str
    fields: list[GlslField]


@dataclass
class GlslBlock:
    name: str
    storage: str
    layout: str
    fields: list[GlslField]
    instance_name: Optional[str]
    instance_array: bool
    set_number: Optional[int]
    binding: Optional[int]


@dataclass
class RefEntry:
    name: str
    offset: int
    type_code: int
    count: int
    index: int
    binding: int
    array_stride: Optional[int]
    top_stride: Optional[int]


@dataclass
class RefBlock:
    name: str
    size: int
    index: int
    binding: int


@dataclass
class OutField:
    name: str
    cpp_type: str
    offset: int
    size: int


@dataclass
class OutStruct:
    name: str
    size: int
    fields: list[OutField] = field(default_factory=list)
    metadata: list[str] = field(default_factory=list)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*?$", "", text, flags=re.M)
    return text


def expand_includes(path: Path, include_dirs: list[Path], seen: set[Path]) -> str:
    path = path.resolve()
    if path in seen:
        return ""
    seen.add(path)
    text = path.read_text(encoding="utf-8")
    output = []
    for line in text.splitlines():
        m = re.match(r'\s*#\s*include\s*["<]([^">]+)[">]', line)
        if not m:
            output.append(line)
            continue
        name = m.group(1)
        candidates = [path.parent / name] + [d / name for d in include_dirs]
        target = next((p for p in candidates if p.exists()), None)
        if target:
            output.append(expand_includes(target, include_dirs, seen))
        # Missing includes are ignored because layout declarations may still parse.
    return "\n".join(output)


def parse_constants(text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for name, value in re.findall(
        r"^\s*#\s*define\s+([A-Za-z_]\w*)\s+([0-9]+)\b", text, re.M
    ):
        values[name] = int(value)
    for name, value in re.findall(
        r"\bconst\s+(?:u?int)\s+([A-Za-z_]\w*)\s*=\s*([0-9]+)\s*;",
        text,
    ):
        values[name] = int(value)
    return values


def parse_fields(body: str, constants: dict[str, int]) -> list[GlslField]:
    fields: list[GlslField] = []
    for statement in body.split(";"):
        statement = statement.strip()
        if not statement:
            continue
        statement = re.sub(r"\blayout\s*\([^)]*\)", "", statement).strip()
        # Ignore precision/storage/member qualifiers.
        statement = re.sub(
            r"^(?:(?:readonly|writeonly|coherent|volatile|restrict|const|"
            r"highp|mediump|lowp)\s+)+", "", statement
        )
        m = re.fullmatch(
            r"([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*"
            r"(?:\[\s*([A-Za-z_]\w*|[0-9]+)?\s*\])?",
            statement,
        )
        if not m:
            continue
        type_name, name, array_expr = m.groups()
        has_brackets = "[" in statement
        runtime = has_brackets and array_expr is None
        count = None
        if array_expr:
            count = int(array_expr) if array_expr.isdigit() else constants.get(array_expr)
            if count is None:
                raise ValueError(
                    f"Cannot resolve array length {array_expr!r} for {name!r}"
                )
        fields.append(GlslField(type_name, name, array_expr, count, runtime))
    return fields


def parse_glsl(text: str) -> tuple[dict[str, GlslStruct], list[GlslBlock]]:
    constants = parse_constants(text)
    clean = strip_comments(text)

    structs: dict[str, GlslStruct] = {}
    for m in re.finditer(r"\bstruct\s+([A-Za-z_]\w*)\s*\{(.*?)\}\s*;", clean, re.S):
        name, body = m.groups()
        structs[name] = GlslStruct(name, parse_fields(body, constants))

    # Remove ordinary struct definitions before searching interface blocks.
    without_structs = re.sub(
        r"\bstruct\s+[A-Za-z_]\w*\s*\{.*?\}\s*;", "", clean, flags=re.S
    )

    block_re = re.compile(
        r"layout\s*\((?P<layout>[^)]*)\)\s*"
        r"(?:(?:readonly|writeonly|coherent|volatile|restrict)\s+)*"
        r"(?P<storage>uniform|buffer)\s+"
        r"(?:(?:readonly|writeonly|coherent|volatile|restrict)\s+)*"
        r"(?P<name>[A-Za-z_]\w*)\s*"
        r"\{(?P<body>.*?)\}\s*"
        r"(?P<instance>[A-Za-z_]\w*)?\s*"
        r"(?P<array>\[\s*\])?\s*;",
        re.S,
    )

    blocks: list[GlslBlock] = []
    for m in block_re.finditer(without_structs):
        layout = m.group("layout")
        set_m = re.search(r"\bset\s*=\s*(\d+)", layout)
        binding_m = re.search(r"\bbinding\s*=\s*(\d+)", layout)
        blocks.append(GlslBlock(
            name=m.group("name"),
            storage=m.group("storage"),
            layout=layout,
            fields=parse_fields(m.group("body"), constants),
            instance_name=m.group("instance"),
            instance_array=m.group("array") is not None,
            set_number=int(set_m.group(1)) if set_m else None,
            binding=int(binding_m.group(1)) if binding_m else None,
        ))
    return structs, blocks


def extra_int(extra: str, key: str) -> Optional[int]:
    m = re.search(rf"(?:^|,\s*){re.escape(key)}\s+(\d+)", extra)
    return int(m.group(1)) if m else None


def parse_reflection(text: str) -> tuple[list[RefEntry], dict[str, RefBlock]]:
    section = None
    entries: list[RefEntry] = []
    blocks: dict[str, RefBlock] = {}

    for raw in text.splitlines():
        line = raw.strip()
        if line == "Uniform reflection:":
            section = "uniform"
            continue
        if line == "Uniform block reflection:":
            section = "block"
            continue
        if line.endswith("reflection:"):
            section = None
            continue
        if not line:
            continue
        if section == "uniform":
            m = ENTRY_RE.match(line)
            if not m:
                continue
            offset = int(m.group("offset"))
            if offset < 0:
                continue
            entries.append(RefEntry(
                name=m.group("name"),
                offset=offset,
                type_code=int(m.group("type"), 16),
                count=int(m.group("size")),
                index=int(m.group("index")),
                binding=int(m.group("binding")),
                array_stride=extra_int(m.group("extra"), "arrayStride"),
                top_stride=extra_int(m.group("extra"), "topLevelArrayStride"),
            ))
        elif section == "block":
            m = BLOCK_RE.match(line)
            if m:
                blocks[m.group("name")] = RefBlock(
                    m.group("name"), int(m.group("size")),
                    int(m.group("index")), int(m.group("binding"))
                )
    return entries, blocks


def natural_size(type_name: str) -> int:
    try:
        return CPP_TYPES[type_name][1]
    except KeyError:
        raise ValueError(f"Unsupported GLSL scalar/vector/matrix type: {type_name}")


def cpp_type(type_name: str) -> str:
    return CPP_TYPES.get(type_name, (type_name, 0))[0]


def round_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


class LayoutBuilder:
    def __init__(
        self,
        structs: dict[str, GlslStruct],
        blocks: list[GlslBlock],
        entries: list[RefEntry],
        ref_blocks: dict[str, RefBlock],
    ):
        self.glsl_structs = structs
        self.blocks = blocks
        self.entries = entries
        self.ref_blocks = ref_blocks
        self.outputs: dict[str, OutStruct] = {}
        self.order: list[str] = []

    def entries_for_block(self, block: GlslBlock) -> list[RefEntry]:
        prefixes = [block.name + "."]
        # SSBO reflection commonly uses the runtime-array member name directly.
        prefixes.extend(f.name + "." for f in block.fields if f.runtime_array)
        prefixes.extend(f.name + "[0]." for f in block.fields if f.runtime_array)

        raw_result = [
            e for e in self.entries
            if any(e.name.startswith(prefix) for prefix in prefixes)
        ]

        # A direct scalar runtime array can be reflected without qualification.
        runtime_names = {f.name for f in block.fields if f.runtime_array}
        raw_result.extend(e for e in self.entries if e.name in runtime_names)

        # Convert block-qualified UBO/push-constant names to paths local to the
        # source block. SSBO paths already begin with the runtime-array member.
        result: list[RefEntry] = []
        for e in raw_result:
            local_name = e.name
            block_prefix = block.name + "."
            if local_name.startswith(block_prefix):
                local_name = local_name[len(block_prefix):]
            result.append(RefEntry(
                name=local_name,
                offset=e.offset,
                type_code=e.type_code,
                count=e.count,
                index=e.index,
                binding=e.binding,
                array_stride=e.array_stride,
                top_stride=e.top_stride,
            ))

        # Deduplicate explicit [0] entries and active aliases by offset/type/count.
        chosen: dict[tuple[int, int, int], RefEntry] = {}
        for e in result:
            key = (e.offset, e.type_code, e.count)
            old = chosen.get(key)
            if old is None or ("[0]" in e.name and "[0]" not in old.name):
                chosen[key] = e
        return sorted(chosen.values(), key=lambda e: (e.offset, e.name))

    def local_path(self, block: GlslBlock, entry: RefEntry) -> str:
        if entry.name.startswith(block.name + "."):
            return entry.name[len(block.name) + 1:]
        return entry.name

    def matching(
        self, block_entries: list[RefEntry], prefix: str
    ) -> list[RefEntry]:
        def normalize(name: str) -> str:
            return re.sub(r"\[\d+\]", "", name)
        target = normalize(prefix)
        return [e for e in block_entries if normalize(e.name).startswith(target)]

    def field_start(
        self, block: GlslBlock, block_entries: list[RefEntry],
        prefix: str, field: GlslField
    ) -> int:
        full = prefix + field.name
        matches = self.matching(block_entries, full)
        if not matches:
            raise ValueError(
                f"No reflection entry found for {block.name}.{full}"
            )
        return min(e.offset for e in matches)

    def reflected_leaf_size(self, entry: RefEntry) -> int:
        base = REFLECT_TYPE_SIZE.get(entry.type_code)
        if base is None:
            raise ValueError(
                f"Unsupported reflected type 0x{entry.type_code:x}: {entry.name}"
            )
        if entry.count > 1:
            return (entry.array_stride or base) * entry.count
        return base

    def build_named_struct(
        self,
        type_name: str,
        fields: list[GlslField],
        block: GlslBlock,
        block_entries: list[RefEntry],
        prefix: str,
        base_offset: int,
        container_end: int,
    ) -> OutStruct:
        starts = [
            self.field_start(block, block_entries, prefix, f) for f in fields
        ]
        out_fields: list[OutField] = []

        for i, f in enumerate(fields):
            start = starts[i]
            end = starts[i + 1] if i + 1 < len(starts) else container_end
            full = prefix + f.name

            if f.runtime_array:
                raise ValueError(
                    f"Runtime array {full} must be handled as a block member"
                )

            if f.type_name in self.glsl_structs:
                nested_decl = self.glsl_structs[f.type_name]
                if f.array_count is not None:
                    matches = self.matching(block_entries, full)
                    strides = {e.top_stride for e in matches if e.top_stride}
                    if len(strides) != 1:
                        raise ValueError(
                            f"Cannot determine reflected stride for {full}"
                        )
                    stride = next(iter(strides))
                    self.build_named_struct(
                        f.type_name, nested_decl.fields, block, block_entries,
                        full + "[0].", start, start + stride
                    )
                    size = stride * f.array_count
                    ctype = f"std::array<{f.type_name}, {f.array_count}>"
                else:
                    self.build_named_struct(
                        f.type_name, nested_decl.fields, block, block_entries,
                        full + ".", start, end
                    )
                    size = end - start
                    ctype = f.type_name
            else:
                ctype = cpp_type(f.type_name)
                matches = self.matching(block_entries, full)
                exact = min(matches, key=lambda e: (e.offset, e.name))
                if f.array_count is not None:
                    stride = exact.array_stride or natural_size(f.type_name)
                    elem_cpp = ctype
                    if stride != natural_size(f.type_name):
                        elem_cpp = f"ShaderStridedElement<{ctype}, {stride}>"
                    ctype = f"std::array<{elem_cpp}, {f.array_count}>"
                    size = stride * f.array_count
                else:
                    size = natural_size(f.type_name)

            out_fields.append(OutField(f.name, ctype, start - base_offset, size))

        size = container_end - base_offset
        current = self.outputs.get(type_name)
        candidate = OutStruct(type_name, size, out_fields)
        if current is None:
            self.outputs[type_name] = candidate
            self.order.append(type_name)
        else:
            # The same GLSL struct may be used more than once. Require identical layout.
            sig_a = [(f.name, f.cpp_type, f.offset, f.size) for f in current.fields]
            sig_b = [(f.name, f.cpp_type, f.offset, f.size) for f in candidate.fields]
            if current.size != size or sig_a != sig_b:
                raise ValueError(f"Inconsistent reflected layouts for struct {type_name}")
        return self.outputs[type_name]

    def infer_block_size(
        self, block: GlslBlock, entries: list[RefEntry]
    ) -> int:
        summary = self.ref_blocks.get(block.name)
        if summary and summary.size > 0:
            return summary.size
        if not entries:
            return 0
        end = max(e.offset + self.reflected_leaf_size(e) for e in entries)
        # Descriptor arrays of uniform blocks use a std140-like 16-byte block stride.
        if block.storage == "uniform" and block.instance_array:
            end = round_up(end, 16)
        return end

    def build(self) -> list[OutStruct]:
        for block in self.blocks:
            entries = self.entries_for_block(block)
            if not entries:
                continue

            runtime_fields = [f for f in block.fields if f.runtime_array]
            if runtime_fields:
                if len(block.fields) != 1 or len(runtime_fields) != 1:
                    raise ValueError(
                        f"Only a single trailing runtime array is supported in {block.name}"
                    )
                f = runtime_fields[0]
                stride_values = {e.top_stride for e in entries if e.top_stride}
                if len(stride_values) == 1:
                    stride = next(iter(stride_values))
                else:
                    stride = max(
                        e.offset + self.reflected_leaf_size(e) for e in entries
                    )

                metadata = []
                if block.set_number is not None:
                    metadata.append(
                        f"static constexpr std::uint32_t Set = {block.set_number};"
                    )
                if block.binding is not None:
                    metadata.append(
                        f"static constexpr std::uint32_t Binding = {block.binding};"
                    )
                metadata.append(
                    f"static constexpr std::size_t ElementStride = {stride};"
                )

                if f.type_name in self.glsl_structs:
                    decl = self.glsl_structs[f.type_name]
                    prefix = f.name + "[0]."
                    self.build_named_struct(
                        f.type_name, decl.fields, block, entries,
                        prefix, 0, stride
                    )
                    metadata.insert(0, f"using Element = {f.type_name};")
                else:
                    metadata.insert(0, f"using Element = {cpp_type(f.type_name)};")

                if block.name not in self.outputs:
                    self.outputs[block.name] = OutStruct(
                        block.name, 0, [], metadata
                    )
                    self.order.append(block.name)
                continue

            block_size = self.infer_block_size(block, entries)
            self.build_named_struct(
                block.name, block.fields, block, entries, "", 0, block_size
            )

        return [self.outputs[name] for name in self.order]


def emit_struct(out: list[str], s: OutStruct) -> None:
    out.append(f"struct {s.name} {{")
    if s.metadata:
        for line in s.metadata:
            out.append(f"    {line}")
        out.append("};")
        out.append("")
        return

    cursor = 0
    padding = 0
    for f in sorted(s.fields, key=lambda x: x.offset):
        if f.offset < cursor:
            raise ValueError(
                f"Overlapping generated members in {s.name}: {f.name}"
            )
        if f.offset > cursor:
            gap = f.offset - cursor
            out.append(
                f"    std::array<std::byte, {gap}> _padding{padding}{{}};"
            )
            padding += 1
            cursor += gap
        out.append(f"    {f.cpp_type} {f.name}{{}};")
        cursor += f.size
    if cursor < s.size:
        out.append(
            f"    std::array<std::byte, {s.size - cursor}> "
            f"_padding{padding}{{}};"
        )
        cursor = s.size
    if cursor != s.size:
        raise ValueError(
            f"Generated size mismatch for {s.name}: {cursor} != {s.size}"
        )
    out.append("};")
    out.append(f"static_assert(std::is_standard_layout_v<{s.name}>);")
    out.append(f"static_assert(sizeof({s.name}) == {s.size});")
    for f in sorted(s.fields, key=lambda x: x.offset):
        out.append(
            f"static_assert(offsetof({s.name}, {f.name}) == {f.offset});"
        )
    out.append("")


def preprocess_glsl(
    source: Path,
    glslang_validator: Path,
    include_dirs: list[Path],
) -> str:
    command = [str(glslang_validator), "-E"]
    command.extend(f"-I{directory}" for directory in include_dirs)
    command.append(str(source))

    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )

    if completed.returncode != 0:
        details = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(
            "glslangValidator preprocessing failed.\n"
            f"Command: {' '.join(command)}\n"
            f"{details}"
        )

    return completed.stdout


def generate(
    source: Path, reflect: Path, namespace: Optional[str],
    include_dirs: list[Path], glslang_validator: Path
) -> str:
    glsl_text = preprocess_glsl(source, glslang_validator, include_dirs)
    structs, blocks = parse_glsl(glsl_text)
    entries, ref_blocks = parse_reflection(
        reflect.read_text(encoding="utf-8")
    )

    built = LayoutBuilder(structs, blocks, entries, ref_blocks).build()

    out = [
        "// Generated from GLSL source and glslangValidator reflection.",
        "// Do not edit.",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <type_traits>",
        "",
        "#ifndef OTCV_SHADER_STRIDED_ELEMENT_DEFINED",
        "#define OTCV_SHADER_STRIDED_ELEMENT_DEFINED",
        "template <typename T, std::size_t Stride>",
        "struct ShaderStridedElement {",
        "    static_assert(Stride >= sizeof(T));",
        "    T value{};",
        "    std::array<std::byte, Stride - sizeof(T)> padding{};",
        "};",
        "#endif",
        "",
    ]
    if namespace:
        out += [f"namespace {namespace} {{", ""]

    for s in built:
        emit_struct(out, s)

    if namespace:
        out += [f"}} // namespace {namespace}", ""]
    return "\n".join(out)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("shader", type=Path)
    p.add_argument("reflect", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--namespace")
    p.add_argument(
        "--include-dir", action="append", default=[], type=Path
    )
    p.add_argument(
        "--glslang-validator",
        required=True,
        type=Path,
        help="path to glslangValidator used for -E preprocessing",
    )
    args = p.parse_args()

    header = generate(
        args.shader,
        args.reflect,
        args.namespace,
        args.include_dir,
        args.glslang_validator,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(header, encoding="utf-8")
    print(f"Generated {args.output}")


if __name__ == "__main__":
    main()
