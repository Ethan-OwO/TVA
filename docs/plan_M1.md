# plan_M1 — 物件庫與快照核心

## 涵蓋範圍

對應 roadmap M1:git 相容的物件庫(自寫 SHA-1、zlib、blob / tree / commit、loose object、loose ref)、`.gitignore` 子集、工作目錄快照與還原、path 層級 tree diff,以及 git 風格的 plumbing CLI。

TVA 是 C++20 實作的版本控制工具,物件格式與 git 完全相容,直接讀寫 `.git/`。M1 不做:moment、hook、PR、行層級 diff、packfile、`packed-refs`、`.git/index`。

## 👤 使用者動作清單

1. 👤 把目前空的 GitHub repo clone 到本機,在 repo 根目錄開始工作。(TVA 自己的原始碼用真 git 管理。)
2. 👤 確認 `git config --global user.name` 與 `user.email` 已設定;沒有就自己設。TVA 會讀這兩個值當 commit 作者,整合測試會用到。
3. 👤 依 roadmap 第 0 節安裝 C++ 工具鏈(`build-essential g++ cmake ninja-build zlib1g-dev pkg-config git`)。

## 0. 環境設置(增量)

沿用 roadmap 第 0 節。M1 新增的 FetchContent 依賴只有:

- GoogleTest(固定一個 release tag)
- CLI11(固定一個 release tag)

zlib 用 `find_package(ZLIB REQUIRED)`。M1 不引入 nlohmann/json 與 cpp-httplib。

CMake 選項:
- `TVA_SANITIZE`(預設 OFF):開啟時對所有 target 加 `-fsanitize=address,undefined -fno-omit-frame-pointer`。
- 一律開 `-Wall -Wextra -Wpedantic`;Debug 時加 `-Werror`。

## 1. 檔案結構

```
.
├── CMakeLists.txt
├── cmake/
│   └── dependencies.cmake        # FetchContent:GoogleTest、CLI11
├── .gitignore                    # build/
├── src/
│   ├── main.cpp
│   ├── cli/
│   │   ├── cli.h
│   │   ├── cli.cpp               # 註冊所有子指令、統一錯誤處理
│   │   ├── cmd_init.cpp
│   │   ├── cmd_hash_object.cpp
│   │   ├── cmd_cat_file.cpp
│   │   ├── cmd_write_tree.cpp
│   │   ├── cmd_commit_tree.cpp
│   │   ├── cmd_snapshot.cpp
│   │   ├── cmd_rev_parse.cpp
│   │   ├── cmd_log.cpp
│   │   ├── cmd_status.cpp
│   │   └── cmd_restore.cpp
│   ├── core/
│   │   ├── bytes.h               # using Bytes = std::vector<std::uint8_t>; ByteView = std::span<const std::uint8_t>
│   │   ├── error.h               # class Error : public std::runtime_error
│   │   ├── sha1.h / sha1.cpp
│   │   ├── object_id.h / object_id.cpp
│   │   ├── zlib_codec.h / zlib_codec.cpp
│   │   ├── object.h / object.cpp           # ObjectType、物件 header 編解碼
│   │   ├── tree.h / tree.cpp
│   │   ├── commit.h / commit.cpp
│   │   ├── object_database.h / object_database.cpp
│   │   ├── refs.h / refs.cpp
│   │   ├── config.h / config.cpp           # git config ini 子集
│   │   ├── repository.h / repository.cpp
│   │   ├── ignore.h / ignore.cpp
│   │   ├── snapshot.h / snapshot.cpp       # 工作目錄 → tree
│   │   ├── materialize.h / materialize.cpp # tree → 工作目錄
│   │   ├── tree_diff.h / tree_diff.cpp
│   │   └── rev_parse.h / rev_parse.cpp
│   └── platform/
│       ├── fs.h
│       └── fs_posix.cpp
└── tests/
    ├── CMakeLists.txt
    ├── unit/
    │   ├── sha1_test.cpp
    │   ├── zlib_codec_test.cpp
    │   ├── object_test.cpp
    │   ├── tree_test.cpp
    │   ├── commit_test.cpp
    │   ├── object_database_test.cpp
    │   ├── refs_test.cpp
    │   ├── config_test.cpp
    │   ├── ignore_test.cpp
    │   └── tree_diff_test.cpp
    └── integration/
        ├── lib.sh                # 共用:建 temp dir、assert_eq、找 tva 執行檔
        ├── test_hash_object.sh
        ├── test_write_tree.sh
        ├── test_commit_log.sh
        ├── test_fsck.sh
        └── test_restore.sh
```

