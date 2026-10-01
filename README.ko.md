# WebPositive HTML5 비디오, GMA500 하드웨어 디코딩

English: [`README.md`](README.md).

32비트 Haiku(x86_gcc2 hybrid)의 WebPositive에서 YouTube를 포함한 HTML5 비디오가
재생되게 하고, Sony VAIO P의 Intel GMA500(Poulsbo) 비디오 디코더로 H.264를
하드웨어 디코딩합니다.

- **HaikuWebKit 1.9.19 수정** (`webkit/`): HaikuWebKit이 자기 미디어 엔진을
  등록하지 않아 `<video>`/`<audio>`가 전혀 재생되지 않았고, YouTube는 "이
  브라우저에서 재생할 수 없음"을 표시했습니다. 플레이어 시계가 MP4에서 실제의
  1/20 속도로 가는 디코더 타임스탬프를 믿던 문제도 고쳤습니다.
- **Media Kit 디코더 애드온 `msvdx_h264`** (`media-msvdx/`): GMA500에서 H.264를
  디코딩하고, 처리할 수 없는 스트림이나 다른 기기에서는 ffmpeg 플러그인으로
  넘깁니다.

VAIO P(Atom Z520)에서 YouTube 360p 스트림의 프레임당 CPU가 ffmpeg 17.1 ms에서
5.8 ms로 줄었습니다. MSE가 없어서 YouTube는 WebPositive에 360p progressive MP4를
보내므로, 해상도는 360p입니다.

## pkgman으로 설치

```sh
pkgman add-repo https://pkgman.rainygirl.com/x86_gcc2-webpositive
pkgman refresh
pkgman install webpositive_hwvideo
```

설치 후 WebPositive를 다시 시작하세요. `webpositive_hwvideo`가 다음을 함께
설치합니다.

| 패키지 | 내용 |
|---|---|
| `haikuwebkit_x86` 1.9.19-6 | 미디어 엔진/시계 수정(및 haiku-rwebpositive-arm64의 누수 수정)이 들어간 HaikuWebKit, 공식 1.9.19를 대체 |
| `msvdx_media_x86` | 디코더 애드온 |
| `msvdx_firmware` | GMA500 비디오 디코더용 Intel 마이크로코드 |

되돌리기: `pkgman uninstall webpositive_hwvideo msvdx_media_x86` 후, 이
저장소를 제거하고(`pkgman drop-repo`) `pkgman install haikuwebkit_x86`으로 공식
WebKit을 다시 설치합니다.

WebPositive 환경변수 `MSVDX_MEDIA=0`은 소프트웨어 디코딩을 강제하고,
`MSVDX_MEDIA_DEBUG=1`은 디코더 동작을 stderr에 기록합니다.

## 요구 사항

- HaikuWebKit 1.9.19를 쓰는 Haiku R1 beta6 계열 x86_gcc2 hybrid.
- 하드웨어 디코딩: Intel GMA500 / SCH US15W. Sony VAIO P(VGN-P70H)에서
  확인했습니다. 다른 기기에서는 같은 비디오가 소프트웨어로 재생됩니다.

## 빌드

- WebKit: `webkit/build-haikuwebkit-x86.sh` (Linux 크로스 빌드).
  [haiku-rwebpositive-arm64](https://github.com/rainygirl/haiku-rwebpositive-arm64)의
  x86 빌드에 이 저장소의 패치를 얹어 실행합니다. `RWP`에 그 체크아웃 경로를
  지정하세요.
- 애드온: Haiku 기기에서 `cd media-msvdx && make` (`haiku_x86_devel`,
  `ffmpeg6_x86_devel` 필요).

개발 노트, 원인 분석, 측정값은 [`AGENTS.md`](AGENTS.md)에 있습니다.

## 라이선스

새 코드는 MIT. WebKit 패치, Chromium/psb_video 소스, Intel 펌웨어는 각자의
라이선스를 따릅니다. [`LICENSE`](LICENSE) 참고.
