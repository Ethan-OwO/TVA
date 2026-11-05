# plan_M2 — PR 基礎

## 涵蓋範圍

對應 roadmap M2:`tva pr open "<意圖>"`、`tva pr list`、`tva pr switch <slug>`、`tva pr show [<slug>]`,建立 / 切換 `refs/heads/pr/<slug>`,切換時改寫工作目錄。並拍板兩件 roadmap 留到這裡決定的事:**PR 標題與描述怎麼用 git 原生形式存**、**slug 怎麼產生**。

M2 不做:merge / prune(M5)、hook 自動開 PR(M3)、repo 層級 lock(M3)。

## 1. 拍板的設計

### 1.1 PR 的 metadata:「開 PR commit」

開 PR 時,TVA 在 `refs/heads/pr/<slug>` 上建立一個 commit O:

| 欄位 | 內容 |
|---|---|
| tree | base 的 tree(main 開 PR 當下的 tree;main 還沒有 commit 時是空 tree `4b825dc…`) |
| parent | main 當下的 head;main 還沒有 commit 時沒有 parent(root commit) |
| message | 意圖原文(去掉開頭空行與結尾空白),空一行,再接 trailer 段 `TVA-PR-Open: <slug>` |
| author / committer | 目前身分(與 `snapshot` 相同的規則) |

- **標題** = message 本文的 git subject(第一段,多行以空白接起來,同 `git log --format=%s`);**描述** = 第一段之後的其餘段落(去掉頭尾空行)。
- **base** = O 的 parent。這是 PR 從 Sacred Timeline 分出去的點,M5 合併時會用到。
- **moment** = PR head 沿 first parent 往回走、在 O 之前遇到的 commit(M2 階段由 `tva snapshot` 產生,M3 起由 hook 產生)。
- **辨認 O**:從 PR head 沿 first parent 往回走,第一個 trailer 段含 `TVA-PR-Open: <這個 PR 的 slug>` 的 commit。比對 slug 是為了不被別的 PR 的開 PR commit 混淆(例如有人用真 git 把 PR fast-forward 進 main)。

為什麼這樣選:
- 它就在 branch 歷史裡,push `pr/<slug>` 就帶著走,不需要額外 refspec;GitHub 上看是一個普通的空 commit,標題就是 PR 標題。
- parent 天然記錄了 base;M5 的 squash commit 可以直接沿用標題與描述。

排除的方案:
- git notes(`refs/notes/*`):要額外 refspec,GitHub 不顯示,rebase / 改寫後容易遺失。
- 獨立 ref(例如 `refs/tva/meta/<slug>`):GitHub 是否接受 `refs/tva/*` 要到 M7 才驗證。
- 在 tree 裡放檔案(例如 `.tva-pr.json`):污染工作目錄,合併時還會衝突。

### 1.2 Trailer

沿用 git trailer 格式(`git interpret-trailers` 看得懂):message 最後一段、每行 `Key: value`。

- TVA 寫入時:本文去掉結尾空白,接 `\n\n`,每個 trailer 一行;key 一律 `TVA-` 開頭;value 裡的換行換成空白。
- TVA 讀取時:只看最後一段,而且**第一段(subject)永遠不算 trailer**(與 git 的 `trailer.c` 相同);該段每一行都必須是 `Token: value`(token 為 `[A-Za-z0-9-]+`)或以空白開頭的續行,否則視為沒有 trailer。
- 各里程碑用到的 key:`TVA-PR-Open`(M2);`TVA-Moment-Kind`、`TVA-Harness`、`TVA-Session`、`TVA-Turn`(M3);`TVA-PR`、`TVA-Moments`(M5)。

### 1.3 slug 規則

1. 取標題;ASCII 字母轉小寫,保留 `[a-z0-9]`,其餘字元(空白、標點、所有非 ASCII,包括中文)的每一段連續區間換成一個 `-`;去掉頭尾的 `-`。
2. 超過 40 字元時,在前 40 字元內最後一個 `-` 處截斷(找不到就硬切 40),再去掉結尾 `-`。
3. 結果為空(例如標題全是中文「做登入功能」)時用 `pr`。
4. 唯一性:若 `refs/heads/pr/<s>`、`refs/tva/merged/pr/<s>`、`refs/tva/pruned/pr/<s>` 任一存在,或 `<s>` 是保留字 `main`,依序改試 `<s>-2`、`<s>-3`……(後兩個命名空間是 M5 的存檔位置,現在就保留,避免日後撞名)。
5. `--slug <s>` 可以手動指定:必須符合 `^[a-z0-9]+(-[a-z0-9]+)*$`、長度 ≤ 64、不是 `main`、沒被占用,否則 exit 128。

為什麼只用 ASCII:shell 好打、URL 安全、沒有 Unicode 正規化(macOS 的 NFD)問題。git ref 雖然允許 UTF-8,但 `tva pr switch 做登入功能` 不好用。

