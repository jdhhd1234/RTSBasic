#pragma once

#include <raylib.h>

#include <string>

// UI 언어. "en"(기본, raylib 기본 폰트) 또는 data/lang/<code>.lua 가 있는 언어.
// 화면 코드는 영어 문자열을 그대로 넘기고, TextDraw/TextWidth가 그릴 때 번역한다.

// 언어를 바꾼다. 실패하면(언어 팩·폰트 없음) 영어로 되돌리고 false와 error.
bool SetLanguage(const std::string& dataDir, const std::string& code, std::string& error);

const std::string& CurrentLanguage();

// 언어 팩의 구절을 단어 경계 기준으로 치환한다. 영어면 그대로.
std::string Translate(const std::string& text);

void TextDraw(const char* text, int x, int y, int size, Color color);
int TextWidth(const char* text, int size);

// 번역 없이 현재 폰트로만 그린다 (사용자가 입력 중인 이름 등).
void TextDrawRaw(const char* text, int x, int y, int size, Color color);

// 창이 닫히기 전에 언어 폰트를 해제한다. GameWindow 다음에 선언해야 먼저 소멸한다.
class LanguageSession
{
public:
    LanguageSession() = default;
    ~LanguageSession();

    LanguageSession(const LanguageSession&) = delete;
    LanguageSession& operator=(const LanguageSession&) = delete;
    LanguageSession(LanguageSession&&) = delete;
    LanguageSession& operator=(LanguageSession&&) = delete;
};
