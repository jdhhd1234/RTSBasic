# MinRTS MVP 개발 계약서 (백엔드 ↔ 프론트엔드)

이 문서는 병렬로 작업하는 에이전트들의 **단일 기준**이다. 여기 적힌 API·메시지 형식·좌표계를 임의로 바꾸지 말 것.
바꿔야 하면 작업을 멈추고 조율자(메인 에이전트)에게 보고한다.

참고 문서: `Planner/main_plan_spec.md`(기획), `Planner/ai_ask.md`(확정 답변), `server/game/data.py`(수치 데이터, 이미 작성됨).

## 0. 파일 소유권

| 영역 | 담당 | 경로 |
|---|---|---|
| 백엔드 | backend 에이전트 | `server/**`, `requirements.txt`, `run.bat` |
| 프론트엔드 | frontend 에이전트 | `web/**` |
| 계약/기획 | 조율자 | `Planner/**` |

상대 영역 파일은 수정하지 않는다. `server/game/data.py`는 백엔드가 수정 가능하지만 이 계약에 영향 주는 키 이름은 유지.

## 1. 실행 환경

- Python 3.12, 가상환경 `.venv` (이미 생성, fastapi·uvicorn[standard] 설치됨). 실행: `.venv/Scripts/python -m uvicorn server.main:app --port 8000 --reload`
- FastAPI가 `web/` 폴더를 `/` 경로에 정적 서빙한다 (`index.html` 기본).
- DB: SQLite `data/minrts.db` (stdlib `sqlite3`). 추가 패키지 설치 금지(필요하면 보고).
- 프론트 라이브러리(CDN, 버전 고정):
  - PixiJS: `https://cdn.jsdelivr.net/npm/pixi.js@7.4.2/dist/pixi.min.js` (전역 `PIXI`)
  - milsymbol: `https://cdn.jsdelivr.net/npm/milsymbol@2.2.0/dist/milsymbol.js` (전역 `ms`)
- 빌드 도구 없음. 프론트는 순수 ES 모듈(`<script type="module">`).
- PC 전용, 한국어 UI, 분위기: 실제 작전지도처럼 단순·군사적.

## 2. 좌표계·헥스 (양쪽 모두 이 식을 그대로 구현)

- 월드 단위 1 = 1 km. 맵 1200 × 1200 km.
- 헥스: **pointy-top**, 중심 간 가로 거리 20 km → 반지름 `R = 20/√3 ≈ 11.547`, 행 간격 `1.5R ≈ 17.32`.
- 크기: `COLS = 60`, `ROWS = 69`. 인덱스 `i = row * COLS + col`.
- 오프셋 방식 **odd-r**(홀수 행이 오른쪽으로 반 칸).
- 헥스 중심: `x = 20 * (col + 0.5 * (row & 1)) + 10`, `y = R + row * 1.5R`.
- axial 변환: `q = col - (row - (row & 1)) / 2`, `r = row`; 역변환 `col = q + (r - (r & 1)) / 2`.
- 픽셀→헥스: `px = x - 10`, `py = y - R` 로 원점 보정 후
  `qf = (√3/3 * px - py/3) / R`, `rf = (2/3 * py) / R` → cube 반올림 → offset 변환.
- 꼭짓점 i(0..5): 각도 `60*i - 30`도 (화면 y축 아래 방향). 
- 이웃 방향 k(0..5) axial: `[(+1,0),(0,+1),(-1,+1),(-1,0),(0,-1),(+1,-1)]` — 방향 k와 공유하는 변은 꼭짓점 k, k+1.
- 위경도→km (서버 맵 생성용): `x = lon * 72.5`, `y = (54.5 - lat) * 111`. 맵은 경도 0°~16.5°, 위도 54.5°~43.7° (중서부 유럽: 프랑스 동부·베네룩스·독일·체코·알프스).

## 3. REST API (JSON, 인증은 `Authorization: Bearer <token>`)

| 메서드 | 경로 | 요청 | 응답 |
|---|---|---|---|
| POST | `/api/register` | `{username, password}` | `{token, user}` |
| POST | `/api/login` | `{username, password}` | `{token, user}` |
| GET | `/api/me` | – | `{user}` |
| GET | `/api/data` | – (인증 불필요) | `{battalions, effects, terrain, weather, stages, difficulty, rules}` |
| PUT | `/api/templates` | `{templates: [{name, battalions:[id,...]}]}` (최대 6) | `{user}` |
| POST | `/api/gacha/pull` | – | `{result, user}` |