CMake target:`tva_core`(static library,包含 `src/core` 與 `src/platform`)、`tva`(執行檔,`src/main.cpp` + `src/cli`,link `tva_core`)、`tva_unit_tests`(GoogleTest)。每支整合腳本用 `add_test` 註冊,把 `tva` 執行檔路徑以環境變數 `TVA_BIN` 傳入。

## 2. 各檔案職責

### 通用約定

- namespace:`tva::core`、`tva::platform`、`tva::cli`。
- 錯誤:所有可預期的失敗都 throw `tva::core::Error`(訊息不含 `fatal:` 前綴)。`cli.cpp` 統一 catch,輸出 `fatal: <msg>` 到 stderr,exit code 128;CLI11 的參數錯誤 exit code 129(同 git 的 usage error)。
- 所有檔案內容一律當位元組處理,不做換行轉換,不做編碼轉換。

### `src/platform/fs.h` + `fs_posix.cpp`

OS 相關呼叫只能出現在這裡。

- `Bytes read_file(const std::filesystem::path& p);`
- `void write_file_atomic(const std::filesystem::path& p, ByteView data, std::filesystem::perms perms = default);`——在同目錄寫 `<name>.tmp.<pid>.<random>`,`fsync` 後 `rename` 到目標。
- `bool is_executable(const std::filesystem::path& p);`——看 owner exec bit(`S_IXUSR`)。
- `void set_executable(const std::filesystem::path& p, bool exec);`
- `std::string read_symlink(const std::filesystem::path& p);`
- `void create_symlink(const std::string& target, const std::filesystem::path& link);`
- `class LockFile`——RAII,語意同 git 的 lockfile:
  - `static std::optional<LockFile> try_acquire(const std::filesystem::path& target);`——以 `O_CREAT|O_EXCL` 建 `<target>.lock`,已存在回 `nullopt`。
  - `void write(ByteView data);`——寫入 lock 檔。
  - `void commit();`——`fsync` 後把 `<target>.lock` rename 成 `<target>`。
  - 解構時若沒 commit,刪掉 lock 檔。
- `struct FileStat { std::uint64_t size; std::int64_t mtime_ns; std::uint64_t inode; std::uint32_t mode; };` `FileStat lstat(const std::filesystem::path& p);`(M1 只定義,M3 的 stat cache 才用)

### `src/core/sha1.h`

自己實作 FIPS 180-4 SHA-1,不准呼叫任何外部 hash 函式庫。

- `class Sha1 { public: Sha1(); void update(ByteView data); std::array<std::uint8_t, 20> finish(); };`——串流式,內部 64-byte buffer,`finish` 做 padding(`0x80`、補零、64-bit big-endian bit length)。
- `std::array<std::uint8_t, 20> sha1(ByteView data);`

### `src/core/object_id.h`

- `struct ObjectId { std::array<std::uint8_t, 20> bytes; std::string hex() const; static std::optional<ObjectId> from_hex(std::string_view hex40); auto operator<=>(const ObjectId&) const = default; };`
- 提供 `std::hash<ObjectId>` 特化。

### `src/core/zlib_codec.h`

- `Bytes zlib_deflate(ByteView data);`——zlib 格式(含 header 與 adler32),不是 raw deflate、不是 gzip。
- `Bytes zlib_inflate(ByteView data);`——資料損毀時 throw `Error`。
- `std::pair<Bytes, std::size_t> zlib_inflate_prefix(ByteView data, std::size_t max_out);`——只解壓前 `max_out` bytes,給 `cat-file -t / -s` 讀 header 用。

