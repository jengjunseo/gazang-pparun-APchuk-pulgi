# HYPER EXTRACT — Opus 5.5 구현 납품 + 역할 변경 반영 v0.4

- 날짜: 2026-10-24
- 작성: Opus 5.5
- 수신: GPT-6, 프로젝트 사용자
- 근거 문서: v0.1 명세, v0.2 Opus 설계 제언, v0.3 GPT-6 합의·수정 지시, 사용자 환경 정보와 방향 전환 지시(2026-10-24)
- 성격: **실행 가능한 첫 납품**(OutputSink + PathGuard + DirPlanner + AutoTune) + 역할 변경 확정 + ZIP 엔진 연결 계약
- `perf-claim: none` — 이 문서와 패키지에 성능 수치는 없다. 수치는 사용자 PC 로그(`hx-results`)로만 확정한다.

> **정직성**
> - Opus와 GPT-6는 직접 통신하지 않는다. 사용자가 문서·소스·로그를 전달한다.
> - `hx-tune.exe`는 리눅스 샌드박스에서 **크로스 컴파일**했다(zig 0.17 / clang, `x86_64-windows-gnu`, static). **Windows에서 실행해 본 적은 없다.** Windows 전용 경로(OutputSink, RootGuard, trial, selftest)의 동작은 사용자 PC의 `selftest.log`로 처음 확인된다.
> - OS와 무관한 코어(PathGuard, DirPlanner, stats, CRC32)는 샌드박스에서 단위 테스트, ASan/UBSan, 무작위 속성 테스트를 통과했다(§6).

---

## 0. 방향 전환 (사용자 결정, 2026-10-24) — 확정

| 항목 | v0.3 | v0.4 (현재) |
|---|---|---|
| 첫 우선순위 | TICKET-001 bench-runner (경쟁 제품 측정) | **OutputSink + AutoTune** (Opus), **ZIP 엔진** (GPT-6) |
| 경쟁 제품(7-Zip/Bandizip/tar) 측정 | 지금 | **마지막 공정 비교 단계로 연기** (TICKET-001 보류) |
| 사용자 역할 | 여러 도구 반복 측정 | **`run_hx_tune.cmd` 실행 → `hx-results` 폴더 전달**만 |
| 목표 표현 | "세계 최고속" 검증 | 그전까지는 "**이 PC에서 실측으로 최적화된 엔진**". 세계 최고속 주장은 최종 공정 비교 전 금지 |

**측정을 없앤 것이 아니다.** bench-runner에서 쓰려던 계측·검증·통계 코드를 AutoTune 안으로 옮겼다. AutoTune은 프로그램이 직접 여러 설정을 실측하고, 판정 규칙(v0.3 §3.4)을 그대로 적용해 설정을 고른다.

유지되는 v0.3 결정: 3% + bootstrap CI 판정, 결정적 충돌 이름 변경, `finish/drain` 계약, bounded queue, 새 루트 폴더 필수, 시스템 설정 무변경, GPU 보류, 7z/RAR는 ZIP 이후.

---

## 1. 납품물

패키지 `HYPER_EXTRACT_hx-tune_v0.1.0-opus.zip`

```
hx-tune.exe            Windows x64 콘솔 exe (static, 서명 없음)
run_hx_tune.cmd        selftest → tune 일괄 실행
README.md              사용자 안내
SHA256SUMS.txt
docs/HYPER_EXTRACT_Opus55_구현납품_v0.4.md   (이 문서)
src/                   전체 소스 (C++20)
tests/                 OS 무관 단위 테스트 + PathGuard 속성 테스트
CMakeLists.txt, CMakePresets.json
```

| 티켓 | 상태 | 비고 |
|---|---|---|
| TICKET-004 PathGuard + DirPlanner | **구현 + 테스트 통과** | 샌드박스 실행 |
| TICKET-005 OutputSink (IO-1, IO-2, IO-3) | **구현, 컴파일됨, Windows 미실행** | selftest로 검증 예정 |
| AutoTune (신규) | **구현, 컴파일됨, Windows 미실행** | |
| TICKET-001 bench-runner | **보류** (사용자 결정) | 최종 공정 비교 단계에서 재개 |
| TICKET-003 7z/RAR | 대기 | ZIP 성능 입증 후 |

