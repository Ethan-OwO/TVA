# TVA roadmap v1

## 0. 基礎環境設置

目標平台:Linux(Ubuntu 22.04+ / Debian 12+ / WSL2)。原生 Windows 見 M9。

```bash
# C++ 工具鏈
sudo apt update
sudo apt install -y build-essential g++ cmake ninja-build zlib1g-dev pkg-config git
g++ --version     # 需 >= 11(C++20)
cmake --version   # 需 >= 3.20
git --version     # 測試時當對照組(oracle),TVA 本身不依賴 git 執行

# 建置與測試(在 repo 根目錄)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTVA_SANITIZE=ON
cmake --build build
ctest --test-dir build --output-on-failure

# GUI 前端(M8 才需要)
curl -fsSL https://fnm.vercel.app/install | bash
fnm install --lts && fnm use --lts
npm install -g pnpm
```

C++ 第三方依賴全部用 CMake `FetchContent` 抓(GoogleTest、CLI11、nlohmann/json、cpp-httplib),zlib 用系統套件 `find_package(ZLIB)`。不使用 vcpkg / conan。

## 1. 專案概述

TVA 是一個為「AI agent 寫 code」時代設計的版本控制工具,以 C++20 從零實作,名稱取自《Loki》的 Time Variance Authority。核心想法:拿掉 git 的 add / commit / push 手動流程,改由事件驅動自動記錄歷史。

- **最小單位是 moment**:一次 prompt 讓 agent 改出來的結果。透過 agent harness(Claude Code、Gemini CLI、Codex CLI)的 hook,在 prompt 開始與結束時自動對整個工作目錄拍快照。prompt 原文就是 commit message。prompt 之間若有人手動改檔,自動記成一個 `manual` moment,讓每個 moment 的歸屬只有一個寫入者。
- **中間層是 PR**:一個意圖(例如「做登入功能」)= 一串 moment,底層是一個 git branch。在 main 上直接送出 prompt 會自動開一個 PR。
- **主線叫 Sacred Timeline**:就是 `main`。PR 以 squash 方式合進 Sacred Timeline,main 上只出現乾淨的單一 commit,原本的 moment 保留在隱藏 ref,可在 GUI 展開。丟棄 PR 叫 **prune**(可復原)。
- **外層像 Scion**:多個 agent 同時在各自隔離的 worktree(目錄隔離)上跑不同 PR,共用同一個物件庫;所有東西都能推到 GitHub,GitHub 上看起來是正常 repo,用 TVA clone 回來能展開完整 prompt 歷史。
- **GUI**:`tva ui` 啟動本機 web server,在瀏覽器看 timeline、moment diff、PR、worktree 狀態。

定位:先作為學習專案(重點是親手實作 VCS 原理),做得好用再演進成日常工具。

最終狀態:在 Linux 上可以用 Claude Code / Gemini CLI / Codex CLI 正常工作,TVA 在背景自動記錄每個 prompt,可平行開多個 agent,用 GUI 審 PR、合進 Sacred Timeline,並與 GitHub 雙向同步。

## 2. 整體架構與技術選擇

| 項目 | 決定 |
|---|---|
| 核心語言 | C++20 |
| 建置 / 測試 | CMake + Ninja,GoogleTest;整合測試用 bash 腳本 + 真實 `git` 當 oracle |
| CLI 解析 | CLI11 |
| JSON | nlohmann/json(hook payload、REST API) |
| HTTP server | cpp-httplib(`tva ui`) |
| SHA-1 | 自己實作 |
| 壓縮 | zlib(系統函式庫) |
| 儲存格式 | 自己實作 git 相容物件格式(loose object:`"<type> <size>\0<payload>"` → zlib → SHA-1),直接寫入 `.git/objects` |
| 網路傳輸 | 先交給 `git push` / `git fetch`;自己實作 packfile 傳輸是選配(M10) |
| GUI 前端 | React + Vite + TypeScript,pnpm 管理,放在同一個 repo 的 `web/` |
| 平台 | Linux 先做;所有 OS 相關呼叫(路徑、檔案權限、開 process、檔案鎖、atomic rename)集中在 `src/platform/`,原生 Windows 延到 M9 |
| C++ 命名 | 檔案 / 函式 / 變數 snake_case,型別 PascalCase,namespace `tva::<模組>`,成員變數後綴 `_` |
| 錯誤慣例 | 致命錯誤印 `fatal: <訊息>` 到 stderr,exit code 128(同 git) |

