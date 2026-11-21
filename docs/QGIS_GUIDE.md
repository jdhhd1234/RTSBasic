# RTSBasic 지도 제작에 QGIS 사용하기

QGIS에서 지도와 초기 배치를 편집하고, MinRTS에서 그 지도를 불러와 한 판을 진행한다. 원본 레이어는 QGIS 프로젝트와 GeoPackage에 보관하고, 게임에서 읽을 때마다 같은 절차로 GeoJSON을 내보낸다. 캠페인과 샌드박스의 무작위 지도에는 절차 생성이 계속 사용된다.

| 데이터 | QGIS에서 지정 | MinRTS에서 처리 |
| --- | --- | --- |
| 지형 | 지형 구역과 고도 | 헥스별 이동·방어 보정 |
| 도로 | 선형 경로와 등급 | 헥스별 이동 속도와 경로 탐색 비용 |
| 강 | 하천의 위치와 형태 | 강 헥스의 도하·이동 제한 |
| 도시 | 위치, 이름, 등급, 인구 | 소유 진영의 인력 수입과 점령 |
| 광산 | 위치, 자원 종류, 매장량 | 공장 연결, 채굴 속도, 고갈 |
| 유닛 | 초기 위치, 진영, 편제 이름 | 편제 구성, 명령, 전투력 |
| 전선 | 초기 영토 경계 | 교전 중 영토와 전선 변화 |

기지의 진영·종류·목표 헥스는 시나리오 Lua의 `bases`에서 지정한다. 도시 점에는 아래 표의 도시 속성을 넣는다.

## QGIS 프로젝트와 좌표계

