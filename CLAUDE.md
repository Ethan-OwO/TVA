# TVA

C++20 從零實作的版本控制工具,磁碟格式與 git 完全相容(直接讀寫 `.git/`)。
規劃文件在 `docs/`(`roadmap_v1.md`、`plan_M<n>.md`);實作細節以程式碼與測試為準。目前完成 M1。

## 建置與測試

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTVA_SANITIZE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

- 單一單元測試:`./build/tests/tva_unit_tests --gtest_filter='Ignore*'`
- 單一整合測試:`ctest --test-dir build -R integration.restore --output-on-failure`(`TVA_KEEP_TMP=1` 保留暫存目錄)
- 新檔案要手動加進 `CMakeLists.txt` / `tests/CMakeLists.txt`(不用 glob)。

## 結構

- `src/core/`(`tva::core`):物件模型、物件庫、ref、ignore、snapshot / materialize、tree diff。
- `src/platform/`(`tva::platform`):唯一可以直接呼叫 OS API 的地方(目前 POSIX,M9 補 Windows)。
- `src/cli/`(`tva::cli`):一個子指令一個 `cmd_<name>.cpp`,在 `cli.cpp` 註冊。
- `tests/unit/`:GoogleTest。`tests/integration/`:bash 腳本,用真 git 當 oracle(在隔離的 `HOME` 下執行)。

## 慣例

- 檔案 / 函式 / 變數 snake_case,型別 PascalCase,成員變數後綴 `_`。
- 可預期的失敗 throw `tva::core::Error`(訊息不加 `fatal:`);CLI 印 `fatal: <msg>` 並 exit 128,參數錯誤 exit 129。
- 檔案內容一律當 bytes,不做換行或編碼轉換。
- SHA-1 自己寫;zlib 用系統套件;其他依賴只走 FetchContent 並釘 tag。
- Debug build 開 `-Werror`。CI:gcc 13 / clang 18 + ASan/UBSan,以及 Ubuntu 22.04 的 gcc 11(最舊支援版本)。
- 發版:改 `CMakeLists.txt` 的 `project(VERSION)`,推 `v<版本>` tag,Release workflow 會打包並發佈。

## git 相容性

- 改到物件、ref 或 ignore 行為時,要在整合測試加上與真 git 的對照,並維持 `git fsck --full --strict` 乾淨。
- tree entry 依名稱位元組排序,但目錄名視為尾端多一個 `/`;目錄 mode 寫 `40000`。
- commit message 非空且不以 `\n` 結尾時補 `\n`;時區 `±HHMM`;身分字串照 git 去掉頭尾雜字元。
- `.gitignore` 比對(`src/core/ignore.cpp`)是 git `wildmatch.c` 的逐行移植,修改時要對照原始碼。
