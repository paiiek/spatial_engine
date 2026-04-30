이머시브오디오 렌더링 엔진 제작

<필수 구현 내용>
1.	공연/전시에서의 활용을 목적
2.	오브젝트베이스오디오 + WFS, VBAP, DBAP 등 렌더링 알고리즘 기반
3.	3D 리버브 또는 IR 리버브 지원
4.	커스텀 스피커 레이아웃 (데카르트 좌표 기반)
5.	Linux 또는 windows 기반으로 하여 CPU 파워를 통한 렌더링
- 산업에서는 Linux를 선호
6.	프로세싱 레이턴시 : 프로오디오 산업 내의 엔진의 경우 5ms 내외 구성
7.	3U 이내의 랙마운트 하드웨어 서버 구성 예정
8.	파라미터
- Position -> Azimuth, Elevation, Distance (or X, Y, Z)
- Object Processing -> EQ(4Band 내외), Delay
- 거리 종속 변수
  > 원점과의 거리에 따른 음압 변화
  > 원점과의 거리에 따른 전달시간 변화
  > 원점과의 거리에 따른 주파수 변화(멀어질수록 고음역대 감소)
  > 원점과의 거리에 따른 잔향 임계값 변화(멀어질수록 잔향의 양 증가)
9.	오브젝트 별 렌더링 알고리즘 별도 구현
10.	편의기능
- 위 파라미터를 제어할 수 있는 GUI (WebGuI 또는 컨트롤 소프트웨어 제작)
- OSC를 통한 외부제어
- DAW에서 시스템을 제어할 수 있는 VST3 플러그인
(제어의 경우 오직 컨트롤 제어만 이루어짐)
- 현재 믹싱에 대한 바이노럴 모니터링
- 스피커 출력 점검을 위한 노이즈 제네레이터 (스피커 출력으로 바로 라우팅)
- 오디오 매트릭스 
  > 입력 채널 몇번을, 몇번 오브젝트에 할당할 것인지
  > 렌더링된 출력채널 몇번을, 물리적인(예를들어 단테 네트워크오디오) 출력 몇번에 할당할 것인지
11.	기본적인 오디오 입출력의 경우 ‘Dante Network Audio’를 지원하는 PCIe Card 사용할 예정
- Linux의 경우 Digigram ALP-Dante 사용 예정
(https://www.getdante.com/product/alp-dante/)
- Win의 경우 Rednet PCIeNX Dante 사용 예정
(https://focusrite.com/products/rednet-pcienx)

