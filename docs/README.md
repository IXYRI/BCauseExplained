# Docs 索引

`docs/` 存放 BCause 解剖工程的長說明。源碼內只保留必要短註；跨函式、跨模組、跨階段的論述集中放在這裡。

## 長文地圖

- `compiler-pipeline.md`：從 B source 到 Linux executable 的完整流水線。
- `data-layout.md`：B 的 word、scalar、vector、string literal 與 stack/global layout。
- `expression-lowering.md`：expression parser、lvalue/rvalue、operator codegen。
- `statement-lowering.md`：statement parser 與 label/control-flow lowering。
- `abi-and-stack.md`：System V AMD64 ABI、寄存器、stack frame、對齊。
- `diagnostics-and-parser.md`：字元級 parser、`ungetc()` 模式、錯誤訊息與位置。
- `libb-runtime.md`：freestanding runtime、`_start`、syscall、標準函式。
- `known-limitations.md`：目前限制、脆弱點與可改進方向。

## 寫作原則

- 先說節點在整體 pipeline 的位置。
- 再說輸入、輸出、副作用。
- 再說關鍵不變式。
- 最後用 tests 或 examples 對照。