- 새로 설치한다면 QGIS 장기 지원판(LTR)을 권장한다. 아래 메뉴 이름은 QGIS 3.44 매뉴얼을 기준으로 적었다. [QGIS 공식 다운로드](https://qgis.org/download/)
- 처음 제작할 때는 `EPSG:4326`으로 내보내는 것이 간단하다. GeoJSON 좌표 순서는 `[경도, 위도]`다. 게임은 지형 범위의 중간 위도를 기준으로 경도 길이를 보정하고 북쪽을 화면 위로 둔다.
- 지역 좌표를 미터 단위로 유지하려면 작업 지역에 맞는 투영 CRS를 선택하고 **모든 레이어를 같은 CRS로** 변환한다. 불러오기는 GeoJSON의 `crs` 선언 대신 지형 좌표 범위로 좌표계를 판별한다. 지형 좌표가 모두 경도 ±180°, 위도 ±90° 범위에 있으면 경위도로 해석하므로 그런 값 범위의 로컬 미터 좌표는 `EPSG:4326`으로 변환해 내보낸다. [QGIS 벡터 레이어 병합 문서](https://docs.qgis.org/3.44/en/docs/user_manual/processing_algs/qgis/vectorgeneral.html)

## 레이어와 필드

각 피처의 속성 테이블에 텍스트 필드 `layer`를 만들고 아래 값을 넣는다. 식별자는 표의 소문자 철자를 사용한다. 선택 필드의 빈 값은 `NULL`로 내보내면 기본값이 적용된다.

| `layer` 값 | 도형 | 역할 |
| --- | --- | --- |
| `terrain` | Polygon / MultiPolygon | 지형 구역과 지도 전체 범위 |
| `road` | LineString / MultiLineString | 도로 경로 |
| `river` | LineString / MultiLineString | 강 경로와 강 헥스 |
| `city` | Point | 도시 위치 |
| `mine` | Point | 광산 위치 |
| `unit` | Point | 초기 사단 위치 |
| `territory` | Polygon / MultiPolygon | 초기 소유 구역 |

| 레이어 | 필드명 | 형식 | 값 | 필수 여부 |
| --- | --- | --- | --- | --- |
| 공통 | `layer` | 텍스트 | 위 레이어 식별자 | 필수. `terrain` 속성을 쓰는 기존 피처는 생략 가능 |
| `terrain` | `terrain` | 텍스트 | `sea`, `plain`, `forest`, `mountain`, `river`, `city`, `swamp`, `desert`, `plateau` | 필수 |
| `terrain` | `elevation` | 정수 또는 실수 | 해발 고도(m), 기본은 0m | 선택 |
| `road` | `tier` | 텍스트 | `highway`, `major`, `minor`(기본) | 선택 |
| `river` | `layer` 외 추가 필드 | — | — | — |
| `city` | `name` | 텍스트 | 표시 이름 | 필수 |
| `city` | `tier` | 텍스트 | `metro`, `city`, `town`(기본) | 선택 |
| `city` | `population` | 정수 | 0 이상의 인구(명) | 필수 |
| `mine` | `resource` | 텍스트 | `oil`, `iron`, `nickel`, `quartz`, `rare_earth`, `manganese` | 필수 |
| `mine` | `reserve` | 정수 또는 실수 | 매장량(게임 자원 단위). 생략하거나 0 이하이면 무한 | 선택 |
| `unit` | `faction` | 텍스트 또는 정수 | `blue`, `red`, `0`, `1` | 필수 |
| `unit` | `template` | 텍스트 | 해당 진영의 편제 이름과 정확히 일치하는 값 | 필수 |
| `territory` | `owner` | 텍스트 또는 정수 | `blue`, `red`, `0`, `1` | 필수 |

지형은 피처 순서대로 칠한다. 전체 영역을 덮는 바탕 지형을 앞에 두고 세부 구역을 뒤에 둔다. 지형의 면 내부에 헥스 중심이 있으면 해당 지형이 적용되며 구멍은 제외된다. `elevation`을 넣은 구역만 고도를 갱신한다. 바탕이 없는 헥스는 최상위 `minrts_default_terrain` 값이나 기본값 `plain`을 사용한다. 이전 방식의 지형 선·점과 멀티 도형도 읽을 수 있다. `city`, `mine`, `unit`의 MultiPoint는 각 점에 같은 속성을 적용한다.

명시적인 강과 도시는 지형 위에 덧칠한다. 도시 인력 수입은 점이 있는 헥스의 소유 진영에 들어간다. 양수 매장량을 가진 광산은 같은 진영의 가동 중인 공장과 연결되었을 때만 매장량이 줄어든다. 해당 진영에 유닛 점이 하나라도 있으면 그 진영의 기본 배치 대신 유닛 점을 사용한다. 초기 영토를 지정하면 면 외부는 중립이며, 면이 겹치면 뒤 피처가 우선한다. 기지 헥스는 기지의 진영을 따르고 통행 불가 헥스는 중립으로 남는다.

## 하나의 GeoJSON으로 내보내기

1. QGIS 속성 테이블의 **Field Calculator**에서 길이 16 이상의 `layer` 텍스트 필드를 만들고 표현식에 `'terrain'`, `'road'`처럼 레이어 식별자를 넣는다. 저장할 필드로 만들고 편집 내용을 저장한다. [QGIS 필드 계산기 문서](https://docs.qgis.org/3.44/en/docs/user_manual/working_with_vector/field_calculator.html)
2. **Processing → Toolbox → Merge vector layers**는 같은 도형 유형끼리 합친다. 필요하면 면끼리, 선끼리, 점끼리 각각 병합하고 **Destination CRS**를 공통 값으로 지정한다. 원본 레이어 이름을 기록하는 옵션은 꺼서 이미 만든 `layer` 필드를 그대로 쓴다. 레이어를 개별적으로 내보내도 된다. [QGIS 벡터 레이어 병합 문서](https://docs.qgis.org/3.44/en/docs/user_manual/processing_algs/qgis/vectorgeneral.html#merge-vector-layers)
3. 각 결과를 **Export → Save Features As…**에서 형식 `GeoJSON`, 인코딩 `UTF-8`로 저장한다. 경위도는 CRS `EPSG:4326`, `RFC7946=YES`, `COORDINATE_PRECISION=7`을 사용한다. 투영 미터 CRS를 유지할 때는 `RFC7946=NO`, `COORDINATE_PRECISION=2`를 사용한다. [GDAL GeoJSON 내보내기 옵션](https://gdal.org/en/stable/drivers/vector/geojson.html#layer-creation-options)
4. QGIS의 Python 콘솔에서 스크립트 편집기를 열고 아래 예시의 프로젝트 경로와 내보낸 파일 이름을 바꿔 실행한다. 입력 파일 순서와 각 파일의 피처 순서가 최종 파일에 유지된다. 세부 지형의 덮어쓰기 순서가 중요하면 바탕과 세부 레이어를 따로 내보내고 목록에 바탕부터 적는다.

```python
import json
from pathlib import Path

project = Path(r"C:\work\RTSBasic")
exports = project / "qgis_export"
inputs = ["areas.geojson", "lines.geojson", "points.geojson"]
documents = [json.loads((exports / name).read_text(encoding="utf-8-sig")) for name in inputs]
crs = documents[0].get("crs")
if any(doc["type"] != "FeatureCollection" or doc.get("crs") != crs for doc in documents):
    raise ValueError("모든 입력을 같은 CRS의 GeoJSON FeatureCollection으로 내보내세요.")

result = {
    "type": "FeatureCollection",
    "minrts_default_terrain": "plain",
    "features": [feature for doc in documents for feature in doc["features"]],
}
if crs is not None:
    result["crs"] = crs
target = project / "data" / "maps" / "my_map.geojson"
target.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
```

## 샌드박스에서 열기

- 최종 `.geojson` 파일을 `data/maps/`에 둔다. 실행 파일과 함께 복사된 `data` 폴더를 사용하는 경우 그쪽 `maps/`에도 새 파일을 복사한다.
- **Sandbox**에서 시나리오를 하나 선택하고 **Map** 목록에서 새 GeoJSON을 고른 뒤 **Start Sandbox**를 누른다. 지도를 바꿔 고르면 기존 시나리오의 고정 기지 배치가 초기화된다.
- 시나리오에 지도를 기본값으로 지정하려면 `data/sandbox/` 아래 Lua 파일의 `map` 항목에 다음 설정을 넣는다. `path`는 시나리오 Lua 파일 기준 상대 경로다. 샌드박스에서는 **Scenario map**을 선택한다.

```lua
map = { source = "geojson", path = "../maps/my_map.geojson" },
```

예제 `data/maps/rhineland.geojson`은 `data/sandbox/rhineland_qgis.lua`에서 바로 열 수 있다. 도로, 강, 도시, 광산, 초기 영토를 함께 담고 있다.

## 범위와 불러오기 오류

| 현상 | 확인할 항목 |
| --- | --- |
| `cannot open` | `.geojson` 확장자와 실제 데이터 폴더를 확인한다. 시나리오 경로는 Lua 파일의 위치를 기준으로 확인한다. |
| `not a GeoJSON FeatureCollection` | 최상위 `type`이 `FeatureCollection`이고 `features`가 배열인지 확인한다. 레이어별 파일을 단순히 이어 붙인 경우 위의 병합 스크립트를 사용한다. |
| `no features with a valid properties.terrain` | `terrain` 식별자가 있는 유효한 지형 피처를 적어도 하나 넣는다. 도로·도시만 있는 파일에는 지도 크기를 정할 지형이 필요하다. |
| `map extent is too small` | 지형 범위가 최소 8×8헥스인지 확인한다. 기본 `hex_distance_km = 20`에서 약 160×139km가 필요하다. 단위와 내보낸 CRS도 확인한다. |
| 지도의 오른쪽이나 아래가 잘림 | `data/game_data.lua`의 `config.map_max_km`(기본 1,200km)이 동서·남북 범위에 각각 적용된다. 더 작은 영역으로 잘라 내보내거나 설정을 조정한다. |
| 콘솔에 `items skipped`가 표시됨 | 필드 이름·값·형식, 도형 유형, 점 피처가 지형 범위 안에 있는지 확인한다. 유효한 나머지 피처로 지도를 불러온다. |
| 유닛이 배치되지 않음 | 매치 이벤트에서 편제 이름 또는 통행 가능한 위치 오류를 확인한다. 진영별 편제 목록은 시나리오와 프로필에서 결정한다. |

## 내보내기 전 체크리스트

- [ ] 모든 레이어가 같은 CRS이고 지형 피처가 플레이 영역을 덮는다.
- [ ] `layer`와 필수 필드가 들어 있으며, 인구와 매장량을 숫자로 내보낸다.
- [ ] 도로·강은 선, 도시·광산·유닛은 점, 영토는 면이다.
- [ ] 초기 유닛의 편제 이름과 기지의 시나리오 좌표를 확인했다.
- [ ] 샌드박스에서 지도, 양측 영토, 유닛 배치와 광산 연결을 확인했다.
