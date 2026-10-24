# HYPER EXTRACT v0.7.0 — 사용 안내

Windows 11 x64용 포터블 압축 해제 앱입니다. 설치나 관리자 권한이 필요하지 않습니다.

1. 배포 ZIP 전체를 쓰기 가능한 일반 로컬 폴더에 풉니다.
2. **RUN_HYPER_EXTRACT.cmd**를 한 번 실행합니다.
3. 압축 파일을 선택하거나 창에 끌어놓고, **새 출력 폴더**를 지정한 뒤 **압축 풀기**를 누릅니다.

ZIP Store/Deflate·ZIP64, 7z LZMA/LZMA2, RAR4/RAR5(해제 전용)를 지원합니다. CRC/디코더 오류와 실제 해제 크기를 확인하고 모든 파일이 닫힌 뒤 완료합니다. 기존 출력 폴더는 덮어쓰지 않습니다. 실패·취소하면 최종 출력 폴더를 만들지 않고 자체 임시 출력을 정리합니다. 충돌/Windows 비호환 이름은 결정적으로 바꾸고 결과에 알립니다. 자세한 이름은 `list`로 확인할 수 있습니다.

**미지원:** 암호화, 분할 볼륨, 링크/특수 파일, ZIP Deflate64 등 Store/Deflate 이외 방법. 이런 항목은 일부만 누락하는 대신 전체 작업을 실패시킵니다. 출력 경로에 정션/심볼릭 링크가 있으면 거부합니다. 일반 로컬 폴더를 사용하세요. 네트워크/UNC 출력은 지원하지 않습니다.

**빠른 튜닝**은 합성 ZIP으로 작업자 수와 파일 닫기 스레드를 실험합니다. 개인 파일을 읽거나 업로드하지 않습니다. 이 PC에서 측정한 프로파일을 함께 제공하며 CPU·RAM·OS·볼륨·확인 가능한 Defender 상태·프로그램 버전 지문이 맞고 작은 파일 ZIP일 때만 적용합니다. 7z/RAR은 검증된 공식 디코더 기본 전략을 사용합니다. 튜닝은 선택 기능이며 기본 추출에 필수는 아닙니다.

```powershell
.\release\hx.exe x "C:\archives\sample.zip" -o "C:\output\new-folder"
.\release\hx.exe list "C:\archives\sample.7z"
.\release\hx.exe test "C:\archives\sample.rar"
.\release\hx.exe selftest
.\release\hx.exe tune --quick
.\release\hx.exe tune --full
```

GUI의 취소 또는 CLI의 Ctrl+C로 중단합니다. `--keep-partial`은 실패 출력을 조사할 때만 쓰는 CLI 옵션입니다. 강제 프로세스 종료/전원 장애 뒤 남는 `.hx-<임의 ID>` 폴더를 다음 실행이 자동 삭제하지 않습니다. 완료 폴더로 이름을 바꾼 뒤 빈 임시 폴더 정리만 실패하는 경우에는 검증된 완료 폴더가 남고 I/O 오류를 보고할 수 있습니다. 원본 아카이브는 읽기 전용으로 열며 변경하지 않습니다. 영속 저장을 강제하는 durable 모드는 제공하지 않습니다.

종료 코드: 0 성공, 2 인수, 3 미지원, 4 손상/검증, 5 I/O, 6 취소, 7 경로/한도, 8 내부/메모리 오류. 상세한 시험 범위와 남은 검증은 `docs/VALIDATION_MATRIX.md`, 성능은 `docs/BENCHMARK_REPORT.md`에 있습니다. 실행 파일은 코드 서명되지 않았으며 보안 설정 해제를 요구하지 않습니다.

재빌드: `BUILD_WINDOWS_ONE_CLICK.cmd`. 첫 빌드만 공식 포터블 도구 다운로드가 필요합니다. 라이브러리 소스는 포함되어 있으며 도구 캐시가 있으면 오프라인 재빌드할 수 있습니다. 빌드 로그는 `logs/`, 포터블 앱의 튜닝/검사 자료는 `data/`에 저장합니다.
