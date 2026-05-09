# Data layout

本文件整理 BCause 如何把 B 的資料概念落到 x86-64 記憶體。

## B word

BCause 把 B 的單一資料型別直接實作成 machine word。

在 x86-64 上，這就是：

```c
sizeof(intptr_t) == 8
```

所以一個 B word 同時可以承載：

- integer value
- pointer
- packed character constant
- string address

這個假設在 compiler 和 runtime 兩邊都成立。

## global scalar layout

global scalar declaration 由 `global()` 輸出。

形狀是：

```asm
.data
.type name, @object
.align 8
name:
```

若沒有 initializer：

```asm
.zero 8
```

若有 initializer list，則每個 initializer 都 emit 一個 `.quad`。

因此對 compiler 來說，global scalar 本質上就是一段 word storage 的起始 label。

## global vector layout

global vector 與 scalar 最大的不同是：

> symbol 對應的第一個 word 不是第一個元素，而是 base pointer。

`vector()` 會輸出：

```asm
name:
	.quad .+8
```

也就是把「下一個 word 的位址」存進這個 symbol 的第一個 word。

後面才是真正的元素資料。

這非常符合早期 B 對 vector 的模型：

- vector 名稱是一個 word
- word 的內容是資料區起點地址

## local scalar layout

local scalar 在 stack 上只佔一個 word。

宣告時：

- `stack_offset += 1`
- `sub $8, %rsp`

使用時 expression 不直接拿值，而是先算：

```asm
lea -offset(%rbp), %rax
```

也就是先得到這個 scalar 的地址，再由 lvalue/rvalue 規則決定是否 load。

## local vector layout

local vector 不是 payload 直接掛在名字上，而是：

- 一個 pointer slot
- payload words

這讓 local vector 和 global vector 在語意上保持一致：

- vector name 對應一個裝 base pointer 的 word
- indexing 時先取出 base pointer，再做位移

宣告時 compiler 會：

1. 為 vector 佔 `value + 1` 個 words
2. 算出 payload 區位址
3. 把 payload 位址寫進 pointer slot

## string literal table

字串在 parse 階段不會立刻輸出到 `.rodata`，而是先進 `args->strings`。

當一個 translation input 處理完後，`strings()` 會輸出：

```asm
.section .rodata
.string.0:
	.byte ...
	.byte 0
```

也就是逐 byte 輸出，而不是依賴 assembler `.string` directive。

這樣做有兩個好處：

- B 的 escape 已經在 parser 中被解析完成
- runtime 可以用普通 C-style NUL-terminated byte string 來處理

## multi-character constant

`character()` 會把最多 `word_size` 個 byte pack 進一個 machine word。

例如：

```b
'foo'
```

不是單字元，而是一個 packed word。

實作上每個 byte 會做：

```c
value |= ((uintptr_t)(uint8_t)c) << (i * 8)
```

也就是 little-endian packing。

這也是為什麼 runtime 的 `putchar()` 可以把 `'foo'` 當成三個 byte 一次寫出去。

## pointer arithmetic 與 indexing 的單位

BCause 的 indexing 是以 B word 為單位，不是以 byte 為單位。

在 `postfix()` 裡：

```asm
shl $3, %rax
```

就是把 index 乘以 8，再加到 base pointer 上。

所以：

- `a[0]` -> base + 0
- `a[1]` -> base + 8
- `a[2]` -> base + 16

測試也正是按這個模型驗證地址差。

## scalar / vector 的統一觀點

若想少記幾條規則，可以把 BCause 的資料模型濃縮成：

- scalar name -> 直接對應一個 storage word
- vector name -> 對應一個裝 payload base pointer 的 storage word

然後再用 lvalue/rvalue 規則去決定：

- 現在要的是地址
- 還是要從地址 load 出值

這樣 global/local 兩邊的行為就會變得一致得多。

## 關聯源碼

- `src/compiler/compiler.c`：`global()`、`vector()`、`character()`、`string()`、`strings()`、`term()`、`postfix()`。
- `src/libb/libb.c`：`putchar()`、`char()`、`lchar()`。
