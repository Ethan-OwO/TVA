# TVA

為「AI agent 寫 code」設計的版本控制工具,以 C++20 從零實作,儲存格式與 git 完全相容。名稱取自《Loki》的 Time Variance Authority。

目前完成 M2(PR 基礎),完整路線見 [docs/roadmap_v1.md](docs/roadmap_v1.md)。

## 建置

需要 Linux(或 WSL2)、g++ ≥ 11 或 clang、CMake ≥ 3.20、Ninja、zlib:

```bash
sudo apt install build-essential cmake ninja-build zlib1g-dev git
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 指令

### PR(M2)

| 指令 | 說明 |
|---|---|
| `tva pr open "<意圖>" [--slug <s>]` | 以 main 為 base 開一個 PR(branch `pr/<slug>`)並切過去;意圖的第一段是標題 |
| `tva pr list` | 列出所有 PR,`*` 標示目前所在的 PR |
| `tva pr switch <slug>\|main [--force]` | 切到某個 PR 或回到 main(Sacred Timeline),並改寫工作目錄 |
| `tva pr show [<slug>]` | 顯示 PR 的標題、描述、base、head 與 moment |

PR 就是一般的 git branch。它的第一個 commit(開 PR commit)tree 與 base 相同、message 存意圖並帶 `TVA-PR-Open: <slug>` trailer,所以 push 到 GitHub 時 PR 資訊會跟著走。slug 由標題的英數字產生(全中文標題得到 `pr`、`pr-2`…),細節見 [docs/plan_M2.md](docs/plan_M2.md)。

### 快照與 plumbing(M1)

| 指令 | 說明 |
|---|---|
| `tva init [<dir>]` | 建立 repo |
| `tva snapshot -m <msg>` | 把整個工作目錄(扣掉 ignore)存成目前 branch 上的一個 commit |
| `tva status` | 與 HEAD 比較,列出 `A` / `D` / `M` / `T`(mode 變更) |
| `tva log [--oneline] [<rev>]` | 沿 first parent 列出歷史 |
| `tva restore [--force] <rev>` | 把工作目錄還原成某個 commit 或 tree(不移動 HEAD) |
| `tva hash-object` / `cat-file` / `write-tree` / `commit-tree` / `rev-parse` | 與 git 同名 plumbing 相同 |

TVA 寫出的物件與 ref 可直接用真 git 讀取(`git log`、`git fsck` 等)。M1 還不寫 `.git/index`,所以在 TVA repo 裡跑 `git status` 會顯示錯誤的狀態,M7 處理。

## 授權

[MIT](LICENSE)
