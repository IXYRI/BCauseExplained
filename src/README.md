# Source tree 總覽

`src/` 目前分成兩個子系統：

- `compiler/`：BCause 編譯器本體，把 B source 降到 x86-64 GNU assembly，並驅動 assembler/linker。
- `libb/`：B 標準庫與 runtime，提供 `_start`、I/O、格式化輸出與 Linux syscall 包裝。

## 閱讀順序

1. `compiler/main.c`：命令列入口。
2. `compiler/compiler.h`：全局編譯狀態與公開接口。
3. `compiler/list.c` / `compiler/list.h`：簡單動態陣列。
4. `compiler/compiler.c`：真正的 parser/codegen/driver。
5. `libb/libb.c`：freestanding runtime。

## 架構邊界

`compiler/` 產生的程式需要 `libb/` 提供入口與標準函式。兩者之間不是透過 header API 直接耦合，而是透過 symbol 名稱與 link 階段耦合。

典型關係：

```text
B source
  -> compiler emits calls/references
  -> ld links with libb.a
  -> _start calls B main()
```
