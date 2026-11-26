# bsinfo JSON schema

English | [简体中文](bsinfo-json.zh-CN.md)

`bsinfo --json` emits one JSON object per stream. `--tree <nal>` adds the
syntax tree for that NAL; `--batch <dir>` emits an array of objects for every
`.h264/.264/.avc/.h265/.265/.hevc` file in the directory.

```bash
bsinfo stream.h264 --json
bsinfo stream.h264 --json --tree 4
bsinfo --batch vectors/
```

## Stream object

| Field | Type | Meaning |
|---|---|---|
| `file` | string | input path |
| `summary` | string | one-line stream summary |
| `codec` | string | `H.264/AVC` or `H.265/HEVC` |
| `frames` | array | decoded-order frame records |
| `nals` | array | NAL records |
| `tree` | array, optional | syntax-tree roots when `--tree` is given |

Frame record: `index`, `type`, `poc`, `qp`, `size`, `offset`, `slices`,
`nals`, `dpb`, `l0`, `l1`, `missing`. `l0`/`l1` are reference POC arrays;
`missing` lists references not present in the derived DPB.

NAL record: `index`, `offset`, `size`, `type`, `summary`, `error`.

Syntax-tree node: `name`, `value`, `bit`, `len`, `error`, optional `children`.
`bit`/`len` are RBSP-relative.

## Batch and exit codes

`--batch` returns a JSON array; a file that cannot be parsed is represented as
`{"file": "...", "error": "..."}`.

Exit codes: `0` success, `1` open/parse failure, `2` usage error. Existing
text output is unchanged when `--json` is absent.