### 1.4 base 永遠是 main

`tva pr open` 一律以 `refs/heads/main`(Sacred Timeline)當 base,不管目前在哪個 PR 上。`main` 是保留名稱:`tva pr switch main` 回到 Sacred Timeline。

### 1.5 切換時怎麼處理工作目錄

`pr open` 與 `pr switch` 共用同一套規則(`core/checkout.h`):

- 目標 tree 與目前 HEAD 的 tree 相同 → **不碰工作目錄**,只移動 HEAD;尚未 snapshot 的修改跟著帶過去(同 `git switch`)。這正是「在 main 上改了一點,決定開成 PR」的情境,也是 M3 hook 在 main 上自動開 PR 的路徑。
- 不同 → 先 snapshot,工作目錄必須等於 HEAD 的 tree,否則 throw `worktree has unsnapshotted changes; run tva snapshot or use --force`,什麼都不改;`--force` 會丟掉那些修改(內容仍在物件庫裡)。
- 寫入順序同 git:先改寫工作目錄,再移動 HEAD。
- `pr open` 會在建立 PR ref 之前先做上述檢查;如果之後的改寫仍失敗,刪掉剛建立的 PR ref 再把錯誤拋出去。

## 2. 檔案

```
src/core/trailers.h / .cpp     # split_trailers、append_trailers、trailer_value
src/core/pr.h / .cpp           # slug、open_pr、read_pr、list_prs、current_pr
src/core/checkout.h / .cpp     # head_tree、plan_checkout、apply_checkout、switch_branch
src/cli/cmd_pr.cpp             # tva pr open|list|switch|show
tests/unit/trailers_test.cpp
tests/unit/pr_test.cpp
tests/integration/test_pr.sh
```

另外:`RefStore::remove(name, expected_old)`(CAS 刪除,`pr open` 回滾用,M5 也會用到);`empty_tree_id()` 放進 `core/object.h`;`EnvGuard` 從 `repository_test.cpp` 移到 `tests/unit/test_util.h` 共用。

## 3. CLI 輸出

| 指令 | 輸出 |
|---|---|
| `tva pr open <intent> [--slug <s>] [--force]` | `Opened PR <slug>: <title>`,HEAD 改指 `refs/heads/pr/<slug>` |
| `tva pr list` | 每個 `refs/heads/pr/*` 一行,依 slug 排序:`<* 或空白> <slug 補齊寬度>  <title> (<n> moment[s])`;找不到開 PR commit 的 branch 顯示 `(not opened by tva)`;沒有 PR 時印 `no open PRs` |
| `tva pr switch <slug>\|main [--force]` | `Switched to PR <slug>` / `Switched to main`;已經在那裡時印 `Already on …` 且什麼都不做(要丟掉修改用 `tva restore --force HEAD`)。也接受 `pr/<slug>` |
| `tva pr show [<slug>]` | 見下;不給 slug 時顯示目前的 PR,不在 PR 上就 exit 128;不是 TVA 開的 `pr/` branch 只印 `Head:` 與一行說明 |

`tva pr show`:

```
PR <slug>[ (current)]
Title: <title>
Base:  <short base id>          # main 還沒有 commit 時:(empty main)
Head:  <short head id>

    <描述,縮排 4 格;沒有描述就省略這一段與它前面的空行>

Moments: <n>
<short id> <subject>             # 新到舊
```

## 4. 驗收

- `trailers_test`:append → split 往返;第一段不算 trailer;最後一段混有非 trailer 行時不算;續行;value 換行被清掉。
- `pr_test`:slug 規則逐條(ASCII、標點、中文 → `pr`、截斷在 `-`、保留 `main`、撞名加 `-2`/`-3`、`--slug` 驗證);開 PR commit 的 tree / parent / message;main 未出生時開 PR 得到 root commit;`read_pr` 算出 base、標題、描述、moment;`switch_branch` 帶著修改 / 拒絕 / `--force`。
- `tests/integration/test_pr.sh`(真 git 當 oracle):
  - `git symbolic-ref HEAD` 為 `refs/heads/pr/<slug>`;`git log -1 --format=%s` 是標題;`git interpret-trailers --parse` 讀出 `TVA-PR-Open: <slug>`;`HEAD^{tree}` 等於 main 的 tree;`HEAD^` 等於 main。
  - 中文標題得到 `pr`、`pr-2`;`pr list` / `pr show` 的輸出逐字比對。
  - 在 PR 間切換時工作目錄等於目標 tree;有未 snapshot 的修改時 `pr switch` exit 128 且工作目錄完全不變;`--force` 可以強制。
  - 在 main 上有修改時 `pr open` 把修改帶進新 PR。
  - `git fsck --full --strict` 乾淨。