- `user` 객체: `{username, points, max_stage, unlocks: {battalion_id: level}, templates: [{name, battalions}]}`
  - `unlocks`에 있는 키 = 해금된 대대. 레벨 1부터. 신규 계정: 기본 대대 6종 레벨 1, points 300, 기본 템플릿 3개(`DEFAULT_TEMPLATES`), max_stage 1.
- `battalions`: `data.BATTALIONS` 그대로 + 각 항목에 `id` 포함한 **배열**. `effects` = `EFFECT_DESC`.
- `rules`: `{max_battalions:4, max_duplicate:2, max_templates:6, gacha_cost:100, gacha_unlock_chance:0.4, gacha_rarity_weight:{...}, max_level:10, level_bonus:0.05, match_seconds:1200}`
- `stages`: `{1:{name,...}, ...}`, `difficulty`: `{easy:{name:"하"}, normal:{name:"중"}, hard:{name:"상"}}`
- 가챠 `result`: `{kind: "unlock"|"upgrade", battalion: id, level}`. 포인트 부족 시 400 `{detail: "..."}`.
- 오류: HTTP 4xx + `{detail: "한국어 메시지"}`.
- 템플릿 검증: `data.validate_template` (1~4개, 같은 대대 최대 2개, 해금된 것만).

## 4. WebSocket `/ws/game?token=<token>`

모든 메시지 JSON, 필드 `t`가 타입. 연결 1개 = 게임 1판. 서버가 시뮬레이션 권한을 가진다(10 Hz 계산, 4 Hz 전송).

### 클라이언트 → 서버
```
{"t":"start", "stage":1, "difficulty":"easy"|"normal"|"hard"}   // 반드시 첫 메시지. max_stage 초과 스테이지 거부
{"t":"move", "ids":[3,5], "col":10, "row":20}                     // 목표 헥스로 이동(A* 경로). 후퇴 상태 해제
{"t":"retreat", "ids":[3]}                                         // 가장 가까운 아군 기지로 후퇴
{"t":"build", "template":0}                                        // 계정 템플릿 인덱스로 사단 생산 대기열 추가
{"t":"surrender"}                                                  // 항복(패배 처리)
```

### 서버 → 클라이언트
```
{"t":"init",
 "you":1,                                   // 플레이어 진영 (항상 1=청군, AI=2)
 "cols":60, "rows":69,
 "terrain":[...],                           // 길이 COLS*ROWS, 지형 id(data.TERRAIN의 id)
 "owner":[...],                             // 길이 COLS*ROWS, 0=없음(바다) 1=청 2=적
 "bases":[{"id":0,"side":1,"kind":"hq","name":"파리","x":..,"y":..,"col":..,"row":..,"hp":600,"mhp":600}],
 "cities":[{"name":"파리","col":..,"row":..}],
 "templates":[{"name":..,"battalions":[..],"cost":..}],   // 플레이어 템플릿 + 계산된 비용
 "weather":[...16개 날씨 id...], "weather_cells":4,
 "stage":1, "difficulty":"easy", "match_seconds":1200}

{"t":"state",
 "time": 12.3,                               // 경과 초
 "res": 312.5, "income": 6.2,                // 플레이어 자원·초당 수입
 "build_speed": 1.0,                         // 기지 함락 패널티 반영 생산 속도 배율
 "queue":[{"template":0,"name":"보병사단","progress":0.4}],  // progress 0..1, 첫 항목만 진행
 "units":[{"id":3,"side":1,"name":"제1보병사단","x":..,"y":..,
           "hp":320,"mhp":400,
           "st":"idle"|"move"|"engaged"|"retreat",
           "tgt":[x,y] | null,               // 공격 대상 위치(교전선 그리기용)
           "dest":[x,y] | null,              // 이동 목적지 (자기 진영 유닛만, 적은 null)
           "b":["inf","inf","arty","eng"],   // 대대 구성
           "fn":"UCI---",                    // 대표 대대 milsymbol 기능 코드
           "supply":0.9}],                   // 보급 배율
 "bases":[{"id":0,"side":1,"hp":580,"mhp":600}],   // 변화 반영 (side 변경 = 함락)
 "own":[[idx,owner],...],                    // 이전 전송 이후 바뀐 헥스만
 "weather":[...16개...] | null,              // 바뀌었을 때만
 "terr":[blue_hex_count, red_hex_count]}

{"t":"event", "msg":"리옹 군단작전기지가 함락되었습니다", "kind":"info"|"good"|"bad"}
{"t":"end", "winner":1|2, "reason":"hq"|"time"|"surrender", "points_earned":250, "user":{...}}
{"t":"error", "msg":"..."}
```

