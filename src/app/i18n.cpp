#include "app/i18n.hpp"
#include "core/lua_loader.hpp"
#include "core/phrases.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    std::string language = "en";
    PhraseList phrases;   // PreparePhrases로 정렬됨
    std::unordered_map<std::string, std::string> cache;
    Font font{};
    bool fontLoaded = false;

    constexpr std::size_t CacheLimit = 4096;   // 타이머처럼 매 프레임 바뀌는 문자열 때문에 상한을 둔다.

    void UnloadLanguageFont()
    {
        if (fontLoaded)
        {
            UnloadFont(font);
            fontLoaded = false;
        }
    }

    // shortcut: 한글 음절 11172자 전부를 32px 아틀라스로 굽는다(메모리 ~수십 MB). 느리면 사용하는 글자만 굽도록 바꾼다.
    bool LoadLanguageFont(const std::string& path)
    {
        std::vector<int> codepoints;
        for (int c = 32; c <= 126; ++c) codepoints.push_back(c);
        for (int c = 0x3131; c <= 0x318E; ++c) codepoints.push_back(c);   // 한글 호환 자모
        for (int c = 0xAC00; c <= 0xD7A3; ++c) codepoints.push_back(c);   // 한글 음절
        for (const auto& [from, to] : phrases)
        {
            int count = 0;
            int* extra = LoadCodepoints(to.c_str(), &count);
            codepoints.insert(codepoints.end(), extra, extra + count);
            UnloadCodepoints(extra);
        }
        std::sort(codepoints.begin(), codepoints.end());
        codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());

        Font loaded = LoadFontEx(path.c_str(), 32, codepoints.data(), static_cast<int>(codepoints.size()));
        if (loaded.texture.id == 0 || loaded.texture.id == GetFontDefault().texture.id)
        {
            return false;
        }
        SetTextureFilter(loaded.texture, TEXTURE_FILTER_BILINEAR);
        UnloadLanguageFont();
        font = loaded;
        fontLoaded = true;
        return true;
    }

    void UseEnglish()
    {
        language = "en";
        phrases.clear();
        cache.clear();
        UnloadLanguageFont();
    }
}

bool SetLanguage(const std::string& dataDir, const std::string& code, std::string& error)
{
    if (code == "en")
    {
        UseEnglish();
        return true;
    }

    // 코드는 profile.txt에서 오므로 파일 이름으로 쓰기 전에 검사한다.
    const bool validCode = !code.empty() && std::all_of(code.begin(), code.end(), [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-'; });
    if (!validCode)
    {
        error = "invalid language code";
        UseEnglish();
        return false;
    }

    std::vector<std::pair<std::string, std::string>> loaded;
    if (!LoadStringPairs(dataDir + "lang/" + code + ".lua", loaded, error))
    {
        UseEnglish();
        return false;
    }
    PreparePhrases(loaded);
    phrases = std::move(loaded);
    cache.clear();

    // 언어 팩 옆에 <code>.ttf가 있으면 그것을, 없으면 Windows 기본 한글 폰트(맑은 고딕)를 쓴다.
    const std::string bundled = dataDir + "lang/" + code + ".ttf";
    const std::string fontPath = std::filesystem::exists(bundled) ? bundled : "C:/Windows/Fonts/malgun.ttf";
    if (!LoadLanguageFont(fontPath))
    {
        error = "cannot load font " + fontPath;
        UseEnglish();
        return false;
    }
    language = code;
    return true;
}

const std::string& CurrentLanguage()
{
    return language;
}

std::string Translate(const std::string& text)
{
    if (phrases.empty())
    {
        return text;
    }
    const auto found = cache.find(text);
    if (found != cache.end())
    {
        return found->second;
    }
    if (cache.size() >= CacheLimit)
    {
        cache.clear();
    }
    return cache.emplace(text, ReplacePhrases(text, phrases)).first->second;
}

void TextDrawRaw(const char* text, int x, int y, int size, Color color)
{
    if (!fontLoaded)
    {
        DrawText(text, x, y, size, color);
        return;
    }
    const float fontSize = static_cast<float>(size);
    DrawTextEx(font, text, { static_cast<float>(x), static_cast<float>(y) }, fontSize, 0.0f, color);
}

void TextDraw(const char* text, int x, int y, int size, Color color)
{
    TextDrawRaw(Translate(text).c_str(), x, y, size, color);
}

int TextWidth(const char* text, int size)
{
    if (!fontLoaded)
    {
        return MeasureText(text, size);
    }
    const float fontSize = static_cast<float>(size);
    return static_cast<int>(MeasureTextEx(font, Translate(text).c_str(), fontSize, 0.0f).x);
}

LanguageSession::~LanguageSession()
{
    UnloadLanguageFont();
}
