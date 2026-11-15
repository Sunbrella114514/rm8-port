// Traditional -> Simplified Chinese for displayed text ([A] the game's texts are Big5 / Traditional; the port can show
// Simplified). Table: scenes/zh_t2s.txt from OpenCC (tools/make_t2s.py), longest phrase match then single characters.
#pragma once
#include <string>

bool rm8ZhLoad(const std::string &tableFile);	// loads the table; conversion is on after a successful load
void rm8ZhEnable(bool on);
bool rm8ZhEnabled();
const std::string &rm8ZhConvert(const std::string &utf8);	// converted text (cached) or the input when off