## 5. 게임 규칙 요약 (서버 구현 기준, 수치는 data.py)

- 실시간, 제한 20분. 승리: 상대 본부 함락 또는 시간 종료 시 영토(헥스 수) 많은 쪽.
- 진영별 기지: 본부1·군단2·사단4, 위치 고정(`data.BASES`). 초기 영토 = 가장 가까운 기지 소유자(보로노이).
- 기지: 체력·방어·공격 있음. 적 사단이 20km 안이면 교전, 체력 0 → 공격측 소유로 넘어감(체력 30%로 재설정). 탈환 가능. 적 없으면 체력 1/초 회복.
- 생산: 자원으로 템플릿 사단 생산, 기본 15초 ÷ 생산속도. 생산속도 = 잃은 군단기지마다 ×0.5, 잃은 사단기지마다 ×0.85. 완성 시 본부(본부 없으면 아무 소유 기지)에 출현.
- 수입/초 = 1 + 영토헥스×0.0015 + 보유기지 income 합. × (AI는 stage.ai_income × difficulty.income).
- 사단 능력치: 공격·방어·체력 = 대대 합(레벨 보너스 적용), 이동속도 = 대대 최저값, 사거리 = 대대 최대값.
- 이동: A* 헥스 경로, 바다 불가. 속도 × 지형 move × 날씨 move (공병: 지형 패널티 절반, 산악: 산·고원 패널티 없음, 화생방: 날씨 패널티 절반).
- 교전: 적이 20km 안 → 정지·교전(engaged). 사거리 > 20km 대대는 사거리 내 적을 원거리 사격(이동 중에도). 여러 사단이 한 대상 집중 가능.
  - 피해/초 = Σ(대대 공격 × 상성배율) × (hp/mhp) × 지형(공격측 기갑 패널티) × 날씨 atk × 보급 × 효과 × 랜덤(1±날씨 var) × K × 50/(50+방어효과)
  - 방어효과 = 평균방어 × 방어측 지형 dfn.
- 후퇴: hp < 30%(헌병 20%) 자동 후퇴, 또는 플레이어 명령. 후퇴 중엔 반격 안 함.
- 영토: 사단이 있는 헥스와(적 사단 30km 내가 아니면) 인접 6헥스를 점령. 기지 함락 시 반경 2헥스 점령.
- 보급: 가장 가까운 아군 기지 거리 ≤250km 100%, 650km에서 50%(선형), 수송대대는 거리 ÷1.5.
- 날씨: 4×4 지역, 지역마다 60~120초마다 랜덤 변경(맑음 비중 높게).
- 가챠 재화: 적 군단기지 함락 +50, 본부 +200 — 함락 즉시 계정에 저장(같은 기지는 한 판에 한 번). 스테이지 승리 시 max_stage = stage+1 (최대 3).
- AI: 난이도별 think 주기마다 (1) 자원 있으면 stage.ai_templates 중 하나 생산 (2) 위협받는 기지(적 100km 내) 방어 (3) 나머지는 group 수만큼 묶어 가장 가까운 플레이어 기지/전선 공격. 체력 낮으면 후퇴.

## 6. 화면 (프론트엔드)

