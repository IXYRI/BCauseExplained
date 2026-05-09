# Compiler pipeline

本文件追蹤 BCause 如何把一個 `.b` source file 變成 Linux x86-64 executable。

## 一句話總覽

BCause 的 pipeline 是：

```text
命令列參數
	-> struct compiler_args
	-> 讀取 .b source
	-> declarations() 直接輸出 assembly 到 memory stream
	-> 寫出 .s
	-> as 產生 .o
	-> ld 靜態連結 libb.a
	-> executable
```

它不是「parse 成 AST，再由 backend 生成機器碼」的現代分層結構。它更直接：parser 一邊讀 source，一邊往 `FILE *out` 寫 GNU assembler 語法。

## 建置出的工具與 runtime

根目錄 `Makefile` 會產生兩個主要檔案：

- `bcause`：編譯器可執行檔，由 `src/compiler/*.c` 編譯。
- `libb.a`：B runtime / standard library，由 `src/libb/*.c` 以 freestanding 模式編譯後打包。

`bcause` 負責把 `.b` 轉為 object 或 executable；`libb.a` 負責提供 `_start`、`printf`、`putchar`、`exit` 等 B 程式執行時會用到的 symbol。

## `main()`：把命令列收束成 `compiler_args`

入口在 `src/compiler/main.c`。

`main()` 不直接編譯 source。它只做三件事：

1. 建立 `struct compiler_args c_args`。
2. 解析命令列選項並填入 `c_args`。
3. 確認至少有一個 input file，然後呼叫 `compile(&c_args)`。

預設設定由 `set_default_args()` 建立：

- `arg0`：程式名稱，用於錯誤訊息。
- `lib_dir = "-L."`：預設從目前目錄搜尋 `libb.a`。
- `output_file = A_OUT`，也就是 `a.out`。
- `input_files`：指向 `main()` stack 上的 `input_files` 陣列。
- `do_assembling = true`。
- `do_linking = true`。
- `word_size = X86_64_WORD_SIZE`。

這裡的 `word_size` 是 BCause 實作 B 單一 word 型別的核心參數；在 x86-64 上是 8 bytes。

## 命令列選項如何改變 pipeline

### 預設模式

```text
bcause input.b -o output
```

預設會走完整流程：

```text
input.b -> output.s -> output.o -> output
```

若沒有 `-o`，最終輸出是 `a.out`。

### `-S`

`-S` 表示只編譯到 assembly，不 assemble、不 link。

在 `main()` 中它會設定：

- `output_file = A_S`，也就是 `a.s`。
- `do_assembling = false`。
- `do_linking = false`。

因此 `compile()` 會直接把 assembly 寫到 `a.s`，然後停止。

### `-c`

`-c` 表示 compile + assemble，但不 link。

在 `main()` 中它會設定：

- `output_file = A_O`，也就是 `a.o`。
- `do_linking = false`。

因此 pipeline 是：

```text
input.b -> a.o.s -> a.o
```

這裡有一個實作細節：因為 `compile()` 在需要 assemble 時會把 assembly file 命名為 `output_file + ".s"`，所以預設 `-c` 的中間 assembly 名稱是 `a.o.s`。

### `--save-temps`

`--save-temps` 不改變 pipeline 階段，只改變中間檔是否刪除。

未開啟時：

- assemble 後刪除 `.s`。
- link 後刪除 `.o`。

開啟時：

- 保留 `.s`。
- 保留 `.o`。

測試系統使用 `--save-temps`，這讓失敗時可以回頭檢查產生的 assembly 與 object。

### `-L<dir>`

`-L<dir>` 不影響 compile/assemble，只影響 link。它會取代 `args->lib_dir`，最後傳給 `ld`，用來尋找 `libb.a`。

測試中常用：

```text
-L..
```

因為測試在 `build/` 目錄執行，而 `libb.a` 位於上一層。

## `compile()`：總控函式

`compile()` 位於 `src/compiler/compiler.c`，是實際 pipeline 的總控節點。

它的輸入是已經填好的 `struct compiler_args *args`。它的副作用包括：

- 讀取 source file。
- 產生 assembly 文字。
- 寫出 `.s`。
- 可能呼叫 `as`。
- 可能呼叫 `ld`。
- 可能刪除中間檔。

### 輸出檔名推導

`compile()` 一開始會根據是否 assemble/link 推導中間檔名：

- `asm_file = args->do_assembling ? concat(args->output_file, ".s") : args->output_file`
- `obj_file = args->do_linking ? concat(args->output_file, ".o") : args->output_file`

也就是：

| 模式           | `output_file` | `asm_file` | `obj_file` |
| -------------- | ------------- | ---------- | ---------- |
| 預設 `-o prog` | `prog`        | `prog.s`   | `prog.o`   |
| `-S`           | `a.s`         | `a.s`      | `a.s`      |
| `-c`           | `a.o`         | `a.o.s`    | `a.o`      |

這張表很重要：BCause 的檔名策略不是獨立的 target model，而是從 `output_file` 和兩個布林旗標推導。

### Assembly buffer

BCause 不會一邊解析一邊直接寫到磁碟檔。它先用 GNU extension `open_memstream()` 建立 memory-backed `FILE *buffer`：

```text
FILE *buffer = open_memstream(&buf, &buf_len)
```

後續 parser/codegen 看到的 `out` 其實是這個 memory stream。

這帶來一個簡單流程：

1. 所有 input file 的 assembly 都先 append 到同一個 memory stream。
2. `fclose(buffer)` 後，`buf` 和 `buf_len` 得到完整 assembly 文字。
3. 再一次性寫入 `asm_file`。

