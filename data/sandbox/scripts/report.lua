-- 예시 경기 스크립트: 1분마다 양측 상황을 이벤트 기록에 남긴다.
-- game은 읽기 전용이다. game.time = 0 같은 쓰기는 오류가 난다.
interval = 60

local function summary(game, side)
    local f = game.factions[side]
    local divisions, hp = 0, 0
    for _, d in ipairs(game.divisions) do
        if d.faction == f.id then
            divisions = divisions + 1
            hp = hp + d.hp
        end
    end
    return string.format("%s: %d hex, %d div (hp %.0f), oil %.0f, %s",
        f.name, f.owned_hexes, divisions, hp, f.stock.oil, f.posture)
end

function on_start(game)
    print(string.format("map %.0fx%.0f km, %d bases", game.map.width_km, game.map.height_km, #game.bases))
end

function on_tick(game)
    print(string.format("%d min | %s | %s", math.floor(game.time / 60), summary(game, 1), summary(game, 2)))
end