1. **로그인/회원가입**
2. **로비**: 포인트, 스테이지 선택(max_stage까지), 난이도(상/중/하), 출격 버튼 / **편제 편집기**(템플릿 최대 6개, 대대 1~4, 중복 2, 해금된 것만, 사단 능력치 미리보기) / **가챠**(확률 표시, 결과 연출) / **대대 도감**(20종, 잠김/레벨/능력치/효과)
3. **게임**: PixiJS 전체화면 맵 + HUD
   - 지형(작전지도 톤의 차분한 색), 영토(진영색 반투명) + 전선(진영 경계 굵은 선), 도시 이름, 날씨 지역 오버레이(아이콘/패턴)
   - 기지·사단: milsymbol 심볼 (청군 affiliation F, 적군 H). 사단 SIDC 예: `SFGPUCI----I---`(Friend, 기능코드, 제대 I=사단). 본부 `S?GPU-----AK---`, 군단기지 `S?GPU-----AJ---`, 사단기지 `S?GPU-----AI---` (? = F/H). 체력바 표시. 확대해도 심볼은 화면상 일정 크기.
   - 교전선(유닛→tgt), 내 사단 이동경로(dest)
   - 조작: 좌클릭 선택, Shift+클릭 추가, 좌드래그 박스선택, 우클릭 이동명령, 우드래그 팬, 휠 줌, WASD/방향키 팬, R 후퇴, 숫자키 1~6 생산
   - HUD: 남은 시간, 자원/수입, 생산속도, 영토 비율 바, 생산 패널(템플릿+대기열), 선택 사단 정보(대대 구성·체력·보급), 이벤트 로그, 항복 버튼
   - 종료 오버레이: 승패, 사유, 획득 포인트 → 로비로

## 7. 장비·구성품 도감 (2단계 추가)

근거: `Planner/MinRTS 장비·자원·능력치 설계서.md` (이하 설계서). **도감 전용 데이터**다.
- 장비 70종(`EQ-*`)·구성품 110종(`PART-*`)은 공식 대대 20종을 설명하는 자료다. 편제·생산·가챠·전투 로직은 바뀌지 않는다.
- 소재 태그 6종(금속·경량합금·복합재·기계부품·전자부품·광학부품)은 **분류 태그**일 뿐, 자원 필드·재고·HUD 표시를 만들지 않는다.
- 템플릿에는 여전히 대대 ID만 들어간다. 장비는 편제에서 선택할 수 없다.
- (향후) 유저 커스텀 장비·부품 개조는 `Planner/problem.md` 후속 문서가 오면 별도 계약으로 다룬다.

### 데이터 생성
- `server/tools/build_codex.py`가 설계서 5장·6장을 파싱해 `server/game/codex.json`을 만든다(생성 파일도 저장소에 둠). 손으로 옮겨 적지 않는다.
- 빌드 시 검증: 장비 70·구성품 110, ID 중복 없음, 연결 대대 ID가 `data.BATTALIONS`에 존재, 카드의 Lv.1 참조 능력치·생산비가 `data.py`와 일치. 불일치는 경고 목록으로 출력(빌드 실패 아님).

### REST
| 메서드 | 경로 | 응답 |
|---|---|---|
| GET | `/api/codex` | `{categories, tags, equipment, parts}` (인증 불필요) |

- `categories`: `[{id:"T", name:"전차와 전차 기반 차량"}, {id:"A",name:"장갑차"}, {id:"H",name:"헬기"}, {id:"I",name:"보병장비"}, {id:"E",name:"전자전장비"}, {id:"S",name:"공용 장비"}]` (S는 구성품만)
- `tags`: `["금속","경량합금","복합재","기계부품","전자부품","광학부품"]`
- `equipment[]`: `{id:"EQ-T01", category:"T", name, role, battalion:"tank", parts:["복합장갑", ...], axis:"방어력", scope}`
  - 능력치·비용은 **포함하지 않는다**. 프론트가 `battalion`으로 `/api/data`의 대대 Lv.1 값을 참조해 표시(설계서 1.3: 카드는 연결 대대 값을 재인용).
- `parts[]`: `{id:"PART-T01", category:"T", name, tag:"복합재", field:"방어력", desc}`

### 화면 (대대 도감 확장)
- 도감 탭 3개: **대대 20** / **장비 70** / **구성품 110**.
- 장비 탭: 분류 필터(T/A/H/I/E), 연결 대대 필터, 검색. 카드에 역할·연결 대대(잠김 여부 표시)·주요 구성품·비교 축·적용 범위·연결 대대 Lv.1 능력치(계정 레벨 아님)·생산비.
- 구성품 탭: 분류·소재 태그 필터, 검색. 표 형태(ID·이름·태그·비교 필드·설명).
- 대대 상세에 "대표 장비" 목록(해당 대대에 연결된 장비 카드)을 표시하고 클릭 시 장비 탭으로 이동.
- 편제 편집기에서 장비는 선택 불가. 대대 선택 항목 옆에 대표 장비 이름 1개를 보조 표기만 할 수 있다.