這也是為什麼 `README.md` 說明相容性時要求 GNU/Linux：`open_memstream()` 是 GNU/POSIX 風格接口，不是 ISO C99。

## source file 掃描與 `.b` 過濾

`compile()` 會遍歷 `args->input_files`。

對每個輸入檔，它先做：

1. `stat()`：確認檔案存在且不是目錄。
2. 檢查檔名是否以 `.b` 結尾。
3. 設定 `args->pos.file_name` 與 `args->pos.line`。
4. `fopen()`。
5. 呼叫 `declarations(args, in, buffer)`。
6. `fclose(in)`。

只有副檔名確實是 `.b` 的檔案會被送進 parser。非 `.b` 輸入通過存在性檢查後不會被編譯；目前實作沒有對這種情況額外報錯。

## `declarations()`：top-level parser 入口

`declarations()` 是 source 層面的入口。它會反覆讀取 top-level identifier，並根據後續字元分派：

- `name(`：函式定義，交給 `function()`。
- `name[`：global vector，交給 `vector()`。
- 其他：global scalar，交給 `global()`。

每個 top-level symbol 先輸出：

```text
.globl name
```

然後對應函式或資料 declaration 會繼續向 assembly stream 寫入 `.text`、`.data`、label、指令或 directive。

所有 string literal 先累積在 `args->strings`。當 `declarations()` 讀完整個 source file 後，會呼叫 `strings(args, out)` 輸出 `.rodata` string table。

## 寫出 `.s`

所有輸入檔都處理完後，`compile()` 關閉 memory stream：

```text
fclose(buffer)
```

此時 assembly 完整內容位於 `buf`，長度位於 `buf_len`。

接著：

1. `fopen(asm_file, "w")`
2. `fwrite(buf, buf_len, 1, out)`
3. `fclose(out)`
4. `free(buf)`

到這一步，BCause 自己的主要工作已經完成。後面兩步是交給外部工具。

## 呼叫 assembler：`as`

若 `args->do_assembling` 為 true，`compile()` 會呼叫：

```text
as <asm_file> -o <obj_file>
```

實際執行由 `subprocess()` 完成。

若 `as` 返回非零 exit code，BCause 會輸出：

```text
error running assembler (exit code N)
```

並返回失敗。

assemble 成功後，如果沒有 `--save-temps`，會刪除 `asm_file`。

## 呼叫 linker：`ld`

若 `args->do_linking` 為 true，`compile()` 會呼叫：

```text
ld -static -nostdlib \
	<obj_file> \
	<args->lib_dir> -L/lib64 -L/usr/local/lib \
	-lb \
	-o <args->output_file> \
	-z noexecstack
```

幾個重點：

- `-static`：靜態連結。
- `-nostdlib`：不連 libc / 系統啟動檔。
- `-lb`：連結 `libb.a`。
- `-z noexecstack`：標記 stack 不可執行。

因為 `-nostdlib`，可執行檔入口不是 libc 的 `_start`，而是 `libb.a` 中提供的 `_start`。那個 `_start` 會呼叫 B 程式的 `main()`，再用 syscall exit。

link 成功後，如果沒有 `--save-temps`，會刪除 `obj_file`。

## `subprocess()`：外部工具執行模型

`subprocess()` 是 `as` 與 `ld` 的共用執行器。

它做的事：

1. 先把即將執行的命令列印到 stdout。
2. `fork()`。
3. child process 中 `execvp(p_name, p_arg)`。
4. parent process 中 `waitpid()` 等待 child。
5. 回傳 child 的 exit status。

這使 BCause 自己不實作 assembler 或 linker，而是明確依賴 GNU binutils。

## 多輸入檔行為

`args->input_files` 可以包含多個檔案。`compile()` 會把每個 `.b` 檔的 top-level declarations 依序 append 到同一個 assembly buffer。

因此可以把多個 B source 合成一個 assembly file，再生成一個 object/executable。

需要注意：

- 每個檔案讀取前會重設 `args->pos.file_name` 和 `args->pos.line`。
- `declarations()` 結束時會輸出該檔案累積的 string table。
- symbol collision 主要交給 assembler/linker 或後續語意檢查暴露。

## Pipeline 與測試

`tests/fixture.cpp` 中的 `compile_and_run()` 會建立臨時 `.b` 檔，然後執行：

```text
../bcause --save-temps -L.. <test>.b -o <test>
```

之後再執行：

```text
./<test>
```

所以測試驗證的是完整 pipeline：

```text
B source
	-> BCause parser/codegen
	-> GNU as
	-> GNU ld
	-> libb runtime
	-> process stdout
```

這也是 tests 很有價值的原因：它們不是只驗證 parser 接受某種語法，而是驗證最終 binary 的可觀察行為。

## 讀這條 pipeline 時要抓住的接口

BCause 的幾個關鍵接口不是傳統 class/module API，而是更低階的邊界：

- `main.c -> compile()`：以 `struct compiler_args` 傳遞整個編譯設定。
- `compile() -> declarations()`：以 `FILE *in` 和 `FILE *out` 連接 source parser 與 assembly emitter。
- parser/codegen -> assembler：以 GNU assembly 文字作為接口。
- assembler -> linker：以 object file 作為接口。
- compiler output -> libb：以 symbol name 作為接口。

如果後續要重構，這些接口就是最該先穩住的邊界。

## 關聯源碼

- `src/compiler/main.c`
- `src/compiler/compiler.c`
- `Makefile`
