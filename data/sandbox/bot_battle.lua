-- AI vs AI 봇전투 예시. 양쪽 모두 ai = true면 관전 모드로 진행된다.
return {
    name = "Bot Battle: Armor vs Infantry",
    description = "Hard armored AI against a larger Normal infantry AI.",
    match_seconds = 1200,

    map = { source = "random", seed = 2026, width_km = 1200, height_km = 1000 },

    sides = {
        {
            name = "Blue Armor", ai = true, difficulty = "Hard",
            templates = {
                { name = "Armored Div", battalions = { "tank", "tank", "mech", "arty" } },
                { name = "Mech Div",    battalions = { "mech", "mech", "tank", "recon" } },
            },
            divisions = 4,
        },
        {
            name = "Red Infantry", ai = true, difficulty = "Normal", income = 1.3,
            templates = {
                { name = "Infantry Div", battalions = { "inf", "inf", "at", "arty" } },
                { name = "Mountain Div", battalions = { "mtn", "mtn", "arty", "med" } },
            },
            divisions = 6,
        },
    },
}
