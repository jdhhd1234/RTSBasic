#pragma once

#include <string>
#include <utility>
#include <vector>

// 번역 구절 목록: (영어 원문, 번역). PreparePhrases 후 ReplacePhrases에 넘긴다.
using PhraseList = std::vector<std::pair<std::string, std::string>>;

// 빈 키를 지우고 긴 구절이 먼저 오도록 정렬한다 ("Tank"보다 "Anti-Tank" 먼저).
void PreparePhrases(PhraseList& phrases);

// 구절을 왼쪽부터 가장 긴 것으로 치환한다. 영문자로 시작·끝나는 구절은 단어 경계에서만 맞춘다.
std::string ReplacePhrases(const std::string& text, const PhraseList& phrases);
