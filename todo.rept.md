# BCause 解剖註釋工程待辦

## 0. 目標與策略

- [x] 採用「源碼逐行為主，文檔長論述輔助」：重要源碼逐段/近逐行註釋，跨模組長文放獨立說明文件。
- [ ] 文檔風格採用「分層解剖」：先概覽，再進入節點，再逐段/逐行說明。
- [ ] 第一輪範圍限定為：
	- [ ] `src/compiler/` 編譯器本體。
	- [ ] `src/libb/` 標準庫 runtime。
	- [ ] 根目錄與重要目錄 README。
- [ ] 暫不把 B 語言規範作為主線；重點放在 BCause 實現如何把 B 降到 x86-64/Linux。

## 1. 文檔骨架建設

- [x] 建立根目錄導覽文檔：`NOTES.md` 或擴充 `README.md` 的學習導覽段落。
- [x] 為 `src/` 建立總覽：`src/README.md`。
- [x] 為 `src/compiler/` 建立說明：`src/compiler/README.md`。
- [x] 為 `src/libb/` 建立說明：`src/libb/README.md`。
- [x] 為 `tests/` 預留說明位置：`tests/README.md`，本輪可只建提綱。
- [x] 為 `examples/` 預留說明位置：`examples/README.md`，本輪可只建提綱。

## 2. 編譯器主線解剖

- [x] 梳理 `main.c`：命令列選項、預設參數、入口控制流。
- [x] 梳理 `compiler.h`：核心狀態 `struct compiler_args`、位置資訊、公共接口。
- [x] 梳理 `list.h` / `list.c`：簡單動態陣列、ownership 風險、使用場景。
- [x] 梳理 `compile()`：輸入檔、assembly buffer、`as`、`ld`、臨時檔處理。
- [x] 梳理 `subprocess()`：fork/exec/wait 模型與錯誤處理。

## 3. `compiler.c` 逐段論述順序

- [x] 前置常量與表：`MAX_FN_CALL_ARGS`、argument registers、operator code tables。
- [x] 診斷與工具：`eprintf()`、`eprintf_pos()`、`concat()`。
- [x] 字元級掃描：`comment()`、`whitespace()`、`identifier()`、`number()`。
- [x] literal 解析：`character()`、`string()`、`ival()`。
- [x] 全域資料 lowering：`global()`、`vector()`。
- [x] 符號查找：`struct stack_var`、`find_identifier()`。
- [x] expression 原子與 postfix：`term()`、`postfix()`。
- [x] binary / comparison / assignment codegen：`binary_expr()`、`cmp_expr()`、`assign_expr()`。
- [x] precedence parser：`expression()`。
- [x] statement lowering：`statement()`。
- [x] 函式參數與函式體：`arguments()`、`function()`。
- [x] string table 與 top-level：`strings()`、`declarations()`。

## 4. 重要專題長說明

- [x] 建立 `docs/compiler-pipeline.md`：source 到 executable 的完整流水線。
- [x] 建立 `docs/data-layout.md`：global scalar/vector、local scalar/vector、string literal、多字元常量。
- [x] 建立 `docs/expression-lowering.md`：lvalue/rvalue、寄存器約定、運算子 lowering。
- [x] 建立 `docs/statement-lowering.md`：if/while/switch/case/goto/return 的 label 生成。
- [x] 建立 `docs/abi-and-stack.md`：System V AMD64 ABI、stack frame、參數傳遞、對齊。
- [x] 建立 `docs/diagnostics-and-parser.md`：無 lexer 的字元級 parser、`ungetc()` 模式、錯誤位置問題。
- [x] 建立 `docs/known-limitations.md`：限制、偏差、可改進點。

## 5. `libb` runtime 解剖

- [x] 梳理 freestanding 設定：`-nostdlib`、`-ffreestanding`、`B_TYPE`。
- [x] 梳理 syscall wrapper：`__syscall0/1/2/3` 與 Linux syscall number。
- [x] 梳理 `_start()`：不經 libc 進入 B `main()`。
- [x] 梳理輸出族：`putchar()`、`printn()`、`printf()`。
- [x] 梳理字元/字串工具：`char()`、`lchar()`、內部 `strlen()`。
- [x] 梳理檔案與程序 syscall：`open`、`close`、`nread`、`nwrite`、`fork`、`execv`、`wait` 等。
- [x] 建立 `docs/libb-runtime.md` 長說明。

## 6. 源碼內逐行註釋策略

- [x] 重要源碼以逐段/近逐行註釋為主，長篇架構論述仍放進 `docs/`。
- [x] 對 `compiler.c` 加 section banner，標出 parsing/codegen 區塊。
- [x] 對 `expression()` 的 precedence level 加表格或短註。
- [ ] 對 local/global vector layout 加短註。
- [x] 對 `libb.c` 的 syscall inline assembly 加短註。

## 7. 複雜與緊迫拆解

### 高複雜、高價值

- [x] `expression()`：最高優先，因為它同時承載 parser、語意、codegen、lvalue/rvalue。
- [x] `statement()`：第二優先，巨大控制流節點，label 生成與 scope 問題集中於此。
- [x] local/global vector layout：第三優先，是 B 語意與 x86-64 layout 的核心交界。
- [x] `libb` `_start` + syscall：第四優先，是可執行文件能跑起來的 runtime 根。

### 中複雜、支撐性強

- [x] `compile()` / `subprocess()`：理解整體 pipeline 必要。
- [x] `function()` / `arguments()`：理解 ABI 與 stack frame 必要。
- [x] `strings()` / string table：連接 parser 與 data section。

### 低複雜、可先完成建立信心

- [ ] `main.c` 命令列說明。
- [ ] `list.c` 動態陣列說明。
- [ ] 目錄 README 骨架。

## 8. 建議執行順序

1. [x] 先建所有 README / docs 空骨架與目錄索引。
2. [x] 完成 `main.c`、`compiler.h`、`list.c` 的源碼註釋。
3. [x] 完成 `docs/compiler-pipeline.md`。
4. [x] 深入 `compile()`、`declarations()`、`function()`。
5. [x] 深入 `expression()` 並建立 `docs/expression-lowering.md`。
6. [x] 深入 `statement()` 並建立 `docs/statement-lowering.md`。
7. [x] 深入資料佈局並建立 `docs/data-layout.md`。
8. [x] 深入 `libb.c` 並建立 `docs/libb-runtime.md`。
9. [x] 補源碼內短註與 section banner。
10. [x] 最後整理 `docs/known-limitations.md`。

## 9. 驗收標準

- [x] 新讀者能從根目錄文檔找到每個子系統的入口。
- [x] 每個重要目錄都有 README 或等價說明。
- [x] 每個重要函式都有至少一段「它做什麼、輸入是什麼、輸出/副作用是什麼」的說明。
- [x] `expression()`、`statement()`、`libb` syscall 三個高複雜區域有獨立長文。
- [x] 源碼內註釋不喧賓奪主；長論述都能在 docs 中找到。
- [ ] 說明能對照測試或 examples 驗證，而不是只描述猜想。

