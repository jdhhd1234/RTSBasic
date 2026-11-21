#include "core/phrases.hpp"

#include <algorithm>
#include <cctype>

namespace
{
    // UTF-8 바이트(한글 등)도 단어 문자로 본다: 사용자가 쓴 한글 이름 안에서 치환하지 않도록.
    bool IsWordChar(char ch)
    {
        const unsigned char u = static_cast<unsigned char>(ch);
        return u >= 0x80 || std::isalnum(u) != 0 || ch == '_';
    }
}

void PreparePhrases(PhraseList& phrases)
{
    std::erase_if(phrases, [](const auto& pair) { return pair.first.empty(); });
    std::stable_sort(phrases.begin(), phrases.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
}

std::string ReplacePhrases(const std::string& text, const PhraseList& phrases)
{
    std::string out;
    std::size_t i = 0;
    while (i < text.size())
    {
        bool replaced = false;
        for (const auto& [from, to] : phrases)
        {
            if (text.compare(i, from.size(), from) != 0)
            {
                continue;
            }
            const std::size_t end = i + from.size();
            const bool startOk = !IsWordChar(from.front()) || i == 0 || !IsWordChar(text[i - 1]);
            const bool endOk = !IsWordChar(from.back()) || end == text.size() || !IsWordChar(text[end]);
            if (startOk && endOk)
            {
                out += to;
                i = end;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            out.push_back(text[i]);
            ++i;
        }
    }
    return out;
}