---

## 2. 확정된 공용 계약 (GPT-6가 ZIP 엔진에서 사용할 것)

헤더 위치: `src/core/status.h`, `src/core/types.h`, `src/core/output/output_sink.h`, `src/core/pathguard.h`, `src/core/dirplan.h`

### 2.1 타입

```cpp
namespace hx {
enum class Err { Ok, Win32, InvalidArg, InvalidState, Rejected, Collision,
                 LimitExceeded, Cancelled, Verify, Internal };
struct Status { Err code; uint32_t win32; std::string where; bool ok() const; };
template<class T> struct Result { T value; Status status; bool ok() const; };

struct EntryMeta {
    uint32_t index;                    // 아카이브 내 안정적 id
    std::vector<std::byte> raw_name;   // 원본 바이트. 인코딩 가정 없음 (v0.3 §2.3)
    std::wstring name;                 // 디코딩된 이름 — PathGuard 입력
    uint64_t declared_size;            // 신뢰하지 않음 (힌트)
    uint64_t mtime_filetime;           // 0 = 없음
    uint32_t win_attrs;                // 0 = 없음
    bool is_dir, is_symlink;
};
struct SafePath { uint32_t dir_id; std::wstring leaf; uint32_t entry_index; };
}
```

**ZIP 쪽 책임:** `raw_name` → `name` 디코딩(UTF-8 플래그 → CRC 검증된 Unicode Path extra 0x7075 → CP949/오버라이드 순, v0.3 §2.3). `\`/`/` 정규화와 안전 검사는 **PathGuard가 한다**(ZIP 쪽에서 중복 구현 금지).

### 2.2 경로 계획 — 병렬 실행 전에 한 번

```cpp
hx::DirPlanner dirs;
hx::PathPlan plan;
hx::Status s = hx::plan_paths(entries, hx::GuardPolicy{}, dirs, plan);
// plan.entries: entry_index 오름차순, 각 항목 verdict = ok|sanitized|renamed|skipped|rejected
// final_rel: original_entry_index → final_relative_path 매니페스트용
```

- `rejected`는 개별 항목 상태다. 아카이브 전체 실패 여부는 호출자 정책(기본: 하나라도 rejected면 전체 실패 보고, 나머지는 계속할지 `--on-reject` 옵션으로 GPT-6가 결정).
- 충돌 해소는 **entry index 순서로 결정적**이다. 스레드 순서와 무관.

### 2.3 OutputSink 사용 패턴

```cpp
hx::RootGuard rg;  rg.create_fresh(root);          // 새 폴더 필수, 이미 있으면 실패
hx::SinkConfig sc = profile.write_path;            // AutoTune 결과(§4.3)
hx::Win32Sink sink(sc, dirs, root);
dirs.materialize(root);                            // sc.lazy_dirs == false 일 때

// 워커 스레드 (서로 다른 토큰은 완전 병렬)
auto r = sink.open(pe.path, entry.declared_size);
if (!r.ok()) ...                                   // 실패 원인은 drain()에도 집계됨
Status ws = sink.write(r.value, span);             // 반환 시 버퍼 소비 완료 → 즉시 재사용 가능
Status fs = sink.finish(std::move(r.value), entry, ws.ok() && crc_ok);
                                                   // ok=false → 파일은 삭제되고 drain에서 확인
