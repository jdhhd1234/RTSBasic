-- Lua로 고유 대대·사단을 설계하는 예시. 적군 편제와 초기 사단도 Lua로 정한다.
return {
    name = "Dragon Valley",
    description = "Custom battalions: dragons, battle mages, trolls vs a goblin horde.",
    match_seconds = 900,

    map = { source = "random", seed = 77, width_km = 800, height_km = 700 },

    -- 기본 20종 뒤에 추가된다. class/effect는 main_real.md의 목록을 쓴다.
    -- 커스텀 전용 effect: "all_terrain"(지형 이동 패널티 없음), "regen"(교전 중에도 체력 1%/초)
    battalions = {
        { id = "dragon",  name = "Dragon",      class = "air",   attack = 55, defense = 18, speed = 3.5, range = 30, hp = 260, effect = "all_terrain", factory = "armor", manpower = 200, materials = { iron = 30, oil = 10, rare_earth = 4 } },
        { id = "mage",    name = "Battle Mage", class = "arty",  attack = 30, defense = 6,  speed = 1.4, range = 50, hp = 70, factory = "ew", manpower = 150, materials = { quartz = 4, rare_earth = 3 } },
        { id = "troll",   name = "Troll",       class = "inf",   attack = 18, defense = 20, speed = 1.0, range = 20, hp = 220,  effect = "regen", factory = "infantry", manpower = 300, materials = { iron = 10, oil = 2 } },
        { id = "goblin",  name = "Goblin Mob",  class = "inf",   attack = 9,  defense = 6,  speed = 1.8, range = 20, hp = 80, factory = "infantry", manpower = 900, materials = { iron = 2, oil = 1 } },
        { id = "warg",    name = "Warg Riders", class = "mech",  attack = 16, defense = 8,  speed = 3.0, range = 20, hp = 90, factory = "armor", manpower = 400, materials = { iron = 6, oil = 4 } },
        { id = "shaman",  name = "Shaman",      class = "support", attack = 4, defense = 5, speed = 1.6, range = 20, hp = 60,  effect = "medical", factory = "infantry", manpower = 200, materials = { iron = 2, quartz = 1 } },
    },

    sides = {
        {
            name = "Kingdom",
            ai = false,
            templates = {
                { name = "Dragon Wing",  battalions = { "dragon", "aa" } },
                { name = "Arcane Corps", battalions = { "troll", "troll", "mage", "inf" } },
                { name = "Royal Army",   battalions = { "inf", "inf", "arty", "eng" } },
            },
            divisions = { "Dragon Wing", "Arcane Corps", "Royal Army" },
        },
        {
            name = "Horde",
            ai = true,
            difficulty = "Hard",
            income = 1.6,
            templates = {
                { name = "Goblin Swarm", battalions = { "goblin", "goblin", "warg", "shaman" } },
                { name = "Warg Pack",    battalions = { "warg", "warg", "goblin" } },
                { name = "Troll Band",   battalions = { "troll", "goblin", "goblin" } },
            },
            divisions = { "Goblin Swarm", "Goblin Swarm", "Warg Pack", "Warg Pack", "Troll Band" },
        },
    },
}