### `src/core/object.h`

- `enum class ObjectType { Blob, Tree, Commit };`(tag 不在 M1 範圍,遇到時 throw `Error("unsupported object type: tag")`)
- `std::string_view to_string(ObjectType);` / `std::optional<ObjectType> parse_object_type(std::string_view);`
- `Bytes encode_loose(ObjectType type, ByteView payload);`——產生 `"<type> <payload 長度的十進位>\0" + payload`。
- `struct RawObject { ObjectType type; Bytes payload; };`
- `RawObject decode_loose(ByteView decompressed);`——驗證 header 格式與長度一致,不一致 throw。
- `ObjectId hash_object(ObjectType type, ByteView payload);`——`sha1(encode_loose(...))`。

### `src/core/tree.h`

- `enum class FileMode : std::uint32_t { Regular = 0100644, Executable = 0100755, Symlink = 0120000, Directory = 040000 };`
- `struct TreeEntry { FileMode mode; std::string name; ObjectId id; };`
- `struct Tree { std::vector<TreeEntry> entries; };`
- `Bytes serialize_tree(const Tree& tree);`——規格:
  - 每個 entry:`<mode 八進位 ASCII,無前導零> <name>\0<20 bytes 原始 hash>`。目錄 mode 寫成 `40000`,不是 `040000`。
  - entries 依名稱的位元組序排序,**但目錄名比較時視為尾端多一個 `/`**。例:檔案 `a.txt` 與目錄 `a` 比較時用 `a.txt` vs `a/`。
  - name 不得為空、不得含 `/` 或 `\0`、不得為 `.` 或 `..`,違反 throw。
  - 同名 entry 重複 throw。
- `Tree parse_tree(ByteView payload);`
- `bool tree_entry_less(const TreeEntry& a, const TreeEntry& b);`——上面的排序規則,單獨 export 以便單元測試。

### `src/core/commit.h`

- `struct Signature { std::string name; std::string email; std::int64_t when_unix; int tz_offset_minutes; };`
- `std::string format_signature(const Signature&);`——`Name <email> 1700000000 +0800`,時區 `±HHMM`。
- `Signature parse_signature(std::string_view line_after_keyword);`
- `struct Commit { ObjectId tree; std::vector<ObjectId> parents; Signature author; Signature committer; std::string message; };`
- `Bytes serialize_commit(const Commit&);`——規格:
  ```
  tree <hex>\n
  parent <hex>\n      (0 到多行,依序)
  author <signature>\n
  committer <signature>\n
  \n
  <message>
  ```
  message 若不是以 `\n` 結尾,自動補一個 `\n`。
- `Commit parse_commit(ByteView payload);`——遇到不認得的 header 行(例如 `gpgsig`、`encoding`)要略過而不是失敗,包含以空格開頭的續行。

### `src/core/object_database.h`

- `class ObjectDatabase { public: explicit ObjectDatabase(std::filesystem::path objects_dir); ... };`
  - `ObjectId write(ObjectType type, ByteView payload);`——計算 id;若物件已存在直接回傳(不重寫);否則 `zlib_deflate(encode_loose(...))`,用 `write_file_atomic` 寫到 `objects/<前 2 hex>/<後 38 hex>`,權限 `0444`,目錄不存在就建。
  - `std::optional<RawObject> read(const ObjectId& id) const;`——讀檔、inflate、decode,並**重新 hash 驗證**內容與 id 一致,不一致 throw `Error("corrupt object <hex>")`。
  - `std::optional<std::pair<ObjectType, std::size_t>> read_header(const ObjectId& id) const;`——用 `zlib_inflate_prefix` 只解 header。
  - `bool exists(const ObjectId& id) const;`
  - `std::vector<ObjectId> resolve_prefix(std::string_view hex_prefix) const;`——prefix 至少 4 個 hex 字元,掃描對應的 `objects/xx/` 目錄。
  - `ObjectId write_blob(ByteView)`、`ObjectId write_tree(const Tree&)`、`ObjectId write_commit(const Commit&)`,以及對應的 `Tree read_tree(const ObjectId&)`、`Commit read_commit(const ObjectId&)`(型別不符 throw)。

