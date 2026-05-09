# BCause 閱讀順序指南

如果你是來**讀懂這個項目**，而不是立刻改功能，建議按下面的線性順序走。

## 0. 先看地圖

1. `README.md`
2. `NOTES.md`
3. `src/README.md`
4. `docs/README.md`

這四個文件的作用不是講細節，而是讓你知道：

- 專案在幹什麼
- 哪些部分是 compiler
- 哪些部分是 runtime
- 哪些 `docs/` 值得在什麼時候打開

## 1. 先走編譯流水線外殼

5. `src/compiler/main.c`
6. `src/compiler/compiler.h`
7. `docs/compiler-pipeline.md`
8. `docs/diagnostics-and-parser.md`

這一步要先回答：

- 命令列參數怎麼進來
- `struct compiler_args` 裡裝了什麼
- `.b` 是怎麼走到 `.s` / `.o` / executable 的
- parser 為什麼直接在字元流上工作

如果這一步沒走，後面看 parser 會容易迷路。

## 2. 再看最小支撐件

9. `src/compiler/list.h`
10. `src/compiler/list.c`

這部分很小，但會反覆出現於：

- locals
- extrns
- strings
- switch case list

看完後至少要知道：

> `struct list` 只管 pointer array，不管 element ownership。

## 3. 進 `compiler.c`，但按區塊看

建議依這個順序：

11. `compile()` / `subprocess()`
12. `comment()` / `whitespace()` / `identifier()` / `number()`
13. `character()` / `string()` / `ival()`
14. `global()` / `vector()`
15. `find_identifier()`
16. `term()` / `postfix()`
17. `binary_expr()` / `cmp_expr()` / `assign_expr()`
18. `expression()`
19. `statement()`
20. `arguments()` / `function()`
21. `strings()` / `declarations()`

不要從頭一路死讀到尾，那樣會很累。

## 4. 在 `compiler.c` 幾個關鍵節點之間插入長文

如果想讀得更順，不要把 `compiler.c` 整段吃完才回去看文檔。比較好的節奏是：

22. `docs/data-layout.md`
23. `docs/expression-lowering.md`
24. `docs/statement-lowering.md`
25. `docs/abi-and-stack.md`

推薦穿插方式：

- 看完 `global()` / `vector()` / `string()` 後，打開 `docs/data-layout.md`
- 看完 `term()` / `postfix()` / `expression()` 後，打開 `docs/expression-lowering.md`
- 看完 `statement()` 後，打開 `docs/statement-lowering.md`
- 看完 `arguments()` / `function()` 後，打開 `docs/abi-and-stack.md`

## 5. 讀 `compiler.c` 時要配套查的文檔

對應關係如下：

- 看 `expression()` 時：`docs/expression-lowering.md`
- 看 `statement()` 時：`docs/statement-lowering.md`
- 看參數、stack frame、call lowering 時：`docs/abi-and-stack.md`
- 看 global/local vector、string、char packing 時：`docs/data-layout.md`
- 看 scanner / error path 時：`docs/diagnostics-and-parser.md`
- 想知道它哪裡故意簡陋時：`docs/known-limitations.md`

## 6. 再進 runtime

26. `src/libb/README.md`
27. `src/libb/libb.c`
28. `docs/libb-runtime.md`

建議在 `libb.c` 裡這樣看：

- `B_TYPE` / `B_FN`
- syscall wrapper
- `_start`
- `char()` / `lchar()`
- `printf()` / `printn()` / `putchar()`
- 其他 syscall wrapper

這一步的關鍵問題是：

> compiler 產生出來的 symbol，最後怎麼靠這個 runtime 真正跑起來？

## 7. 用 tests 反查，不用死背

29. `tests/README.md`
30. 按主題看：
   - `expr_test.cpp`
   - `precedence_test.cpp`
   - `assignment_test.cpp`
   - `func_test.cpp`
   - `globals_test.cpp`
   - `libb_test.cpp`

如果某段 lowering 看著抽象，回頭找對應測試通常最快。

## 8. 最後看 examples

31. `examples/README.md`
32. `examples/helloworld.b`
33. `examples/fizzbuzz.b`
34. `examples/fibonacci.b`
35. 其他 examples

examples 比 tests 更接近「完整程式」，適合最後拿來驗證自己對整個系統的直覺。

## 最短閱讀路徑

如果只想走一條最短但仍有收穫的路：

```text
README.md
-> NOTES.md
-> src/compiler/main.c
-> docs/compiler-pipeline.md
-> docs/diagnostics-and-parser.md
-> src/compiler/compiler.c 的 expression()/statement()
-> docs/data-layout.md
-> docs/expression-lowering.md
-> docs/statement-lowering.md
-> docs/abi-and-stack.md
-> src/libb/libb.c
-> docs/libb-runtime.md
```

## 一句話建議

先看外殼，再看 expression，再看 statement，再看 runtime。

不要一開始就硬啃整個 `compiler.c`；那樣很容易把自己啃成註釋的一部分。
