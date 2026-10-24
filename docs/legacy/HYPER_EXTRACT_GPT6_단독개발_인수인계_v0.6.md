# HYPER EXTRACT — GPT-6 단독 개발 결과 v0.6

작성: 2026-10-24. 담당: GPT-6. `perf-claim: none`.

## 범위와 상태

- Opus v0.4 프로젝트 및 GPT-6 v0.5 ZIP 패치를 통합하고 개발을 단독 이어받았다.
- 이전 Opus 메시지에서 언급한 **합쳐진 작업공간 및 vendor 소스 파일은 실제로 전달되지 않았다.** 따라서 지금 전달된 v0.4 ZIP/v0.5 패치를 기반으로 병합했다. upstream 라이브러리의 소스가 없어서 부트스트랩 시 별도 다운로드가 필요하다.
- 작성/수정한 것은 **ZIP 실험적 엔진의 코드**이지 이미 검증된 세계 최고속 압축 해제 프로그램이 아니다. 사용자 PC 실행시간 수치는 현재 0건이다.

## 구현 및 변경

1. Windows ZIP 엔진 `zip_extract_win.cpp` 수정:
   - QPC 나노초 시계로 출력 루트 생성 직전부터 `drain()` 뒤까지 `T_done` 측정.
   - 호출자에게 `ExtractReport {t_done_ms, SinkStats, skipped}` 반환.
   - workers의 예외 수집 및 병렬 작업 전 ID 불변식 검증.
   - `SinkConfig`에 lazy_dirs, queue_capacity 연결.
2. Opus `Win32Sink::write()`의 `f->written + n` 및 `fetch_add` overflow 가능성 제거: 한도 검사 후 CAS 원자적 누적.
3. ZIP parser의 `NUL` 파일명 거부 및 ZIP64 구문 정돈.
4. `zip_writer.{h,cpp}` 구현: 디코더 테스트/AutoTune을 위한 Store/Deflate 합성 ZIP. 비-ZIP64 4GiB 미만 전용.
5. `zip_tune_win.cpp`: 합성 ZIP을 생성하고 W/K를 직접 반복 측정, 실제 디코딩·쓰기·닫기 + 외부 무결성 확인. 시험 디렉터리는 자체 생성한 것만 삭제. 통계는 후보 중앙값과 3% 단순성 규칙. **짝지은 부트스트랩 CI는 아직 연결되지 않았으며, 통계 확실성 주장은 금지.**
6. CLI `hx.exe tune-zip [--quick]` / `hx.exe x archive.zip -o NEW_ROOT` 및 AutoTune 결과 프로파일 자동 적용.
7. 빌드: vendored sources 우선 / Linux 시스템 라이브러리 / 명시 허용 시 FetchContent 경로. `tools/build.sh`, Windows portable bootstrap PS1 및 CMD.
8. 테스트: ZIP 작성기 왕복 신규 추가, NUL 파일명 신규 회귀 테스트.

## 실측 및 테스트 구분

| 검증 항목 | 결과 | 근거/한계 |
|---|---|---|
| Linux portable CMake Release build | 통과 | 런타임 코드 일부는 `#ifdef _WIN32`으로 빌드되지 않음 |
| Linux ctest 5묶음 | 5/5 통과 | core, PathGuard, ZIP, ZIP mutation 25,000, writer |
| Linux ASan/UBSan ctest 5묶음 | 5/5 통과 | 메모리·UB 검사 적용 |
| Windows v0.6 컴파일/링크 | **미실행** | 이 환경에 Zig/MinGW/MSVC가 없고 다운로드 불가 |
| Windows ZIP 기능/파일 생성 | **미실행** | 실제 Windows 검증 필요 |
| Windows 자동 튜닝 | **미실행** | 사용자의 Ryzen 3 4100에서 측정 전 |
| 경쟁 제품 비교 | 미실행 | 사용자 결정으로 보류 |
| 7z/RAR | 미구현 | v0.7 이후 |

## 알려진 제약 / 향후 수정 우선순위

- 중요: Windows 부분은 크로스 컴파일 단계도 통과하지 못했으므로 `hx.exe` 실행 파일을 첨부할 수 없다. Windows 부트스트랩 스크립트 역시 해당 OS에서 미검증.
- AutoTune에서는 아카이브 파싱을 측정에서 제외한다. 따라서 *전체 사용자 체감시간*이 아닌 `decode+write+close` 부분을 최적화한다.
- 3% 이내 단순 선택은 자동화했지만 paired bootstrap은 아직 연결되지 않았다. 특히 quick의 2회 표본은 불확실하다.
- `profile.json` v2 통합 대신 독립 `zip-profile.json`을 사용하며 하드웨어 fingerprint 자동 무효화가 없다. 하드웨어/OS 변경시 튜닝을 다시 수행해야 한다.
- 별도 `zip-tune-*` 폴더 안의 합성 테스트 잔여물이 생길 경우 무작정 삭제하지 말고 `summary.md`와 파일목록을 먼저 확인한다.
- 중간 실패시 새 출력 루트가 남는다. 기존 데이터 덮어쓰기는 없으며 자동 롤백은 아직 없다.
- `Win32Sink`의 루트 하위 junction 교체를 완전히 원천 차단하는 핸들 기준 상대 경로 오픈은 미구현. 동시 공격자 환경을 신뢰하지 말 것.
- `hx`는 ZIP Store/Deflate만 지원한다. 암호화/Deflate64 등은 명시적 실패, 7z.dll 폴백은 미구현.
- 의존성 공급망 관리를 강화하기 위해 libdeflate/zlib 소스 다운로드에 대한 별도 검증 해시가 필요하며, 부트스트랩 다운로드를 검증/보강할 것.

## 사용자의 최소 행동

1. 패키지 압축 해제 후, `RUN_ZIP_AUTOTUNE.cmd` 실행(네트워크 필요, portable toolchain 다운로드 포함).
2. 빌드 및 Windows 자체 검사 성공을 확인하고 ZIP 튜닝 수행.
3. 문제가 생기면 PowerShell 오류 로그와 출력 폴더명만 전달. 개인 데이터 내용은 불필요.

자원: Ryzen 3 4100 4C/8T, DDR4-3200 2×8GB, ADATA LEGEND 700 C: NTFS, 여유 약 44.9GB, Defender ON, Ryzen High Performance. 이 수치는 2026-10-24 사용자가 제공한 환경 수집 시점 기준.