### `src/core/refs.h`

M1 只處理 loose ref,不讀 `packed-refs`。

- `struct RefValue { std::variant<ObjectId, std::string> value; };`——`std::string` 代表 symbolic ref 的目標(例如 `refs/heads/main`)。
- `class RefStore { public: explicit RefStore(std::filesystem::path git_dir); ... };`
  - `std::optional<RefValue> read_raw(std::string_view name) const;`——檔案內容是 40 hex + `\n`,或 `ref: <target>\n`。
  - `std::optional<ObjectId> resolve(std::string_view name) const;`——跟著 symbolic ref 走,最多 5 層,超過 throw。
  - `std::optional<std::string> symbolic_target(std::string_view name) const;`
  - `void update(std::string_view name, const ObjectId& new_id, std::optional<ObjectId> expected_old);`——用 `LockFile` 取得 `<ref>.lock`,在持有 lock 時重新讀目前值,與 `expected_old` 不符(包括「預期不存在但已存在」)throw `Error("ref <name> changed concurrently")`;拿不到 lock throw `Error("unable to lock ref <name>")`。
  - `void set_symbolic(std::string_view name, std::string_view target);`
  - `std::vector<std::pair<std::string, ObjectId>> list(std::string_view prefix) const;`
  - ref 名稱驗證:必須以 `refs/` 開頭或等於 `HEAD`;不得含 `..`、空白、`~^:?*[\`,不得以 `/` 或 `.lock` 結尾。

### `src/core/config.h`

git config 的 ini 子集,只讀不寫。

- `class Config { public: static Config load_files(const std::vector<std::filesystem::path>& files); std::optional<std::string> get(std::string_view section, std::string_view key) const; };`
- 支援 `[section]`、`[section "sub"]`、`key = value`、`#` / `;` 註解、值的前後空白去除、雙引號包住的值。section 與 key 不分大小寫。
- 讀取順序:`~/.gitconfig` → `<git_dir>/config`,後者覆蓋前者。

### `src/core/repository.h`

- `class Repository { public: ... };`
  - `static Repository init(const std::filesystem::path& worktree_root);`——worktree 已有 `.git` 就 throw。建立:
    - `.git/HEAD`:`ref: refs/heads/main\n`
    - `.git/objects/`、`.git/refs/heads/`、`.git/refs/tags/`
    - `.git/config`:
      ```
      [core]
      	repositoryformatversion = 0
      	filemode = true
      	bare = false
      ```
    - `.git/info/exclude`:內容 `.tva/\n`
    - `.tva/`(空目錄)
  - `static Repository discover(const std::filesystem::path& start);`——從 `start` 往上找含 `.git/` 目錄的資料夾,找不到 throw `Error("not a tva repository (or any of the parent directories)")`。
  - `const std::filesystem::path& worktree_root() const;`、`const std::filesystem::path& git_dir() const;`、`const std::filesystem::path& tva_dir() const;`
  - `ObjectDatabase& odb();`、`RefStore& refs();`、`const Config& config() const;`
  - `Signature current_signature() const;`——依序取 `GIT_AUTHOR_NAME` / `GIT_AUTHOR_EMAIL` / `GIT_AUTHOR_DATE` 環境變數,再取 config 的 `user.name` / `user.email`;name 或 email 缺任一個 throw `Error("author identity unknown; set user.name and user.email")`。時間為現在,時區為本機時區。`GIT_AUTHOR_DATE` 只需支援 `<unix 秒數> <±HHMM>` 格式(測試要用它固定 hash)。committer 同理讀 `GIT_COMMITTER_*`,沒設就等於 author。

### `src/core/ignore.h`