// 모든 워커 join 후
Status d = sink.drain();                           // 모든 close 완료 + 오류 집계. 이것이 T_done 끝점
```

계약 요약 (v0.3 §2.1 반영):

- `FileToken`은 move-only. `finish(FileToken&&)`가 토큰을 소비한다. 중복 finish, finish 후 write → `InvalidState`.
- `finish()`는 대개 빠르게 반환하지만 **close 큐가 차면 대기**(backpressure)할 수 있다. 대기 시간은 `SinkStats::ns_finish_queue_wait`.
- `finish(ok=false)`: 열린 핸들에 `FileDispositionInfo(DeleteFile=TRUE)` 설정 → close → **존재하지 않음을 확인**. 설정이 실패하면 close 후 `DeleteFile`로 대체. **열린 독점 핸들 상태에서 DeleteFile하지 않는다.**
- finish 없이 파괴된 토큰 → 자동 discard + 오류 기록.
- `drain()` 시점에 열린 토큰이 남아 있으면 오류.
- 크기 상한: 항목당 `max_entry_bytes`(기본 8GiB), 전체 `max_total_bytes`(기본 256GiB, 원자적 누적). **선언 크기가 아닌 실제 쓴 바이트 기준.** ZIP 폭탄 1차 방어선이며, 압축비 기반 조기 차단은 ZIP 디코더 쪽에서 추가 권장.
- `CreateFileW`는 기본 `CREATE_NEW` — 덮어쓰기 없음.
- `SinkStats`의 `ns_*`는 **스레드 누적 시간**이다. `T_done`에서 빼거나 백분율로 해석하지 않는다(v0.3 §3.3).

### 2.4 스레드 안전성

| 객체 | 규칙 |
|---|---|
| `DirPlanner`, `PathPlan` | 계획 단계(단일 스레드)에서 생성, 이후 **읽기 전용 공유** |
| `Win32Sink` | `open/write/finish/stats` 다중 스레드 안전. 같은 토큰의 write는 호출자가 직렬화 |
| `drain()` | 모든 생산자 스레드가 끝난 뒤 한 번 호출 |

---

## 3. 구현 상세

### 3.1 PathGuard (TICKET-004)

| 입력 | 처리 |
|---|---|
| `..` 포함, `\` 시작(절대/UNC/`\\?\`), `X:` 드라이브 | **rejected** |
| `:` 포함 (ADS) | rejected |
| 구성요소 > 255 UTF-16, 상대경로 > 30,000, 깊이 > 256 | rejected |
| 제어문자, `<>"|?*` | `_`로 치환 → sanitized |
| 끝의 `.`/공백 | `_`로 치환 (Win32가 조용히 잘라서 생기는 숨은 충돌 방지) |
| `CON PRN AUX NUL COM0-9 LPT0-9 COM¹²³ LPT¹²³ CONIN$ CONOUT$` (확장자·뒤 공백 무관) | 앞에 `_` 추가 |
| `/` | `\`로 정규화 |
| `.`, 빈 구성요소 (`a//b`, `./a`) | 제거 |
| 심볼릭 링크 항목 | **skipped** (생성하지 않음) |
| 대소문자 무시 충돌 (파일-파일, 파일-폴더, 폴더-파일, 정규화 후 충돌) | 뒤 항목을 `name (2).ext`, `(3)`…; 폴더는 `dir (2)`. `CollisionPolicy::Fail`이면 rejected |
| 같은 폴더가 대소문자만 다르게 여러 번 등장 (`A/x`, `a/y`) | **같은 폴더로 병합** (Windows 동작과 일치) |

대소문자 접기: Windows에서는 `LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE)`. NTFS `$UpCase` 테이블과 완전히 같다고 보장하지는 않으므로, 실제 생성 시 `CREATE_NEW`가 2차 방어선이다(충돌 시 오류로 드러나며 덮어쓰지 않음).

### 3.2 RootGuard (TOCTOU 대응, v0.3 §2.3)

- 루트는 `CreateDirectoryW`로 **새로** 만든다. 이미 있으면 실패.
- 만든 직후 `FILE_FLAG_OPEN_REPARSE_POINT`로 열어 일반 디렉터리인지(reparse 아님) 확인하고, **`FILE_SHARE_DELETE` 없이 핸들을 쥐고 있어** 작업 중 루트의 이름 변경·교체를 막는다.
- 하위 폴더는 모두 HX가 만든다. 미리 만들기(planned) 모드에서는 하위 폴더가 이미 있으면 외부 개입으로 보고 오류 처리한다.
- **남은 위험:** 루트 내부에서 같은 사용자 권한의 다른 프로세스가 동시에 하위 폴더를 junction으로 바꾸는 경우. planned 모드는 `ERROR_ALREADY_EXISTS`로 감지하지만 lazy 모드는 감지하지 못한다. 완전 차단(핸들 기준 `NtCreateFile` + `RootDirectory`)은 IO-후보로 남긴다.

### 3.3 OutputSink 변형

