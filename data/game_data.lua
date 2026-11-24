-- MinRTS 밸런스·설정 데이터. 로직은 넣지 않는다. 기준 문서: main_real.md

return {
    config = {
        hex_distance_km = 20,
        map_cols = 60,
        map_rows = 69,
        map_seed = 1234,
        map_rivers = 6,
        map_cities = 24,
        tick_hz = 10,
        match_seconds = 1200,
        engage_km = 20,
        frontage_default_km = 30,
        frontage_min_km = 10,
        frontage_max_km = 100,
        frontage_reference_km = 30,
        territory_clear_km = 30,
        base_capture_radius = 2,
        production_seconds = 30,
        -- 경제 (problem.md 2026/11/21)
        days_per_second = 3,       -- 게임 달력: 1초 = 3일 (1200초 ≈ 9.9년, 공장 4년 ≈ 487초)
        start_year = 2030,
        mines_per_side = 12,
        start_manpower = 20000,
        start_munitions = 150,
        conscript_manpower = 12000,
        conscript_pressure = 0.3,  -- 징집 1회당 압력
        pressure_decay = 0.002,    -- 초당 압력 감소 (징집 1회 해소에 150초)
        pressure_safe = 0.5,       -- 넘으면 징집거부·탈영·군사질 하락이 매우 잦아진다
        refusal_chance = 0.9,
        desertion_chance = 0.08,
        quality_loss = 0.06,
        quality_recover = 0.002,
        quality_min = 0.5,
        upkeep_oil = 0.02,         -- 사단당 석유/초 × (1 + upkeep_growth × 사단 수)
        upkeep_growth = 0.05,
        overstretch_divisions = 12,
        command_chance = 0.01,
        combat_munitions = 0.05,
        no_munitions_attack = 0.4,
        no_fuel_attack = 0.7,
        no_fuel_speed = 0.5,
        retreat_threshold = 0.30,
        retreat_threshold_police = 0.20,
        captured_base_hp = 0.30,
        supply_full_km = 250,
        supply_min_km = 650,
        supply_min = 0.5,
        transport_divisor = 1.5,
        damage_scale = 0.15,
        defense_k = 20,
        hp_factor_min = 0.5,
        weather_regions = 4,
        weather_min_s = 60,
        weather_max_s = 120,
        max_templates = 6,
        max_battalions = 4,
        max_same_battalion = 2,
        recover_idle_s = 1.0,
        medical_rate = 0.01,
        maintenance_rate = 0.005,
        recon_attack = 0.10,
        signals_bonus = 0.10,
        mountain_attack = 0.30,
        ew_penalty = 0.10,
        ai_defense_km = 100,
        base_regen = 1.0,        -- 적이 없을 때 기지 체력 회복/초
        regen_rate = 0.01,       -- 커스텀 효과 regen: 교전 중에도 최대 체력 1%/초
        map_min_km = 400,
        map_max_km = 1200,
        road_speed_highway = 2.0,
        road_speed_major = 1.6,
        road_speed_minor = 1.3,
        city_manpower_per_100k = 0.25,  -- 인구 10만 명당 초당 인력
        elevation_defense_per_100m = 0.03,
        elevation_defense_max = 0.30,
    },

    -- AI·전투 조정값. 빠진 키는 C++ 기본값 (src/core/game_data.hpp AiTuning).
    ai_tuning = {
        -- 사단 Utility AI
        hold_score = 0.15,          -- 할 일이 마땅치 않을 때 제자리 점수
        inertia_bonus = 0.1,        -- 지금 목표를 유지하면 더하는 점수 (흔들림 방지)
        rally_radius_km = 60,       -- 집결지 도착 판정·집결 병력 집계 반경
        raid_radius_km = 150,       -- 기습 후보로 볼 적 기지 거리
        recover_health = 0.4,       -- 사단 hp 비율이 이보다 낮으면 회복 후보
        defend_frontage_km = 20,    -- 방어 시 기본 전선에 더하는 km
        attack_frontage_km = -10,   -- 공격·기습 시 기본 전선에 더하는 km
        -- 대대 AI
        battalion_think_s = 1.0,
        rear_km = 6,                -- 지원 대대가 배치선 뒤로 물러나는 거리
        standoff_ratio = 0.7,       -- 교전 시 적과의 거리 = 사거리 × 비율
        artillery_ratio = 0.85,
        cover_hp_ratio = 0.4,       -- 사단 hp가 이보다 낮으면 교전 대신 엄폐
        screen_ratio = 0.6,         -- 정찰 대대가 나가는 거리 = 담당 전선 × 비율
        -- 전투
        range_tolerance_km = 0.5,   -- 사거리 판정 여유
    },

    -- armor_attack: 전차·기계화 계열 공격력 배율
    terrain = {
        { id = "sea",      move = 0.0, defense = 1.00, armor_attack = 1.0, color = { 70, 110, 150 } },
        { id = "plain",    move = 1.0, defense = 1.00, armor_attack = 1.0, color = { 196, 206, 160 } },
        { id = "forest",   move = 0.7, defense = 1.25, armor_attack = 0.7, color = { 112, 150, 96 } },
        { id = "mountain", move = 0.4, defense = 1.60, armor_attack = 0.6, color = { 140, 125, 110 } },
        { id = "river",    move = 0.5, defense = 1.20, armor_attack = 1.0, color = { 105, 155, 205 } },
        { id = "city",     move = 0.8, defense = 1.50, armor_attack = 0.7, color = { 168, 168, 168 } },
        { id = "swamp",    move = 0.4, defense = 1.20, armor_attack = 0.6, color = { 122, 140, 112 } },
        { id = "desert",   move = 0.8, defense = 0.90, armor_attack = 1.0, color = { 222, 205, 150 } },
        { id = "plateau",  move = 0.7, defense = 1.30, armor_attack = 1.0, color = { 176, 166, 126 } },
    },

    -- weight: 날씨가 바뀔 때 뽑힐 가중치
    weather = {
        { id = "clear", move = 1.0, range = 1.0, attack = 1.00, variance = 0.05, weight = 50 },
        { id = "rain",  move = 0.7, range = 1.0, attack = 0.95, variance = 0.10, weight = 20 },
        { id = "snow",  move = 0.6, range = 1.0, attack = 0.90, variance = 0.10, weight = 10 },
        { id = "fog",   move = 0.9, range = 0.5, attack = 0.95, variance = 0.15, weight = 20 },
    },

    -- penalty: 원소유측이 잃었을 때 생산속도 배율. 기지를 잃으면 그곳 공장도 파괴된다.
    bases = {
        { id = "hq",       hp = 600, defense = 40, attack = 15, penalty = 1.0 },
        { id = "corps",    hp = 400, defense = 30, attack = 12, penalty = 0.5 },
        { id = "division", hp = 250, defense = 25, attack = 10, penalty = 0.85 },
    },

    -- 공장: 건설에 build_days(게임 일수). 대대는 factory가 하나 이상 가동 중이어야 생산된다.
    -- 같은 공장이 여러 개면 생산이 빨라진다 (1개 1배, 2개 1.5배, 3개 2배).
    factories = {
        { id = "infantry",  name = "Infantry Equipment Factory", build_days = 1460, cost = { iron = 120, oil = 40 } },
        { id = "armor",     name = "Armored Vehicle Factory",    build_days = 1460, cost = { iron = 200, oil = 60, nickel = 40, manganese = 40 } },
        { id = "ew",        name = "EW Equipment Factory",       build_days = 1460, cost = { iron = 80, quartz = 30, rare_earth = 40 } },
        -- 군수품 공장: 자원을 소모해 군수품을 만든다. 자원이 없으면 군수품 생산이 멈춘다.
        { id = "munitions", name = "Munitions Factory",          build_days = 1460, cost = { iron = 150, oil = 50, nickel = 20 },
          input = { iron = 0.1, oil = 0.1 }, munitions = 0.5 },
    },
    start_factories = { "infantry", "armor", "munitions" },
    start_stock = { oil = 300, iron = 300, nickel = 60, quartz = 20, rare_earth = 20, manganese = 60 },

    -- 광산: mines_per_side개를 지형 규칙대로 놓고 좌우 대칭 복사. 처음 6개는 자원마다 하나씩 보장.
    -- rate = 초당 채굴량. 아군 영토로 공장까지 이어져야 들어온다. weight가 낮을수록 희귀.
    mines = {
        { resource = "oil",        terrain = { "desert", "plain", "swamp" },       rate = 0.20, weight = 30 },
        { resource = "iron",       terrain = { "mountain", "plateau", "plain" },   rate = 0.20, weight = 30 },
        { resource = "nickel",     terrain = { "mountain", "forest" },             rate = 0.08, weight = 15 },
        { resource = "quartz",     terrain = { "mountain", "plateau" },            rate = 0.03, weight = 5 },   -- 고순도 석영 (희귀)
        { resource = "rare_earth", terrain = { "mountain", "desert" },             rate = 0.04, weight = 8 },   -- 전자전 장비 필수
        { resource = "manganese",  terrain = { "plateau", "plain", "mountain" },   rate = 0.08, weight = 12 },  -- 고급 장갑
    },

    battalions = {
        { id = "inf",   name = "Infantry",        class = "inf",     attack = 10, defense = 12, speed = 1.2, range = 20, hp = 100, app6 = "UCI---", factory = "infantry", manpower = 800, materials = { iron = 6, oil = 2 } },
        { id = "mech",  name = "Mech Infantry",   class = "mech",    attack = 14, defense = 14, speed = 2.0, range = 20, hp = 110, app6 = "UCII--", factory = "armor", manpower = 700, materials = { iron = 12, oil = 6, nickel = 2, manganese = 2 } },
        { id = "tank",  name = "Tank",            class = "armor",   attack = 22, defense = 16, speed = 2.2, range = 20, hp = 120, app6 = "UCA---", factory = "armor", manpower = 500, materials = { iron = 20, oil = 8, nickel = 4, manganese = 5 } },
        { id = "arty",  name = "Artillery",       class = "arty",    attack = 16, defense = 5,  speed = 1.2, range = 40, hp = 70,  app6 = "UCF---", factory = "munitions", manpower = 500, materials = { iron = 12, oil = 4, nickel = 1 } },
        { id = "eng",   name = "Engineer",        class = "support", attack = 6,  defense = 10, speed = 1.4, range = 20, hp = 80,  app6 = "UCE---", effect = "engineer", factory = "infantry", manpower = 600, materials = { iron = 6, oil = 3 } },
        { id = "recon", name = "Recon",           class = "support", attack = 8,  defense = 6,  speed = 3.0, range = 30, hp = 60,  app6 = "UCR---", effect = "recon", factory = "infantry", manpower = 400, materials = { iron = 5, oil = 4, quartz = 1 } },
        { id = "spg",   name = "SP Artillery",    class = "arty",    attack = 20, defense = 7,  speed = 2.0, range = 45, hp = 80,  app6 = "UCF---", factory = "armor", manpower = 450, materials = { iron = 16, oil = 6, nickel = 3, manganese = 2 } },
        { id = "mlrs",  name = "MLRS",            class = "arty",    attack = 28, defense = 4,  speed = 1.6, range = 60, hp = 60,  app6 = "UCFR--", factory = "munitions", manpower = 400, materials = { iron = 18, oil = 6, nickel = 2, quartz = 1, rare_earth = 1 } },
        { id = "aa",    name = "Air Defense",     class = "aa",      attack = 8,  defense = 8,  speed = 1.8, range = 30, hp = 70,  app6 = "UCD---", factory = "ew", manpower = 450, materials = { iron = 10, oil = 4, quartz = 2, rare_earth = 2 } },
        { id = "at",    name = "Anti-Tank",       class = "at",      attack = 14, defense = 10, speed = 1.6, range = 25, hp = 80,  app6 = "UCAA--", factory = "infantry", manpower = 600, materials = { iron = 8, oil = 2, manganese = 1 } },
        { id = "heli",  name = "Attack Heli",     class = "air",     attack = 26, defense = 6,  speed = 4.0, range = 30, hp = 60,  app6 = "UCVRA-", factory = "armor", manpower = 300, materials = { iron = 14, oil = 12, nickel = 4, quartz = 2, rare_earth = 2 } },
        { id = "sig",   name = "Signals",         class = "support", attack = 2,  defense = 6,  speed = 1.6, range = 20, hp = 50,  app6 = "UUS---", effect = "signals", factory = "ew", manpower = 300, materials = { iron = 3, oil = 2, quartz = 2, rare_earth = 2 } },
        { id = "med",   name = "Medical",         class = "support", attack = 0,  defense = 5,  speed = 1.6, range = 20, hp = 50,  app6 = "USM---", effect = "medical", factory = "infantry", manpower = 400, materials = { iron = 2, oil = 2 } },
        { id = "trans", name = "Transport",       class = "support", attack = 0,  defense = 5,  speed = 2.4, range = 20, hp = 50,  app6 = "UST---", effect = "transport", factory = "armor", manpower = 400, materials = { iron = 8, oil = 6 } },
        { id = "maint", name = "Maintenance",     class = "support", attack = 0,  defense = 6,  speed = 1.8, range = 20, hp = 50,  app6 = "USX---", effect = "maintenance", factory = "armor", manpower = 400, materials = { iron = 6, oil = 3, nickel = 1 } },
        { id = "cbrn",  name = "CBRN",            class = "support", attack = 2,  defense = 6,  speed = 1.6, range = 20, hp = 50,  app6 = "UUA---", effect = "cbrn", factory = "infantry", manpower = 350, materials = { iron = 4, oil = 2, quartz = 1 } },
        { id = "abn",   name = "Airborne",        class = "inf",     attack = 15, defense = 12, speed = 1.6, range = 20, hp = 90,  app6 = "UCI---", factory = "infantry", manpower = 700, materials = { iron = 8, oil = 4 } },
        { id = "mtn",   name = "Mountain",        class = "inf",     attack = 12, defense = 14, speed = 1.4, range = 20, hp = 100, app6 = "UCI---", effect = "mountain", factory = "infantry", manpower = 800, materials = { iron = 6, oil = 2 } },
        { id = "mp",    name = "Military Police", class = "support", attack = 4,  defense = 8,  speed = 1.6, range = 20, hp = 60,  app6 = "UUL---", effect = "police", factory = "infantry", manpower = 400, materials = { iron = 3, oil = 2 } },
        { id = "ew",    name = "Electronic War",  class = "support", attack = 2,  defense = 6,  speed = 1.6, range = 30, hp = 50,  app6 = "UUM---", effect = "ew", factory = "ew", manpower = 300, materials = { iron = 4, oil = 3, quartz = 3, rare_earth = 4 } },
    },

    -- 공격 클래스 -> 표적 클래스 배율. 빠진 값은 1.0
    matchup = {
        inf     = { inf = 1.0,  mech = 0.9,  armor = 0.7, arty = 1.2,  aa = 1.0, at = 1.1, air = 0.5, support = 1.1 },
        mech    = { inf = 1.15, mech = 1.0,  armor = 0.9, arty = 1.2,  aa = 1.1, at = 1.0, air = 0.6, support = 1.2 },
        armor   = { inf = 1.25, mech = 1.15, armor = 1.0, arty = 1.3,  aa = 1.2, at = 0.8, air = 0.5, support = 1.25 },
        arty    = { inf = 1.25, mech = 1.0,  armor = 0.8, arty = 1.0,  aa = 1.1, at = 1.1, air = 0.3, support = 1.25 },
        aa      = { inf = 0.7,  mech = 0.6,  armor = 0.5, arty = 0.8,  aa = 1.0, at = 0.7, air = 2.0, support = 0.8 },
        at      = { inf = 0.8,  mech = 1.25, armor = 1.5, arty = 1.0,  aa = 1.0, at = 1.0, air = 0.6, support = 0.9 },
        air     = { inf = 1.1,  mech = 1.25, armor = 1.3, arty = 1.25, aa = 0.6, at = 1.1, air = 1.0, support = 1.2 },
        support = { inf = 1.0,  mech = 0.9,  armor = 0.7, arty = 1.0,  aa = 1.0, at = 1.0, air = 0.5, support = 1.0 },
    },

    stages = {
        { name = "Border Clash",    player_divisions = 3, ai_divisions = 2, ai_income = 0.8 },
        { name = "Rhine Offensive", player_divisions = 3, ai_divisions = 4, ai_income = 1.0 },
        { name = "European War",    player_divisions = 3, ai_divisions = 6, ai_income = 1.2 },
    },

    -- group: AI가 공격 전에 모으는 사단 수, 위협받는 기지에 보내는 사단 수
    difficulties = {
        { name = "Easy",   ai_income = 0.7, think_s = 8.0, group = 1 },
        { name = "Normal", ai_income = 1.0, think_s = 5.0, group = 2 },
        { name = "Hard",   ai_income = 1.3, think_s = 3.0, group = 3 },
    },

    default_templates = {
        { name = "Infantry Div", battalions = { "inf", "inf", "arty", "eng" } },
        { name = "Armored Div",  battalions = { "tank", "tank", "mech", "arty" } },
        { name = "Mech Div",     battalions = { "mech", "mech", "tank", "recon" } },
    },

    ai_templates = {
        { name = "Infantry Div", battalions = { "inf", "inf", "arty", "eng" } },
        { name = "Armored Div",  battalions = { "tank", "tank", "mech", "arty" } },
        { name = "Mech Div",     battalions = { "mech", "mech", "tank", "recon" } },
    },
}