- `class IgnoreMatcher { public: static IgnoreMatcher load(const std::filesystem::path& worktree_root); bool is_ignored(std::string_view rel_path, bool is_dir) const; };`
  - `rel_path` 一律用 `/` 分隔、相對於 worktree 根目錄。
  - 永遠忽略:`.git`、`.tva`(任何層級名稱完全相符的目錄)。
  - 讀根目錄與每一層子目錄的 `.gitignore`,以及 `.git/info/exclude`。子目錄 `.gitignore` 的 pattern 相對於該目錄。
  - 支援語法:空行、`#` 註解、`\#` 與 `\!` 跳脫、尾端未跳脫空白去除、`!` 取消忽略、尾端 `/` 只匹配目錄、pattern 中含 `/`(不在尾端)時錨定於該 `.gitignore` 所在目錄、`*`(不跨 `/`)、`?`、`[abc]` / `[a-z]` / `[!a]`、`**/` 開頭、`/**` 結尾、`/**/` 中間。
  - 規則:後出現的 pattern 優先;**父目錄已被忽略時,其下檔案無法用 `!` 救回**(snapshot 走訪時直接不進入被忽略的目錄,自然達成)。
- `bool glob_match(std::string_view pattern, std::string_view path);`——單獨 export 供單元測試。

### `src/core/snapshot.h`

- `ObjectId snapshot_worktree(Repository& repo);`——遞迴走訪 worktree,遇到被忽略的目錄就不進入;每個一般檔案寫成 blob(mode 依 exec bit 決定 `Regular` / `Executable`),symlink 寫成 blob(內容 = link 目標字串,mode `Symlink`,不跟隨 link);由下而上組 tree 並寫入;空目錄(或只含被忽略檔案的目錄)不產生 entry;回傳根 tree id。根目錄完全是空的時,回傳空 tree 的 id。
- 走訪時遇到無法讀取的檔案 throw,訊息帶路徑。

### `src/core/materialize.h`

- `void materialize_tree(Repository& repo, const ObjectId& target_tree, const ObjectId& current_tree);`——把工作目錄從 `current_tree` 改成 `target_tree`:
  - 用 `tree_diff` 算出差異;只動差異裡的路徑。
  - Added / Modified:寫檔(`write_file_atomic`)、設定 exec bit,或建 symlink;必要時建父目錄;若目標路徑上有型別衝突(原本是檔案、現在要是目錄),先移除舊的。
  - Deleted:刪檔,之後往上把變成空的目錄刪掉(停在 worktree 根)。
  - 被忽略的檔案永遠不動。

### `src/core/tree_diff.h`

- `enum class ChangeKind { Added, Deleted, Modified, ModeChanged };`
- `struct PathChange { ChangeKind kind; std::string path; std::optional<TreeEntry> old_entry; std::optional<TreeEntry> new_entry; };`
- `std::vector<PathChange> diff_trees(const ObjectDatabase& odb, std::optional<ObjectId> old_tree, std::optional<ObjectId> new_tree);`——同時走訪兩棵已排序的 tree(merge-join),**子樹 id 相同就整棵跳過不展開**;檔案變目錄或目錄變檔案,拆成 Deleted + Added。結果依路徑排序。`nullopt` 視為空 tree。

### `src/core/rev_parse.h`

- `ObjectId rev_parse(const Repository& repo, std::string_view spec);`——依序嘗試:40 hex 完整 id → `HEAD` → `refs/` 開頭的完整 ref → `refs/heads/<spec>` → `refs/tags/<spec>` → 至少 4 個字元的 hex prefix(多於一個符合 throw `Error("short object ID <spec> is ambiguous")`)。M1 支援後綴 `~<n>` 與 `^`(first parent)。

### `src/cli/`

`cli.h`:`int run(int argc, char** argv);` `main.cpp` 只呼叫它。每個 `cmd_*.cpp` 提供 `void register_<name>(CLI::App& app);`。輸出格式盡量與 git 相同,以便整合測試直接比對。

