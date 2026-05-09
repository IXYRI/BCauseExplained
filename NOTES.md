# BCause 學習導覽

這份導覽服務於一件事：把 BCause 當作一具可以反覆拆裝的編譯器標本。

BCause 的原始 `README.md` 說明專案用途、建置方式與功能狀態；本文件則說明如何閱讀它的實現。

## 閱讀主線

建議按以下順序進入：

1. `src/README.md`：先看整個 source tree 的責任分層。
2. `src/compiler/README.md`：理解 B source 如何被降到 x86-64 assembly。
3. `docs/compiler-pipeline.md`：串起 compile、assemble、link 的完整流水線。
4. `docs/data-layout.md`：理解 B 的 word、scalar、vector、string 如何落到記憶體。
5. `docs/expression-lowering.md`：攻克 lvalue/rvalue 與 expression codegen。
6. `docs/statement-lowering.md`：理解 if、while、switch、goto、return 的 label lowering。
7. `src/libb/README.md` 與 `docs/libb-runtime.md`：理解不經 libc 的 runtime。
8. `tests/README.md`：用測試反查語意與實作假設。
9. `examples/README.md`：用完整小程式觀察語言行為。

## 這套筆記的邊界

- 不重講 B 語言規範。
- 不把長篇教學塞進源碼。
- 源碼內只補必要短註與 section marker。
- 長說明集中放在 `docs/`。

## 核心問題

閱讀時始終追問：

- 這段 B 語法在哪裡被識別？
- 它是否產生 lvalue？
- 何時從地址變成值？
- 它向 assembly 輸出了什麼？
- runtime 需要提供哪些 symbol 才能跑起來？

## 地圖

- `docs/README.md`：長文索引。
- `src/README.md`：源碼目錄總覽。
- `src/compiler/README.md`：編譯器本體總覽。
- `src/libb/README.md`：B runtime 總覽。
- `tests/README.md`：測試總覽。
- `examples/README.md`：示例總覽。
