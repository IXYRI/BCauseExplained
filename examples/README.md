# Examples 總覽

`examples/` 收集可以手動編譯與執行的 B 程式。它們比單元測試更接近使用者視角，適合觀察完整程式如何通過 BCause pipeline。

## 範例列表

- `helloworld.b`：最小輸出示例。
- `fizzbuzz.b`：條件、迴圈、取模與輸出。
- `fibonacci.b`：遞迴或序列計算示例。
- `e-2.b`：數值計算示例。
- `time.b`：runtime 時間相關函式示例。
- `b.b`：更大的 B 程式樣本。

## 閱讀方式

建議每個例子都做三件事：

1. 看 B source 寫法。
2. 用 `bcause -S` 只產生 assembly。
3. 對照 `src/compiler/` 中相應語法的 lowering。

範例不是規範本身，但非常適合作為「從語法到 assembly」的觀察樣本。