| 指令 | 行為 |
|---|---|
| `tva init [<dir>]` | `Repository::init`,印 `Initialized empty TVA repository in <abs>/.git/` |
| `tva hash-object [-w] [-t <type>] <file>` | 印 id;`-w` 時寫入物件庫(需在 repo 內);預設 blob |
| `tva cat-file (-t\|-s\|-p) <rev>` | `-t` 印型別,`-s` 印大小,`-p` 印內容:blob 原樣輸出;tree 每行 `<6 位數 mode> <type> <hex>\t<name>`(mode 補成 6 位,例如 `040000`,與 `git cat-file -p` 相同);commit 原樣輸出 payload |
| `tva write-tree` | `snapshot_worktree`,印根 tree id |
| `tva commit-tree <tree> [-p <parent>]... -m <msg>` | 建 commit,印 id,不動任何 ref |
| `tva snapshot -m <msg>` | 測試用 porcelain:write-tree;若與 HEAD 的 tree 相同就印 `nothing changed` 並 exit 0;否則以 HEAD(若存在)為 parent 建 commit,用 `RefStore::update` 以 CAS 更新 HEAD 指向的 branch;印 `[<branch> <short id>] <message 第一行>` |
| `tva rev-parse <rev>` | 印完整 id |
| `tva log [<rev>]` | 從 rev(預設 HEAD)沿 first parent 往回走;每個 commit 印 `commit <hex>`、`Author: <name> <email>`、`Date: <本機時間>`、空行、縮排 4 格的 message;`--oneline` 印 `<7 碼 short id> <message 第一行>` |
| `tva status` | 對 worktree 做 snapshot(物件照寫,反正 content-addressed),跟 HEAD 的 tree 做 `diff_trees`,每行 `A <path>` / `D <path>` / `M <path>` / `T <path>`(mode 變更);無差異印 `clean` |
| `tva restore <rev> [--force]` | 先 snapshot 目前 worktree 並比對 HEAD tree;不一致且沒 `--force` 時 throw `Error("worktree has unsnapshotted changes; run tva snapshot or use --force")`;否則 `materialize_tree(target, current_snapshot)`。M1 的 `restore` 不移動 HEAD |

## 3. 執行順序

每一步完成、checkpoint 通過後才進下一步。

1. **建置骨架**:`CMakeLists.txt`、`cmake/dependencies.cmake`、`tests/CMakeLists.txt`、`main.cpp` + `cli.cpp`,只有 `tva --version`(印 `tva 0.1.0`),外加一個空的 GoogleTest。
   ✅ checkpoint:`cmake --build build && ./build/tva --version` 印出 `tva 0.1.0`;`ctest --test-dir build` 通過。
2. **`bytes.h`、`error.h`、`sha1`**。
   ✅ checkpoint:`sha1_test` 通過以下測試向量:`""` → `da39a3ee5e6b4b0d3255bfef95601890afd80709`;`"abc"` → `a9993e364706816aba3e25717850c26c9cd0d89d`;一百萬個 `'a'` → `34aa973cd4c4daa4f61eeb2bdbad27316534016f`;另外對 0–200 bytes 長度的隨機資料,逐 byte `update` 與一次 `update` 結果相同(驗證 block 邊界與 padding)。
3. **`zlib_codec`**。
   ✅ checkpoint:空資料、1 byte、1 MiB 隨機資料 deflate → inflate 往返相同;損毀資料 inflate 時 throw。
4. **`object_id`、`object`、`object_database`(只有 blob)、`platform/fs`、`refs` 的 read 部分、`repository::init/discover`、`tva init`、`tva hash-object`、`tva cat-file`(blob)**。
   ✅ checkpoint:`tests/integration/test_hash_object.sh`:在 temp dir 執行 `tva init`,寫一個 `printf 'hello world\n' > a.txt`,`tva hash-object a.txt` 的輸出與 `git hash-object a.txt` 相同;`tva hash-object -w a.txt` 之後,**真 git** 執行 `git cat-file -p <id>` 印出 `hello world`;再測空檔案與 1 MiB 二進位檔。