**排除的方案(限制型理由)**
- 不用自有物件格式——要求 GitHub 能直接使用這個 repo,只有 git 相容格式做得到。
- 沒有 staging area / index 概念——設計上工作目錄裡所有非 ignore 的檔案都被追蹤,快照 = 整個工作目錄。(`.git/index` 只為了讓真 git 看得懂而寫,見 M7。)

**概念與 ref 對照**

| TVA 概念 | git 底層 |
|---|---|
| moment | 一個 commit,message = prompt,trailer 帶 `TVA-Moment-Kind: agent\|manual`、`TVA-Harness`、`TVA-Session`、`TVA-Turn` |
| PR | `refs/heads/pr/<slug>`,head = 最新 moment |
| Sacred Timeline | `refs/heads/main` |
| 已合併 PR 的 moment 存檔 | `refs/tva/merged/pr/<slug>` |
| prune 掉的 PR | `refs/tva/pruned/pr/<slug>`(可復原) |
| squash commit | main 上的一般 commit,trailer `TVA-PR: <slug>`、`TVA-Moments: <被合併的 PR head>` |
| 平行 agent 的工作區 | TVA 自己實作的 worktree,共用 `.git/objects`,各自有 HEAD |

TVA 自己的本機狀態(lock、stat cache、worktree 註冊)放在工作目錄根的 `.tva/`,並寫進 `.git/info/exclude` 讓真 git 也忽略它。

## 3. 里程碑

### M1 — 物件庫與快照核心
交付 git 相容的物件庫:自寫 SHA-1、zlib 包裝、blob / tree / commit 的序列化與解析、loose object 讀寫(temp file + rename 的 atomic 寫入)、loose ref 讀寫(lock file + compare-and-swap)、`.gitignore` 子集、把整個工作目錄快照成 tree、把 tree 還原回工作目錄、path 層級的 tree diff。CLI 提供 git 風格的 plumbing:`init`、`hash-object`、`cat-file`、`write-tree`、`commit-tree`、`rev-parse`、`log`、`status`、`restore`,加上測試用 porcelain `snapshot`。驗收的核心是「真 git 看得懂 TVA 寫的東西」:同一組檔案 `tva write-tree` 與 `git write-tree` 的 hash 一致,`git fsck --full` 無錯誤。
依賴:無。
風險:tree entry 的排序規則(目錄名比較時視為尾端有 `/`)與目錄 mode 寫成 `40000` 是最常見的 hash 不一致來源。

### M2 — PR 基礎
交付 `tva pr open "<意圖>"`、`tva pr list`、`tva pr switch <slug>`、`tva pr show <slug>`:建立 / 切換 `refs/heads/pr/<slug>`,切換時用 M1 的 restore 改寫工作目錄。PR 的標題與描述必須以 git 原生形式存放,才能跟著 push 走;具體編碼(例如 PR 第一個 commit 是 tree 不變的「開 PR」commit,message 存意圖)在 M2 plan 對話時拍板。slug 產生規則也在 M2 拍板。這一階段還沒有 merge / prune。
依賴:M1。

### M3 — Moment 與 agent hook
交付單一進入點 `tva hook --harness=<claude|gemini|codex> <prompt-start|prompt-end>`,每個 harness 一個把 stdin JSON 正規化的 adapter;`tva hooks install --harness=<...>` 把設定寫進該 harness 的專案設定檔;`tva run -- <cmd>` 作為沒有 hook 的工具的退路。prompt-start:快照並與目前 HEAD 比對,有差異先存 `manual` moment;若 HEAD 在 main 上,依 prompt 自動開 PR。prompt-end:快照存成 `agent` moment。所有 ref 更新經過 `.tva/` 的 repo 層級 lock,因為 hook 可能同時觸發。加上 stat cache(path → mtime / size / inode / blob id),讓快照不必每次重新 hash 整個 repo。`tva log` 能顯示 moment 種類與 prompt。
依賴:M1、M2。
🛑 執行前先驗證:三個 harness 實際送進 hook 的 JSON 欄位(事件名、prompt 欄位、session / turn id、cwd)。先寫一支 `tva hook --dump` 把真實 payload 存成 `tests/fixtures/hooks/<harness>/*.json`,adapter 只能依 fixture 裡真的出現的欄位寫;缺 turn id 的 harness 用 `session_id` + 遞增計數代替。Gemini CLI 與 Codex CLI 的 hook 是否需要額外開啟 feature flag 也要實測,需要的話寫進 `tva hooks install` 的輸出提示。
風險:hook 是同步執行的,快照太慢會拖慢 agent;stat cache 必須在這一階段完成,不能延後。

