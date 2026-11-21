#include "core/lua_loader.hpp"
#include "core/lua_read.hpp"

#include <lua.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

LuaState::LuaState()
    : state(luaL_newstate())
{
    if (state == nullptr)
    {
        return;
    }
    // 데이터·시나리오 파일용: io/os 같은 시스템 접근 라이브러리는 열지 않는다.
    luaL_requiref(state, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_settop(state, 0);
}

LuaState::~LuaState()
{
    if (state != nullptr)
    {
        lua_close(state);
    }
}

bool LuaRunFile(LuaState& lua, const std::string& path, std::string& error)
{
    lua_State* L = lua.Get();
    if (L == nullptr)
    {
        error = "failed to create Lua state";
        return false;
    }
    if (luaL_dofile(L, path.c_str()) != LUA_OK)
    {
        const char* message = lua_tostring(L, -1);
        error = message != nullptr ? message : "failed to run " + path;
        return false;
    }
    if (!lua_istable(L, -1))
    {
        error = path + " must return a table";
        return false;
    }
    return true;
}

namespace
{
    void ReadConfig(lua_State* L, int root, GameConfig& c)
    {
        const int t = LuaPushTable(L, root, "config");
        c.hexDistanceKm = LuaReadFloat(L, t, "hex_distance_km", c.hexDistanceKm);
        c.mapCols = LuaReadInt(L, t, "map_cols", c.mapCols);
        c.mapRows = LuaReadInt(L, t, "map_rows", c.mapRows);
        c.mapSeed = LuaReadInt(L, t, "map_seed", c.mapSeed);
        c.mapRivers = LuaReadInt(L, t, "map_rivers", c.mapRivers);
        c.mapCities = LuaReadInt(L, t, "map_cities", c.mapCities);
        c.tickHz = LuaReadFloat(L, t, "tick_hz", c.tickHz);
        c.matchSeconds = LuaReadFloat(L, t, "match_seconds", c.matchSeconds);
        c.engageKm = LuaReadFloat(L, t, "engage_km", c.engageKm);
        c.frontageDefaultKm = LuaReadFloat(L, t, "frontage_default_km", c.frontageDefaultKm);
        c.frontageMinKm = LuaReadFloat(L, t, "frontage_min_km", c.frontageMinKm);
        c.frontageMaxKm = LuaReadFloat(L, t, "frontage_max_km", c.frontageMaxKm);
        c.frontageReferenceKm = LuaReadFloat(L, t, "frontage_reference_km", c.frontageReferenceKm);
        c.territoryClearKm = LuaReadFloat(L, t, "territory_clear_km", c.territoryClearKm);
        c.baseCaptureRadius = LuaReadInt(L, t, "base_capture_radius", c.baseCaptureRadius);
        c.daysPerSecond = LuaReadFloat(L, t, "days_per_second", c.daysPerSecond);
        c.startYear = LuaReadInt(L, t, "start_year", c.startYear);
        c.minesPerSide = LuaReadInt(L, t, "mines_per_side", c.minesPerSide);
        c.startManpower = LuaReadFloat(L, t, "start_manpower", c.startManpower);
        c.startMunitions = LuaReadFloat(L, t, "start_munitions", c.startMunitions);
        c.conscriptManpower = LuaReadFloat(L, t, "conscript_manpower", c.conscriptManpower);
        c.conscriptPressure = LuaReadFloat(L, t, "conscript_pressure", c.conscriptPressure);
        c.pressureDecay = LuaReadFloat(L, t, "pressure_decay", c.pressureDecay);
        c.pressureSafe = LuaReadFloat(L, t, "pressure_safe", c.pressureSafe);
        c.refusalChance = LuaReadFloat(L, t, "refusal_chance", c.refusalChance);
        c.desertionChance = LuaReadFloat(L, t, "desertion_chance", c.desertionChance);
        c.qualityLoss = LuaReadFloat(L, t, "quality_loss", c.qualityLoss);
        c.qualityRecover = LuaReadFloat(L, t, "quality_recover", c.qualityRecover);
        c.qualityMin = LuaReadFloat(L, t, "quality_min", c.qualityMin);
        c.upkeepOil = LuaReadFloat(L, t, "upkeep_oil", c.upkeepOil);
        c.upkeepGrowth = LuaReadFloat(L, t, "upkeep_growth", c.upkeepGrowth);
        c.overstretchDivisions = LuaReadInt(L, t, "overstretch_divisions", c.overstretchDivisions);
        c.commandChance = LuaReadFloat(L, t, "command_chance", c.commandChance);
        c.combatMunitions = LuaReadFloat(L, t, "combat_munitions", c.combatMunitions);
        c.noMunitionsAttack = LuaReadFloat(L, t, "no_munitions_attack", c.noMunitionsAttack);
        c.noFuelAttack = LuaReadFloat(L, t, "no_fuel_attack", c.noFuelAttack);
        c.noFuelSpeed = LuaReadFloat(L, t, "no_fuel_speed", c.noFuelSpeed);
        c.productionSeconds = LuaReadFloat(L, t, "production_seconds", c.productionSeconds);
        c.retreatThreshold = LuaReadFloat(L, t, "retreat_threshold", c.retreatThreshold);
        c.retreatThresholdPolice = LuaReadFloat(L, t, "retreat_threshold_police", c.retreatThresholdPolice);
        c.capturedBaseHp = LuaReadFloat(L, t, "captured_base_hp", c.capturedBaseHp);
        c.supplyFullKm = LuaReadFloat(L, t, "supply_full_km", c.supplyFullKm);
        c.supplyMinKm = LuaReadFloat(L, t, "supply_min_km", c.supplyMinKm);
        c.supplyMin = LuaReadFloat(L, t, "supply_min", c.supplyMin);
        c.transportDivisor = LuaReadFloat(L, t, "transport_divisor", c.transportDivisor);
        c.damageScale = LuaReadFloat(L, t, "damage_scale", c.damageScale);
        c.defenseK = LuaReadFloat(L, t, "defense_k", c.defenseK);
        c.hpFactorMin = LuaReadFloat(L, t, "hp_factor_min", c.hpFactorMin);
        c.weatherRegions = LuaReadInt(L, t, "weather_regions", c.weatherRegions);
        c.weatherMinSeconds = LuaReadFloat(L, t, "weather_min_s", c.weatherMinSeconds);
        c.weatherMaxSeconds = LuaReadFloat(L, t, "weather_max_s", c.weatherMaxSeconds);
        c.maxTemplates = LuaReadInt(L, t, "max_templates", c.maxTemplates);
        c.maxBattalions = LuaReadInt(L, t, "max_battalions", c.maxBattalions);
        c.maxSameBattalion = LuaReadInt(L, t, "max_same_battalion", c.maxSameBattalion);
        c.recoverIdleSeconds = LuaReadFloat(L, t, "recover_idle_s", c.recoverIdleSeconds);
        c.medicalRate = LuaReadFloat(L, t, "medical_rate", c.medicalRate);
        c.maintenanceRate = LuaReadFloat(L, t, "maintenance_rate", c.maintenanceRate);
        c.reconAttack = LuaReadFloat(L, t, "recon_attack", c.reconAttack);
        c.signalsBonus = LuaReadFloat(L, t, "signals_bonus", c.signalsBonus);
        c.mountainAttack = LuaReadFloat(L, t, "mountain_attack", c.mountainAttack);
        c.ewPenalty = LuaReadFloat(L, t, "ew_penalty", c.ewPenalty);
        c.aiDefenseKm = LuaReadFloat(L, t, "ai_defense_km", c.aiDefenseKm);
        c.baseRegen = LuaReadFloat(L, t, "base_regen", c.baseRegen);
        c.regenRate = LuaReadFloat(L, t, "regen_rate", c.regenRate);
        c.mapMinKm = LuaReadFloat(L, t, "map_min_km", c.mapMinKm);
        c.mapMaxKm = LuaReadFloat(L, t, "map_max_km", c.mapMaxKm);
        c.roadSpeedHighway = LuaReadFloat(L, t, "road_speed_highway", c.roadSpeedHighway);
        c.roadSpeedMajor = LuaReadFloat(L, t, "road_speed_major", c.roadSpeedMajor);
        c.roadSpeedMinor = LuaReadFloat(L, t, "road_speed_minor", c.roadSpeedMinor);
        c.cityManpowerPer100k = LuaReadFloat(L, t, "city_manpower_per_100k", c.cityManpowerPer100k);
        c.elevationDefensePer100m = LuaReadFloat(L, t, "elevation_defense_per_100m", c.elevationDefensePer100m);
        c.elevationDefenseMax = LuaReadFloat(L, t, "elevation_defense_max", c.elevationDefenseMax);
        LuaPop(L);

        if (c.mapCols < 8 || c.mapRows < 8 || c.hexDistanceKm <= 0.0f || c.tickHz <= 0.0f || c.weatherRegions < 1 || c.daysPerSecond <= 0.0f)
        {
            LuaFail("config has invalid map/tick/weather values");
        }
        if (!(c.roadSpeedHighway > 0.0f && c.roadSpeedMajor > 0.0f && c.roadSpeedMinor > 0.0f
              && c.cityManpowerPer100k >= 0.0f && c.elevationDefensePer100m >= 0.0f && c.elevationDefenseMax >= 0.0f))
        {
            LuaFail("config has invalid road/city/elevation values");
        }
        if (!(0.0f < c.frontageMinKm && c.frontageMinKm <= c.frontageDefaultKm && c.frontageDefaultKm <= c.frontageMaxKm)
            || !std::isfinite(c.frontageMaxKm) || !(c.frontageReferenceKm > 0.0f) || !std::isfinite(c.frontageReferenceKm))
        {
            LuaFail("config has invalid frontage values");
        }
    }

    // id로 고정 배열 슬롯을 채우는 표 (terrain/weather/bases). 빠진 항목이 있으면 실패.
    template <std::size_t N, typename Fill>
    void ReadKeyedTable(lua_State* L, int root, const char* key, const std::array<const char*, N>& names, Fill&& fill)
    {
        const int t = LuaPushTable(L, root, key);
        std::array<bool, N> found{};
        LuaForEachItem(L, t, [&](int item)
        {
            LuaRequireTable(L, item, std::string(key) + " entry");
            const std::string id = LuaReadString(L, item, "id", "");
            const int index = FindName(names, id);
            if (index < 0)
            {
                LuaFail(std::string(key) + ": unknown id '" + id + "'");
            }
            fill(static_cast<std::size_t>(index), item, std::string(key) + "." + id);
            found[static_cast<std::size_t>(index)] = true;
        });
        LuaPop(L);

        for (std::size_t i = 0; i < N; ++i)
        {
            if (!found[i])
            {
                LuaFail(std::string(key) + ": '" + names[i] + "' is missing");
            }
        }
    }

    void ReadTerrain(lua_State* L, int root, GameData& data)
    {
        ReadKeyedTable(L, root, "terrain", TerrainNames, [&](std::size_t index, int item, const std::string& context)
        {
            TerrainDef& def = data.terrain[index];
            def.move = LuaReadRequired(L, item, "move", context);
            def.defense = LuaReadRequired(L, item, "defense", context);
            def.armorAttack = LuaReadFloat(L, item, "armor_attack", 1.0f);
            const int color = LuaPushOptionalTable(L, item, "color");
            if (color != 0)
            {
                for (int c = 0; c < 3; ++c)
                {
                    lua_rawgeti(L, color, c + 1);
                    def.color[static_cast<std::size_t>(c)] = static_cast<unsigned char>(lua_tointeger(L, -1));
                    lua_pop(L, 1);
                }
                LuaPop(L);
            }
        });
    }

    void ReadWeather(lua_State* L, int root, GameData& data)
    {
        ReadKeyedTable(L, root, "weather", WeatherNames, [&](std::size_t index, int item, const std::string& context)
        {
            WeatherDef& def = data.weather[index];
            def.move = LuaReadRequired(L, item, "move", context);
            def.range = LuaReadRequired(L, item, "range", context);
            def.attack = LuaReadRequired(L, item, "attack", context);
            def.variance = LuaReadFloat(L, item, "variance", 0.0f);
            def.weight = LuaReadFloat(L, item, "weight", 1.0f);
        });
    }

    void ReadBases(lua_State* L, int root, GameData& data)
    {
        ReadKeyedTable(L, root, "bases", BaseTypeNames, [&](std::size_t index, int item, const std::string& context)
        {
            BaseDef& def = data.bases[index];
            def.hp = LuaReadRequired(L, item, "hp", context);
            def.defense = LuaReadRequired(L, item, "defense", context);
            def.attack = LuaReadRequired(L, item, "attack", context);
            def.penalty = LuaReadFloat(L, item, "penalty", 1.0f);
        });
    }

    void ReadMatchup(lua_State* L, int root, GameData& data)
    {
        for (auto& row : data.matchup)
        {
            row.fill(1.0f);
        }
        const int t = LuaPushTable(L, root, "matchup");
        for (std::size_t a = 0; a < UnitClassCount; ++a)
        {
            const int row = LuaPushOptionalTable(L, t, UnitClassNames[a]);
            if (row != 0)
            {
                for (std::size_t b = 0; b < UnitClassCount; ++b)
                {
                    data.matchup[a][b] = LuaReadFloat(L, row, UnitClassNames[b], 1.0f);
                }
                LuaPop(L);
            }
        }
        LuaPop(L);
    }

    void ReadStages(lua_State* L, int root, GameData& data)
    {
        const int t = LuaPushTable(L, root, "stages");
        LuaForEachItem(L, t, [&](int item)
        {
            LuaRequireTable(L, item, "stage entry");
            StageDef def;
            def.name = LuaReadString(L, item, "name", "Stage");
            def.playerDivisions = static_cast<int>(LuaReadRequired(L, item, "player_divisions", "stages." + def.name));
            def.aiDivisions = static_cast<int>(LuaReadRequired(L, item, "ai_divisions", "stages." + def.name));
            def.aiIncome = LuaReadFloat(L, item, "ai_income", 1.0f);
            data.stages.push_back(std::move(def));
        });
        LuaPop(L);
        if (data.stages.empty())
        {
            LuaFail("no stages defined");
        }
    }

    void ReadDifficulties(lua_State* L, int root, GameData& data)
    {
        const int t = LuaPushTable(L, root, "difficulties");
        LuaForEachItem(L, t, [&](int item)
        {
            LuaRequireTable(L, item, "difficulty entry");
            DifficultyDef def;
            def.name = LuaReadString(L, item, "name", "Normal");
            def.aiIncome = LuaReadFloat(L, item, "ai_income", 1.0f);
            def.thinkSeconds = LuaReadFloat(L, item, "think_s", 5.0f);
            def.group = std::max(1, LuaReadInt(L, item, "group", 1));
            data.difficulties.push_back(std::move(def));
        });
        LuaPop(L);
        if (data.difficulties.empty())
        {
            LuaFail("no difficulties defined");
        }
    }

    void ReadEconomy(lua_State* L, int root, GameData& data)
    {
        const int factories = LuaPushTable(L, root, "factories");
        LuaForEachItem(L, factories, [&](int item)
        {
            LuaRequireTable(L, item, "factory entry");
            FactoryDef def;
            def.id = LuaReadString(L, item, "id", "");
            const std::string context = "factories." + def.id;
            def.name = LuaReadString(L, item, "name", def.id);
            def.buildDays = LuaReadRequired(L, item, "build_days", context);
            def.cost = LuaReadResources(L, item, "cost", context);
            def.input = LuaReadResources(L, item, "input", context);
            def.munitions = LuaReadFloat(L, item, "munitions", 0.0f);
            if (def.id.empty() || def.buildDays <= 0.0f || def.munitions < 0.0f)
            {
                LuaFail(context + ": needs id and positive build_days");
            }
            data.factories.push_back(std::move(def));
        });
        LuaPop(L);
        if (data.factories.empty())
        {
            LuaFail("no factories defined");
        }

        const int mines = LuaPushTable(L, root, "mines");
        LuaForEachItem(L, mines, [&](int item)
        {
            LuaRequireTable(L, item, "mine entry");
            MineRuleDef rule;
            const std::string resource = LuaReadString(L, item, "resource", "");
            const int index = FindName(ResourceNames, resource);
            if (index < 0)
            {
                LuaFail("mines: unknown resource '" + resource + "'");
            }
            rule.resource = static_cast<Resource>(index);
            rule.rate = LuaReadRequired(L, item, "rate", "mines." + resource);
            rule.weight = LuaReadFloat(L, item, "weight", 1.0f);
            const int terrain = LuaPushTable(L, item, "terrain");
            LuaForEachItem(L, terrain, [&](int entry)
            {
                const int t = FindName(TerrainNames, LuaToString(L, entry));
                if (t < 0)
                {
                    LuaFail("mines." + resource + ": unknown terrain");
                }
                rule.terrainMask |= 1u << static_cast<unsigned>(t);
            });
            LuaPop(L);
            data.mineRules.push_back(rule);
        });
        LuaPop(L);

        data.startStock = LuaReadResources(L, root, "start_stock", "root");
        const int start = LuaPushTable(L, root, "start_factories");
        LuaForEachItem(L, start, [&](int entry)
        {
            const std::string id = LuaToString(L, entry);
            const auto found = std::find_if(data.factories.begin(), data.factories.end(), [&](const FactoryDef& f) { return f.id == id; });
            if (found == data.factories.end())
            {
                LuaFail("start_factories: unknown factory '" + id + "'");
            }
            data.startFactories.push_back(static_cast<int>(found - data.factories.begin()));
        });
        LuaPop(L);
    }

    std::vector<TemplateDef> ReadTemplates(lua_State* L, int root, const char* key, const GameData& data)
    {
        const int t = LuaPushTable(L, root, key);
        std::vector<TemplateDef> result = LuaReadTemplateList(L, t, data, key);
        LuaPop(L);
        if (result.empty())
        {
            LuaFail(std::string(key) + " is empty");
        }
        return result;
    }
}

bool LoadGameData(const std::string& path, GameData& out, std::string& error)
{
    LuaState lua;
    if (!LuaRunFile(lua, path, error))
    {
        return false;
    }

    lua_State* L = lua.Get();
    const int root = lua_gettop(L);
    try
    {
        GameData data;
        ReadConfig(L, root, data.config);
        ReadTerrain(L, root, data);
        ReadWeather(L, root, data);
        ReadBases(L, root, data);
        ReadEconomy(L, root, data);

        const int battalions = LuaPushTable(L, root, "battalions");
        LuaForEachItem(L, battalions, [&](int item) { data.battalions.push_back(LuaReadBattalion(L, item, data.factories)); });
        LuaPop(L);
        if (data.battalions.empty())
        {
            LuaFail("no battalions defined");
        }

        ReadMatchup(L, root, data);
        ReadStages(L, root, data);
        ReadDifficulties(L, root, data);
        data.defaultTemplates = ReadTemplates(L, root, "default_templates", data);
        data.aiTemplates = ReadTemplates(L, root, "ai_templates", data);
        out = std::move(data);
        return true;
    }
    catch (const std::runtime_error& e)
    {
        error = path + ": " + e.what();
        return false;
    }
}

bool LoadStringPairs(const std::string& path, std::vector<std::pair<std::string, std::string>>& out, std::string& error)
{
    LuaState lua;
    if (!LuaRunFile(lua, path, error))
    {
        return false;
    }
    lua_State* L = lua.Get();
    const int table = lua_gettop(L);
    out.clear();
    lua_pushnil(L);
    while (lua_next(L, table) != 0)
    {
        // lua_tostring이 숫자 키를 바꾸지 않도록 type으로 먼저 거른다.
        if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING)
        {
            out.emplace_back(lua_tostring(L, -2), lua_tostring(L, -1));
        }
        lua_pop(L, 1);
    }
    return true;
}