| ID | 내용 | 설정 |
|---|---|---|
| IO-1 | `finish()` 안에서 `CloseHandle` | `closer_threads = 0` |
| IO-2 | bounded MPSC 큐 + K개 closer 스레드 | `closer_threads = K`, `queue_capacity = 1024` |
| IO-3 | 폴더 선생성(planned) vs 파일마다 생성(lazy) | `lazy_dirs` |

IO-4~IO-10(사전 할당, `FILE_FLAG_SEQUENTIAL_SCAN`/no-buffering, overlapped, 쓰기 병합, 핸들 기반 상대 생성 등)은 **미구현**. AutoTune 1차 결과로 우선순위를 정한다.

---

## 4. AutoTune (`hx-tune tune`)

### 4.1 측정 대상과 한계

- **쓰기 경로만** 측정한다. 디코딩된 내용을 메모리에 미리 만들어 두고(시간 밖) OutputSink로 실제 디스크에 쓴다. v0.3 §3.3의 `--diag write`와 같은 성격이다.
- `T_done` = 폴더 생성 직전 ~ 모든 워커 join + `drain()` 반환(모든 핸들 닫힘, discard 확인). QPC 기반, 프로세스 내부 측정. 데이터 생성, closer 스레드 생성, 검증, 정리는 포함하지 않는다.
- **ZIP 디코딩이 들어가면 최적 W가 달라질 수 있다.** 디코딩과 쓰기가 같은 코어를 나눠 쓰기 때문이다. 따라서 이번 profile은 "쓰기 경로 profile"이고, ZIP 통합 후 디코드 포함 단계(§5.3)에서 W를 재탐색한다.

### 4.2 데이터셋

| ID | 구성 | 목적 |
|---|---|---|
| A-synth | 평면, 10,104 파일, 합계 287,000,000 B, 크기 log-normal(σ=0.9), 영문+한글 UTF-8 텍스트형 | 데이터셋 A와 같은 형태 |
| D-synth | 4,000 + 8개(8MiB) 파일, 96MB, 한글 이름 다단 폴더 | 폴더 전략(IO-3) 비교 |
| A-real (선택) | `--source DIR` — 실제 폴더를 **읽기 전용**으로 메모리에 로드 | 실제 크기 분포 |

데이터 생성은 시드 고정이라 결정적이다. 파일 내용 자체는 로그에 남지 않는다.

### 4.3 탐색 절차

```
S0 warmup        W4-K4 1회 (버림)
S1 closers       K ∈ {0,1,2,4,8,16} @ W=4             블록 3회 (quick 2), 블록마다 순서 셔플
S2 workers       W ∈ {1,2,3,4,6,8} @ best K           블록 3회
S3 confirm       best, 이웃(K±, W±), 기준선 W1-K0(naive), W4-K0
                 새 블록 8회 (quick 4), paired bootstrap
S4 dirs          선택 설정으로 planned vs lazy on D-synth, 블록 6회
```

판정(v0.3 §3.4 그대로):
- 한 번이라도 검증에 실패한 설정은 **실격**.
- S3 최속(fastest) 대비 각 설정을 paired bootstrap 95% CI + 3% 임계로 판정.
- **선택(pick) = fastest와 `tie_practical`인 설정 중 가장 단순한 것**(W+K 최소). 리소스가 적고 Defender/백그라운드 변동에 덜 민감하기 때문이다.
- lazy가 planned를 `wins`로 이길 때만 lazy 선택. 기본은 planned.
- `confidence`: 기본 high / inconclusive 있으면 medium / 예산 초과로 단계를 줄였거나 실패 회차가 있었으면 low.
- 시간 예산 기본 40분. 넘을 것 같으면 반복 수를 줄이고 notes에 기록한다.

### 4.4 출력 (`hx-results/`)

| 파일 | 내용 |
|---|---|
| `selftest.log` | 정확성 검사 |
| `env.json` | OS/빌드, CPU, 논리 프로세서, RAM, 볼륨 FS·여유 공간, Defender 실시간 상태(레지스트리 읽기만), 컴파일러 |
| `results.jsonl` | 회차별: stage, block, config, valid, `t_done_ms`, 오류, 누적 스레드 시간(create/write/setinfo/close/queue_wait), 큐 최대 깊이 |
| `summary.md` | S3 표: n, median, p90, MAD, fastest 대비 차이와 CI, 판정 |
| `profile.json` | 선택된 `write_path {workers, closers, dirs, batch_files, batch_bytes, queue_capacity}` + 판정 근거 + 환경 fingerprint |

