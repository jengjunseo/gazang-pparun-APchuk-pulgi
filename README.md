# HYPER EXTRACT v0.7.0

Windows x64용 네이티브 압축 해제 프로그램입니다. ZIP · 7z · RAR4/RAR5를 지원합니다.

**시작:** [전체 ZIP 다운로드](https://github.com/jengjunseo/gazang-pparun-APchuk-pulgi/archive/refs/heads/main.zip) → 쓰기 가능한 로컬 폴더에 전체 압축 해제 → **RUN_HYPER_EXTRACT.cmd** 실행. 실행 파일 옆의 7z.dll도 필요합니다. 설치·관리자 권한은 필요하지 않습니다.

작은 ZIP 항목은 libdeflate와 병렬 작업자로, 큰 항목은 저메모리 스트리밍으로 풉니다. 검증 후 새 출력 폴더로 완료하며 기존 폴더를 덮어쓰지 않습니다. 빠른 튜닝 버튼으로 이 PC에 맞는 설정을 측정할 수 있습니다.

- 실제 Windows CTest 7개 묶음, CLI 68개 사례, 실패·프로파일 15개 사례, GUI 10개 검사 통과.
- 10,104개 파일·287MB 합성 ZIP 3회 비교 중앙값: HX 17.72초 / 7-Zip 26.05 23.26초. 이 조건에서 완료 시간이 23.8% 짧았습니다. 초기 HX 실행 57.32초와 큰 편차도 [원시 결과와 보고서](docs/BENCHMARK_REPORT.md)에 공개합니다.
- 암호화·분할 볼륨·링크·네트워크 출력은 미지원이며 명확히 실패합니다. 실행 파일은 서명되지 않았고 추가 PC 검증이 남아 있습니다.

[한국어 사용 안내](README_KO.md) · [검증 범위와 한계](docs/VALIDATION_MATRIX.md) · [보안 설계](docs/SECURITY_REVIEW.md) · [라이선스와 소스](LICENSES.md)

소스 재빌드: **BUILD_WINDOWS_ONE_CLICK.cmd**. 공식 포터블 도구의 버전·SHA256이 고정되어 있습니다. Git 체크아웃의 third_party/vendor-sources.zip은 CMake가 해시 확인 후 자동으로 복원합니다. 배포 체크섬은 다운로드 직후의 파일 기준이며, 빌드·튜닝 후 변경되는 로그·프로파일에는 다시 적용하지 않습니다.
