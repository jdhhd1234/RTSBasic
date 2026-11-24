MinRTS sandbox scenarios
========================

Every *.lua file in this folder shows up in the Sandbox menu.
A scenario returns one table. All fields are optional unless marked.

  name, description        text shown in the menu
  match_seconds            match length (default 1200, min 60)
  script = "scripts/report.lua"   -- optional Lua run during the match (path relative to this file)
      The script can READ the game state but cannot change it. Lua cannot write C++ values.
      interval = 5                   -- seconds between on_tick calls (default 1, min 0.1)
      function on_start(game) end    -- once after the match starts
      function on_tick(game) end     -- every interval
      print(...)                     -- shows "[script] ..." in the event log
      game.time, game.match_seconds, game.unlimited, game.over, game.winner, game.title
      game.factions[1|2]   = { id, name, ai, manpower, munitions, quality, draft_pressure, upkeep,
                               fuel_shortage, owned_hexes, factories, queue, posture, stock = { oil, iron, ... } }
                               (factions[1] = blue id 0, factions[2] = red id 1)
      game.divisions[i]    = { id, name, faction, x, y, hp, max_hp, frontage_km, retreating, in_combat,
                               battalions = { { type, task, x, y }, ... } }
      game.bases[i]        = { name, type, faction, hex, x, y, hp, max_hp }
      game.map             = { cols, rows, width_km, height_km, hex_km }
      Writing any game field raises "game is read-only". Only base/table/string/math libs (no io/os).
      A script error or a call longer than 10M instructions stops the script and logs the error.

  map = { source = "random", seed = 1, width_km = 800, height_km = 800 }   -- 400..1200 km
  map = { source = "geojson", path = "../maps/file.geojson" }             -- path relative to this file

  bases = {                -- optional fixed base positions (hex col/row). Each side needs one hq.
      { side = 1, type = "hq", col = 4, row = 30 },        -- side 1 = blue, 2 = red
      { side = 1, type = "corps", col = 10, row = 15 },    -- type: hq | corps | division
  }

  battalions = {           -- custom battalions, appended after the 20 standard ones
      { id = "dragon", name = "Dragon", class = "air", attack = 55, defense = 18,
        speed = 3.5, range = 30, hp = 260, effect = "all_terrain",
        factory = "armor", manpower = 200, materials = { iron = 30, oil = 10, rare_earth = 4 } },
  }
      factory  : infantry armor ew munitions   (factory needed to build it; see game_data.lua)
      materials: oil iron nickel quartz rare_earth manganese (optional, default none)
      manpower : optional, default 500
      class : inf mech armor arty aa at air support      (picks the matchup row/column)
      effect: none engineer recon signals medical transport maintenance cbrn
              mountain police ew all_terrain regen

  sides = {
      { name = "Blue", ai = false,                -- ai = true on both sides = AI vs AI bot battle
        difficulty = "Normal",                    -- Easy | Normal | Hard (or 1..3)
        income = 1.0,                             -- income multiplier
        templates = { { name = "Div", battalions = { "inf", "inf", "arty" } } },   -- omit = profile templates (blue) / ai templates (red)
        divisions = 3 },                          -- count (cycles templates) or { "Div", "Div" }
      { name = "Red", ai = true, ... },
  }

GeoJSON (QGIS export)
---------------------
Export a layer from QGIS as GeoJSON (EPSG:4326 or a metric projection).
Each feature needs properties.terrain = sea | plain | forest | mountain | river | city | swamp | desert | plateau.
Polygons fill hexes, lines (rivers) paint the hexes they cross, points paint one hex.
Later features overwrite earlier ones. Top-level "minrts_default_terrain" sets the background (default plain).
Maps larger than 1200 km are cut at 1200 km.

Scenario files run in a restricted Lua (base, table, string, math only).
