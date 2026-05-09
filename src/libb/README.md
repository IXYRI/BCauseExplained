# libb runtime 總覽

`src/libb/` 提供 B 程式執行時需要的最小標準庫。它以 freestanding 方式編譯，不依賴 libc，直接使用 Linux x86-64 syscall。

## 核心責任

- 提供 `_start` 作為可執行檔入口。
- 呼叫 B 程式的 `main()`。
- 用 syscall 實作 I/O、檔案、程序與時間相關函式。
- 提供 B 風格的 `putchar()`、`printn()`、`printf()`。

## 閱讀順序

1. 編譯設定：`B_TYPE`、`B_FN`、`MAX_EXECL_ARGS`。
2. syscall wrapper：`__syscall0/1/2/3`。
3. syscall number 定義。
4. `_start()`。
5. 輸出函式：`putchar()`、`printn()`、`printf()`。
6. 字元工具：`char()`、`lchar()`。
7. 檔案與程序 syscall wrapper。

## 與 compiler 的關係

編譯器輸出的 assembly 會引用 B 標準函式，例如：

- `printf`
- `putchar`
- `printn`
- `exit`

link 階段透過 `-lb` 將這些 symbol 從 `libb.a` 接上。

## 待深入長文

見 `../../docs/libb-runtime.md`。
