# bsinfo JSON 格式

[English](bsinfo-json.md) | 简体中文

`bsinfo --json` 为每条码流输出一个 JSON 对象；`--tree <nal>` 追加该 NAL 的
语法树；`--batch <dir>` 为目录中所有 `.h264/.264/.avc/.h265/.265/.hevc` 文件
输出数组。

```bash
bsinfo stream.h264 --json
bsinfo stream.h264 --json --tree 4
bsinfo --batch vectors/
```

## 码流对象

| 字段 | 类型 | 含义 |
|---|---|---|
| `file` | string | 输入路径 |
| `summary` | string | 单行码流摘要 |
| `codec` | string | `H.264/AVC` 或 `H.265/HEVC` |
| `frames` | array | 解码顺序的帧记录 |
| `nals` | array | NAL 记录 |
| `tree` | array，可选 | 指定 `--tree` 时的语法树根节点 |

帧记录：`index`、`type`、`poc`、`qp`、`size`、`offset`、`slices`、`nals`、
`dpb`、`l0`、`l1`、`missing`。`l0`/`l1` 是参考 POC 数组；`missing` 是推导
DPB 中不存在的参考。

NAL 记录：`index`、`offset`、`size`、`type`、`summary`、`error`。

语法树节点：`name`、`value`、`bit`、`len`、`error`，可选 `children`；
`bit`/`len` 相对 RBSP。

## 批量与退出码

`--batch` 输出 JSON 数组；无法解析的文件表示为
`{"file": "...", "error": "..."}`。

退出码：`0` 成功，`1` 打开/解析失败，`2` 用法错误。未指定 `--json` 时原有
文本输出保持不变。