5. **`tree`、`ignore`、`snapshot`、`tva write-tree`、`cat-file` 支援 tree**。
   ✅ checkpoint:
   - `tree_test` 包含「檔案 `a.txt`、目錄 `a`、檔案 `a-b`」的排序案例(正確順序是 `a-b`、`a.txt`、`a`——因為 `-`(0x2D)< `.`(0x2E)< `/`(0x2F))。
   - `ignore_test` 涵蓋上面列出的每種語法,至少各一正一反案例。
   - `tests/integration/test_write_tree.sh`:建一個測試目錄,內含巢狀目錄、可執行檔、symlink、空目錄、根目錄與子目錄的 `.gitignore`(含 `!` 與 `**`)、被忽略的 `build/`。複製成兩份:A 份 `tva init && tva write-tree`;B 份 `git init && git add -A && git write-tree`。兩個 tree id 必須相同。
6. **`commit`、`config`、`refs::update` / `set_symbolic`、`Repository::current_signature`、`rev_parse`、`tva commit-tree`、`tva snapshot`、`tva rev-parse`、`tva log`、`cat-file` 支援 commit**。
   ✅ checkpoint:
   - `commit_test`:serialize → parse 往返;parse 帶 `gpgsig` 多行 header 的 commit 不失敗。
   - `refs_test`:CAS 成功;expected_old 不符時 throw;lock 檔已存在時 throw,且不留下殘檔。
   - `tests/integration/test_commit_log.sh`:固定 `GIT_AUTHOR_NAME/EMAIL/DATE` 與 `GIT_COMMITTER_*`,用 `tva snapshot -m` 連做三次(中間改檔);`tva log --oneline` 的 id 與真 git 的 `git log --format=%h` 一致(順序相同);再用同樣的環境變數,在另一個目錄以真 git 做出相同的三次 commit,兩邊 HEAD id 完全相同。
   - `tests/integration/test_fsck.sh`:對上面的 repo 執行 `git fsck --full --strict`,exit code 0 且無任何 error 輸出。
7. **`tree_diff`、`materialize`、`tva status`、`tva restore`**。
   ✅ checkpoint:
   - `tree_diff_test`:新增 / 刪除 / 修改 / mode 變更 / 檔案變目錄 / 相同子樹被跳過(用一個會記錄 `read_tree` 呼叫次數的測試包裝確認)。
   - `tests/integration/test_restore.sh`:snapshot A → 修改一個檔、刪一個檔、新增一個巢狀檔、把一個檔改成可執行、新增被忽略的 `build/out.o` → snapshot B → `tva restore <A>` → `tva write-tree` 的輸出等於 A 的 tree id,`build/out.o` 仍在,新增巢狀檔所在的空目錄已被刪除;在未 snapshot 的修改下執行 `tva restore <A>`(不加 `--force`)exit code 128,且工作目錄沒有任何變動。

## 4. 怎麼驗證跑通

M1 完成的判定:以下全部在一台乾淨的 Linux(或 WSL2)上通過。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTVA_SANITIZE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

- 期望:所有單元測試與 5 支整合腳本通過,ASan / UBSan 沒有任何報告。
- 失敗長相:整合腳本會印出 `expected <git 的值> got <tva 的值>` 與失敗的步驟名稱;hash 不一致時先檢查 tree 排序、目錄 mode 是否寫成 `40000`、commit message 是否補了結尾 `\n`、時區格式。

手動抽查(相容性的最終證明):

```bash
mkdir /tmp/tva-demo && cd /tmp/tva-demo
/path/to/build/tva init
echo 'int main(){}' > main.cpp
/path/to/build/tva snapshot -m "first"
git log --stat          # 真 git 能讀:看到 first 與 main.cpp
git fsck --full         # 無錯誤
```

注意:M1 不寫 `.git/index`,所以在 TVA repo 裡跑真 git 的 `git status` 會顯示所有檔案為「已刪除且未追蹤」——這是預期行為,M7 會處理。整合測試只用 `git cat-file`、`git log`、`git fsck`、`git write-tree`(在另一份副本裡)這些不依賴 index 的指令。