`profile.json`은 `%LOCALAPPDATA%\HyperExtract\profile.json`에도 저장된다. **ZIP 엔진은 이 파일을 읽어 기본값으로 사용한다**(§5.2). fingerprint(CPU, 논리 프로세서, OS 빌드, 볼륨, FS, Defender 상태, hx 버전)가 다르면 무시하고 기본값 W4-K4-planned 사용.

### 4.5 안전 장치

- 시스템 설정 무변경. 이 프로세스에만 `ProcessPowerThrottling` 옵트아웃(EcoQoS 해제)을 적용하고, 그 사실을 `env.json`에 기록한다.
- 작업 폴더: 절대경로만, `%WINDIR%`, `Program Files`, `ProgramData` 내부 금지. 기본 `%LOCALAPPDATA%\HyperExtract\work`.
- 여유 공간 부족(필요량 + 5GB 미만)이면 시작 안 함.
- 정리: **계획에 있던 파일·폴더만** 삭제(Defender 삭제 대기 상태를 고려해 폴더 삭제는 최대 2초 재시도). 정리에 실패하면 즉시 중단하고 남은 폴더 경로를 출력한다.
- 관리자 권한 불필요, cold cache 측정 없음.

---

## 5. GPT-6에게 — ZIP 엔진 연결 요청

### 5.1 디렉터리 경계 (v0.3 §4.3 유지)

- GPT-6: `src/core/zip/*`, `src/core/scheduler/*`, `src/tools/zip-diag/*` (또는 `hx.exe` CLI)
- Opus: `src/core/pathguard*`, `src/core/dirplan*`, `src/core/output/*`, `src/autotune/*`
- 공용 헤더(`status.h`, `types.h`, `output_sink.h`) 변경은 변경안을 문서로 먼저 전달.

### 5.2 통합 순서 제안

1. ZIP 파서가 `std::vector<EntryMeta>`를 만든다(디렉터리 항목 포함, `index` = central directory 순서).
2. `plan_paths()` → `PathPlan`. rejected 처리 정책 결정.
3. `RootGuard::create_fresh()` → `Win32Sink` 생성(`profile.json`의 `write_path` 적용) → `materialize()`.
4. 스케줄러: 32 entry / 2MiB 배치(v0.3 §1-4), 작은 항목은 libdeflate whole-buffer → `write` 1회 → `finish(ok = crc_ok && size_ok)`.
5. 큰 항목(> `small_max` 1MiB)은 스트리밍 디코드 → 여러 번 `write`.
6. 모든 워커 join → `drain()` → 매니페스트(`entry_index → final_rel`, verdict) 출력.

### 5.3 AutoTune 확장 지점

`src/autotune/trial.cpp`의 `run_trial()`은 현재 `Dataset`(메모리 버퍼)을 쓴다. ZIP 엔진이 들어오면 다음 인터페이스로 일반화할 계획이다. GPT-6 의견 요청:

```cpp
// 제안 (미구현)
struct ExtractJob {               // 아카이브 1개를 HX 엔진으로 풀기
    std::wstring archive;
    EngineConfig cfg;             // workers, closers, dirs, batch, small_max, T_big, decoder
};
TrialResult run_extract_trial(const ExtractJob&, const Manifest& expected, ...);
```

- AutoTune의 데이터셋 A를 **실제 ZIP 파일**(합성 데이터를 HX가 stored/deflate로 직접 만들거나, 사용자 아카이브를 읽기 전용으로 사용)로 바꾸고, 같은 S1~S4 + 배치 크기 탐색을 디코드 포함으로 수행한다.
- 검증 기준(manifest)은 ZIP central directory의 CRC32 + 실제 크기 + PathGuard 최종 경로.

### 5.4 GPT-6 결정 요청 (4건)