### M4 — Diff 與三方合併引擎
交付行層級 diff(Myers 演算法)、unified diff 輸出(`tva diff <a> <b>`、`tva log -p`)、merge base 計算、tree 層級 + 行層級的三方合併,衝突時寫入 git 風格的衝突標記。這是 M5 合併與 M8 GUI diff 檢視的共同基礎。
依賴:M1。(可與 M2 / M3 平行開發。)

### M5 — PR 合併與 prune
交付 `tva pr merge <slug>`:以 squash 方式在 main 上產生單一 commit(trailer 連回 moment),把 PR branch 移到 `refs/tva/merged/pr/<slug>`;main 已前進時用 M4 的三方合併,有衝突就停下讓使用者(或 agent)解決後再 `tva pr merge --continue`。`tva pr prune <slug>` 把 branch 移到 `refs/tva/pruned/pr/<slug>`,`tva pr unprune` 復原。`tva log --expand` 可以從 squash commit 展開原本的 moment。
依賴:M2、M3、M4。

### M6 — 平行 worktree(Scion 式外層)
交付 `tva worktree add <name> [--pr <slug>]`、`list`、`remove`:每個 worktree 是一個獨立目錄,共用 `.git/objects`,各自有 HEAD 與 stat cache,同一個 PR 同時只能被一個 worktree 佔用。hook 依 cwd 判斷屬於哪個 worktree。目標是能同時在三個 worktree 各開一個 agent,互不干擾,最後各自合併或 prune。worktree 目錄放在哪(`.tva/worktrees/` 或 repo 外的同層目錄)以及主 worktree 快照時如何排除它們,在 M6 plan 拍板。
依賴:M3、M5。
風險:多個 agent 同時寫物件與 ref;物件寫入靠 content-addressing + atomic rename 天然安全,ref 更新必須全部走 lock + CAS。

### M7 — GitHub 互通
交付:寫出合法的 `.git/index`,讓真 git 的 `git status` 在 TVA repo 裡正常;讀取 packfile(`.pack` / `.idx` v2)與 `packed-refs`,讓 TVA 能操作從 GitHub clone 下來的 repo;`tva push` / `tva fetch` 包裝 `git push` / `git fetch` 並帶上 `refs/tva/*` 的 refspec;`tva clone`。
依賴:M5(M6 完成更好,但非必要)。
🛑 執行前先驗證:GitHub 是否接受 push 自訂命名空間 `refs/tva/*`,以及 fetch 時能否取回。用一個測試 repo 實際 push / fetch / 在另一台機器 clone。決定規則:若 GitHub 拒收或不回傳,改把存檔 ref 放到 `refs/heads/tva/merged/...` 與 `refs/heads/tva/pruned/...`(一般 branch,一定能推),並在文件註明 GitHub 上會多出這些 branch。

### M8 — GUI(`tva ui`)
交付 `tva ui [--port]`:cpp-httplib 提供 REST API(timeline、moment 詳細與 diff、PR 列表 / 合併 / prune、worktree 狀態)並 serve `web/` build 出來的靜態檔。前端 React + Vite + TS:Sacred Timeline 與 PR 的分支圖、點 moment 看 prompt 與 diff、squash commit 可展開、對 PR 做 merge / prune。只綁 `127.0.0.1`。
依賴:M4、M5;M6 完成後再補 worktree 面板。(若想早點看到畫面,唯讀版可提前到 M5 之後。)

### M9 — 原生 Windows
交付:補齊 `src/platform/` 的 Windows 實作(路徑分隔、無 exec bit 時的 mode 處理、symlink 權限、檔案鎖、atomic rename 語意差異、process 啟動),CI 加 Windows build。
依賴:M1–M8 中已完成的部分。

### M10 —(選配)自己實作傳輸協定
交付:packfile 產生(含 delta)、git smart HTTP protocol v2 的 fetch / push,讓 `tva push` 不再依賴 `git` 執行檔。
依賴:M7。
