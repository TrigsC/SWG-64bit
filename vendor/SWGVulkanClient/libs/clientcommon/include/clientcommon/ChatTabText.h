#pragma once
#include <string>
#include <string_view>
namespace clientcommon {
// Installed getAppendableText normalization for supported text. Empty text
// after stripping a lone LF is safely ignored; the original str[0] edge is not
// copied. Capacity policy belongs to the containing history.
inline void appendTabText(std::u16string& history,std::u16string_view text){
    if(text.empty())return;
    if(text.back()==u'\n')text.remove_suffix(1);
    if(text.empty())return;
    if(!history.empty()&&history.back()!=u'\n'&&text.front()!=u'\n')history+=u'\n';
    history+=text;
}
}