| # | 질문 | Opus 제안 |
|---|---|---|
| 1 | rejected 항목이 있을 때 기본 동작 | 나머지는 해제하고 종료 코드 ≠ 0 + 매니페스트에 기록. `--on-reject=abort` 제공 |
| 2 | `profile.json` 스키마를 ZIP 엔진 설정(`batch`, `small_max`, `T_big`, decoder)까지 확장할지 | 확장. `hx_profile_version: 2`로 올리고 `zip` 섹션 추가 |
| 3 | 디코드 포함 AutoTune의 데이터셋 A 생성 방식 | HX 내장 ZIP writer(stored + libdeflate 압축)로 합성 ZIP 생성. 사용자 아카이브는 `--source-archive`로 선택 |
| 4 | ZIP 엔진 빌드 의존성(libdeflate, zlib-ng) 관리 | `third_party/`에 고정 버전 소스 포함 + CMake `add_subdirectory`. vcpkg 의존 없이 단일 exe |

---

## 6. 검증 상태

| 항목 | 결과 | 환경 |
|---|---|---|
| 코어 단위 테스트 `test_core` | **168 checks, 0 failures — PASS** | Linux, g++ 14.2 `-O2 -Wall -Wextra` |
| 같은 테스트 + AddressSanitizer/UBSan | **PASS** | Linux |
| PathGuard 무작위 속성 테스트 (3,000 아카이브) | **1,200,895 checks, 0 failures — PASS** (ASan/UBSan) | Linux |
| Windows 전체 크로스 컴파일 | 성공, 경고 출력 없음 (`-Wall -Wextra`) | zig 0.17 (clang), `x86_64-windows-gnu`, `-O2 -static` |
| `hx-tune.exe` | PE32+ x86-64 콘솔, 787,456 B, 의존 DLL: kernel32, advapi32, shell32, UCRT(Windows 10/11 기본 포함) | |
| Windows selftest / tune | **미실행** | 사용자 PC에서 첫 실행 |
| MSVC 빌드 | **미검증** | 사용자 PC에 MSVC 없음. CMakePresets 제공 |

속성 테스트가 검사하는 불변조건: 출력 경로에 `..`·`:`·예약 이름·금지 문자·끝 점/공백 없음, 최종 경로끼리 대소문자 무시 중복 없음, 파일 경로가 다른 파일의 폴더 접두사가 되지 않음, 같은 입력이면 항상 같은 결과(결정성), 모든 비거부 항목에 매핑 존재.

SHA-256 (`hx-tune.exe`): `SHA256SUMS.txt` 참조.

---

## 7. 사용자 실행 안내 (1회)

1. zip을 `C:` 아래 아무 폴더(예: `C:\hx`)에 풉니다.
2. `run_hx_tune.cmd`를 더블클릭합니다. 서명이 없는 exe라 SmartScreen이 경고하면 **추가 정보 → 실행**.
3. `[1/2] selftest`가 PASS여야 `[2/2] AutoTune`이 진행됩니다(약 15~40분). 그동안 무거운 프로그램은 닫아 주세요.
4. 끝나면 `hx-results` 폴더를 통째로 Opus와 GPT-6에게 전달합니다. 파일 내용, 사용자 이름, 드라이브 시리얼은 들어 있지 않습니다.

selftest가 실패하면 tune은 시작되지 않습니다. 그때는 `hx-results\selftest.log`만 보내 주세요.

### 이 PC에서 알려진 주의점

- **C: 여유 공간 8.8%.** 튜닝 중 동시 사용량은 약 0.3~0.4GB라 공간은 충분하지만, 여유 공간이 적으면 SSD의 SLC 캐시·GC 동작이 달라질 수 있다. `profile.json` notes에 자동 기록된다. 결과는 "측정 당시 상태" 기준이다.
- **Defender 실시간 보호 ON** — 그대로 둔다. 실제 사용 환경과 같으므로 오히려 맞는 조건이다.
- 7-Zip 미설치 — 이번 단계와 무관. 7z/RAR 단계에서 7z.dll 동반 배포로 해결.

---

## 8. Opus 다음 작업

1. 사용자 `hx-results` 수신 → selftest 실패가 있으면 최우선 수정.
2. AutoTune 결과로 IO-4~IO-10 중 다음 후보 2~3개 선정·구현.
3. GPT-6 ZIP 엔진 도착 시 `run_extract_trial` 일반화와 디코드 포함 AutoTune(§5.3).
4. (보류) 최종 공정 비교용 bench-runner — 7-Zip/Bandizip/tar 대비.
