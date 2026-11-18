// Chimera - text layers (see text.h)
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "text.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cwctype>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <locale>
#endif

namespace fs = std::filesystem;

// Rust char::is_alphanumeric uses Unicode properties; the C locale does not classify non-ASCII
// letters. Ask Windows' Unicode tables on this platform and use the user's Unicode locale elsewhere.
static bool unicodeAlphanumeric(uint32_t cp) {
#ifdef _WIN32
    wchar_t units[2]; int count=1;
    if(cp<=0xffff) units[0]=(wchar_t)cp;
    else { cp-=0x10000; units[0]=(wchar_t)(0xd800+(cp>>10)); units[1]=(wchar_t)(0xdc00+(cp&0x3ff)); count=2; }
    WORD types[2]={};
    if(!GetStringTypeW(CT_CTYPE1,units,count,types))return false;
    for(int i=0;i<count;++i)if(types[i]&(C1_ALPHA|C1_DIGIT))return true;
    return false;
#else
    if(cp>static_cast<uint32_t>(WCHAR_MAX))return false;
    static const std::locale unicodeLocale("");
    return std::use_facet<std::ctype<wchar_t>>(unicodeLocale).is(std::ctype_base::alnum,(wchar_t)cp);
#endif
}
static bool unicodeLowercase(uint32_t cp){
#ifdef _WIN32
    wchar_t units[2];int count=1;
    if(cp<=0xffff)units[0]=(wchar_t)cp;
    else{cp-=0x10000;units[0]=(wchar_t)(0xd800+(cp>>10));units[1]=(wchar_t)(0xdc00+(cp&0x3ff));count=2;}
    WORD types[2]={};if(!GetStringTypeW(CT_CTYPE1,units,count,types))return false;
    for(int i=0;i<count;++i)if(types[i]&C1_LOWER)return true;
    return false;
#else
    return cp<=static_cast<uint32_t>(WCHAR_MAX)&&std::towlower((wchar_t)cp)!=(wchar_t)cp;
#endif
}

// after PhotoCraft (crates/text/src/layout/navigate.rs word_boundary): skip separators then
// consume the adjacent alphanumeric run, counting Unicode code points rather than UTF-8 bytes.
static uint32_t utf8CodePoint(const std::string& text,size_t i,size_t& bytes);
size_t textWordBoundary(const std::string& text, size_t index, bool forward) {
    std::vector<std::pair<size_t, uint32_t>> chars;
    for (size_t i=0; i<text.size();) {
        const size_t start=i; const unsigned char c=static_cast<unsigned char>(text[i++]); uint32_t cp=c;
        if (c>=0xF0 && i+2<text.size()) { cp=(c&7u)<<18; cp|=(static_cast<unsigned char>(text[i++])&63u)<<12; cp|=(static_cast<unsigned char>(text[i++])&63u)<<6; cp|=static_cast<unsigned char>(text[i++])&63u; }
        else if (c>=0xE0 && i+1<text.size()) { cp=(c&15u)<<12; cp|=(static_cast<unsigned char>(text[i++])&63u)<<6; cp|=static_cast<unsigned char>(text[i++])&63u; }
        else if (c>=0xC0 && i<text.size()) { cp=(c&31u)<<6; cp|=static_cast<unsigned char>(text[i++])&63u; }
        chars.push_back({start,cp});
    }
    size_t i=std::min(index,chars.size());
    if(forward) { while(i<chars.size()&&!unicodeAlphanumeric(chars[i].second))++i; while(i<chars.size()&&unicodeAlphanumeric(chars[i].second))++i; }
    else { while(i>0&&!unicodeAlphanumeric(chars[i-1].second))--i; while(i>0&&unicodeAlphanumeric(chars[i-1].second))--i; }
    return i;
}
std::pair<size_t,size_t> textWordSelection(const std::string& text,size_t caret){
    std::vector<uint32_t> chars;
    for(size_t i=0;i<text.size();){size_t bytes=1;const uint32_t cp=utf8CodePoint(text,i,bytes);chars.push_back(cp);i+=bytes;}
    size_t a=std::min(caret,chars.size()),b=a;
    while(a>0&&unicodeAlphanumeric(chars[a-1]))--a;
    while(b<chars.size()&&unicodeAlphanumeric(chars[b]))++b;
    return {a,b};
}

static uint32_t utf8CodePoint(const std::string& text,size_t i,size_t& bytes){
    const unsigned char c=(unsigned char)text[i];bytes=1;
    if((c&0xe0)==0xc0&&i+1<text.size()){bytes=2;return ((c&0x1f)<<6)|((unsigned char)text[i+1]&0x3f);}
    if((c&0xf0)==0xe0&&i+2<text.size()){bytes=3;return ((c&0x0f)<<12)|(((unsigned char)text[i+1]&0x3f)<<6)|((unsigned char)text[i+2]&0x3f);}
    if((c&0xf8)==0xf0&&i+3<text.size()){bytes=4;return ((c&7)<<18)|(((unsigned char)text[i+1]&0x3f)<<12)|(((unsigned char)text[i+2]&0x3f)<<6)|((unsigned char)text[i+3]&0x3f);}
    return c;
}
static size_t utf8Length(uint32_t cp){return cp<0x80?1:cp<0x800?2:cp<0x10000?3:4;}
// after PhotoCraft (crates/text/src/layout/shaping.rs push_cased): use the Unicode uppercase
// mapping only when it occupies the same UTF-8 byte length as the source character.
static std::vector<uint32_t> uppercaseCodePoints(uint32_t cp){
    const uint32_t original=cp;
    if(cp==0x00df)return {'S','S'};
#ifdef _WIN32
    wchar_t input[2];int inputLength=1;
    if(cp<=0xffff)input[0]=(wchar_t)cp;
    else{cp-=0x10000;input[0]=(wchar_t)(0xd800+(cp>>10));input[1]=(wchar_t)(0xdc00+(cp&0x3ff));inputLength=2;}
    wchar_t output[8]{};const int outputLength=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,input,inputLength,output,8,nullptr,nullptr,0);
    if(outputLength<=0)return {original};
    std::vector<uint32_t> result;
    for(int i=0;i<outputLength;++i){uint32_t value=(uint16_t)output[i];if(value>=0xd800&&value<=0xdbff&&i+1<outputLength){const uint32_t low=(uint16_t)output[++i];value=0x10000+((value-0xd800)<<10)+(low-0xdc00);}result.push_back(value);}
    return result.empty()?std::vector<uint32_t>{original}:result;
#else
    if(cp>static_cast<uint32_t>(WCHAR_MAX))return {cp};
    return {(uint32_t)std::towupper((wchar_t)cp)};
#endif
}
static bool quoteOpensAfter(uint32_t c){
    const bool ws=(c>=9&&c<=13)||c==32||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;
    return !c||ws||c=='('||c=='['||c=='{'||c=='-'||c==0x2013||c==0x2014;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/editing.rs smart_quotes): map one typed quote in its left context.
uint32_t textSmartQuoteCharacter(uint32_t previous,uint32_t character){
    if(character=='"')return quoteOpensAfter(previous)?0x201c:0x201d;
    if(character=='\'')return quoteOpensAfter(previous)?0x2018:0x2019;
    return character;
}
static std::string mapSmartQuotes(const std::string& text,uint32_t previous){
    std::string out; out.reserve(text.size());
    for(size_t i=0;i<text.size();) {
        const unsigned char c=(unsigned char)text[i];
        if(c=='"'||c=='\''){
            previous=textSmartQuoteCharacter(previous,c);
            if(c=='"')out+=previous==0x201c?"\xe2\x80\x9c":"\xe2\x80\x9d";
            else out+=previous==0x2018?"\xe2\x80\x98":"\xe2\x80\x99";
            ++i;continue;
        }
        size_t n=1;const uint32_t cp=utf8CodePoint(text,i,n);
        out.append(text,i,n);previous=cp;i+=n;
    }
    return out;
}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/keys.rs smart_quotes): quotes open after
// whitespace, brackets, or hyphens/dashes; elsewhere they close. Existing UTF-8 stays unchanged.
std::string textSmartQuotes(const std::string& text){return mapSmartQuotes(text,0);}
// after PhotoCraft (crates/ui-egui/src/tools/type_tool/editing.rs insert_typed): pasted text is not rewritten.
std::string textSmartQuotesTyped(const std::string& prefix,const std::string& typed){
    uint32_t previous=0;
    for(size_t i=0;i<prefix.size();){size_t n=1;previous=utf8CodePoint(prefix,i,n);i+=n;}
    return mapSmartQuotes(typed,previous);
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool/defaults.rs type_unit): points are the
// model unit, pixels follow document resolution, and millimetres remain physical units.
TextUnit textUnit(int units, float dpi) {
    const float safeDpi=std::isfinite(dpi)?std::max(1.0f,dpi):72.0f;
    if(units==1) return {safeDpi/72.0f,"px"};
    if(units==2) return {25.4f/72.0f,"mm"};
    return {1.0f,"pt"};
}

// after PhotoCraft (crates/ui-egui/src/tools/type_tool.rs note_recent_font): the newest selection
// leads, duplicates are removed, and a zero cap disables the history list.
void trimRecentFonts(std::vector<std::string>& recent, size_t cap) {
    if(recent.size()>cap) recent.resize(cap);
}
void noteRecentFont(std::vector<std::string>& recent, const std::string& font, size_t cap) {
    recent.erase(std::remove(recent.begin(),recent.end(),font),recent.end());
    if(cap==0) { recent.clear(); return; }
    recent.insert(recent.begin(),font);
    trimRecentFonts(recent,cap);
}

std::string Text::save() const {
    // Text settings and a path's editable Bezier knots live together in the project layer.
    auto hex = [](const std::string& s) { std::string o; char b[3]; for (unsigned char c : s) { snprintf(b, sizeof b, "%02x", c); o += b; } return o.empty() ? std::string("-") : o; };
    Shape pathShape; pathShape.kind = SHAPE_PATH; pathShape.path = path;
    const std::string pathData = pathText ? pathShape.save() : std::string();
    char b[256];
    snprintf(b, sizeof b, " %g %g %g %d %d %g %g %g %g %g %g %g %g %d %d %g", size, x, y, align, vertical ? 1 : 0, tracking, leading, boxWidth, boxHeight, scaleX, scaleY, rotation, pathStart, pathText ? 1 : 0, pathFlip ? 1 : 0, pathEnd);
    std::string runData, paraData;
    for (const TextRun& r : runs) {
        char q[256]; snprintf(q, sizeof q, "%zu,%g,%g,%g,%g,%g,%g,%d,%d,%d,%d,%d,%g,%d,", r.bytes, r.style.size, r.style.tracking, r.style.leading,
            r.style.baselineShift, r.style.horizontalScale, r.style.verticalScale, r.style.bold, r.style.italic, r.style.underline, r.style.strikethrough, r.style.kerningOff, r.style.kern, (int)r.style.caps);
        runData += q + hex(r.style.font) + ";";
    }
    for (const ParagraphRun& r : paragraphs) { char q[180]; snprintf(q, sizeof q, "%zu,%d,%g,%g,%g,%g,%g,%g;", r.bytes, r.style.align, r.style.firstIndent, r.style.startIndent, r.style.endIndent, r.style.spaceBefore, r.style.spaceAfter, r.style.autoLeading); paraData += q; }
    return "text5 " + hex(str) + " " + hex(font) + " " + hex(pathData) + b + " " + hex(runData) + " " + hex(paraData);
}
bool Text::load(const std::string& blob) {
    auto unhex = [](const std::string& h) { std::string o; if (h.empty() || h[0] == '-') return o; for (size_t i = 0; i + 1 < h.size(); i += 2) { char b[3] = {h[i], h[i + 1], 0}; o += (char)strtol(b, nullptr, 16); } return o; };
    std::string version, textHex, fontHex; int al = 0, v = 0;
    if (blob.rfind("text5 ", 0) == 0 || blob.rfind("text4 ", 0) == 0 || blob.rfind("text3 ", 0) == 0 || blob.rfind("text2 ", 0) == 0) {
        const bool v5 = blob.rfind("text5 ", 0) == 0;
        const bool v4 = v5 || blob.rfind("text4 ", 0) == 0;
        const bool v3 = v4 || blob.rfind("text3 ", 0) == 0;
        std::string pathHex; int onPath = 0, flipped = 0;
        std::istringstream in(blob);
        if (!(in >> version >> textHex >> fontHex >> pathHex >> size >> x >> y >> al >> v >> tracking >> leading >> boxWidth >> boxHeight >> scaleX >> scaleY >> rotation >> pathStart >> onPath >> flipped)) return false;
        if (!(in >> pathEnd)) pathEnd = -1;
        pathText = onPath != 0; pathFlip = flipped != 0; path.clear();
        const std::string pathData = unhex(pathHex);
        if (!pathData.empty()) { Shape shape; if (!shape.load(pathData) || shape.kind != SHAPE_PATH) return false; path = std::move(shape.path); }
        runs.clear(); paragraphs.clear();
        if (v3) {
            std::string rh, ph; if (!(in >> rh >> ph)) return false;
            std::istringstream ri(unhex(rh)); std::string item;
            while (std::getline(ri, item, ';')) { if (item.empty()) continue; std::istringstream fields(item); TextRun r; std::string fhex; int bo=0,it=0,un=0,st=0; char comma;
                int ko=0,caps=0;
                if (!(fields >> r.bytes >> comma >> r.style.size >> comma >> r.style.tracking >> comma >> r.style.leading >> comma >> r.style.baselineShift >> comma >> r.style.horizontalScale >> comma >> r.style.verticalScale >> comma >> bo >> comma >> it >> comma >> un >> comma >> st >> comma)) return false;
                if(v4 && !(fields >> ko >> comma >> r.style.kern >> comma))return false;
                if(v5 && !(fields >> caps >> comma))return false;
                r.style.kerningOff=ko!=0;
                r.style.caps=(TextCaps)std::clamp(caps,0,2);
                if (!(fields >> fhex)) return false; r.style.font = unhex(fhex); r.style.bold=bo!=0; r.style.italic=it!=0; r.style.underline=un!=0; r.style.strikethrough=st!=0; runs.push_back(std::move(r));
            }
            std::istringstream pi(unhex(ph));
            while (std::getline(pi, item, ';')) { if (item.empty()) continue; std::istringstream fields(item); ParagraphRun r; char comma;
                if (!(fields >> r.bytes >> comma >> r.style.align >> comma >> r.style.firstIndent >> comma >> r.style.startIndent >> comma >> r.style.endIndent >> comma >> r.style.spaceBefore >> comma >> r.style.spaceAfter >> comma >> r.style.autoLeading)) return false;
                paragraphs.push_back(r);
            }
        }
    } else {
        char s1[65536], s2[4096];
        const int fields = sscanf(blob.c_str(), "text1 %65535s %4095s %f %f %f %d %d %f %f %f %f %f %f %f", s1, s2, &size, &x, &y, &al, &v, &tracking, &leading, &boxWidth, &boxHeight, &scaleX, &scaleY, &rotation);
        if (fields != 9 && fields != 11 && fields != 14) return false;
        textHex = s1; fontHex = s2;
        if (fields == 9) boxWidth = boxHeight = 0;
        if (fields != 14) scaleX = scaleY = 1, rotation = 0;
        path.clear(); pathText = pathFlip = false; pathStart = 0; pathEnd = -1;
    }
    boxWidth = std::max(0.0f, boxWidth); boxHeight = std::max(0.0f, boxHeight);
    scaleX = std::clamp(scaleX, 0.01f, 100.0f); scaleY = std::clamp(scaleY, 0.01f, 100.0f);
    if (!std::isfinite(rotation)) rotation = 0;
    if (!std::isfinite(pathStart)) pathStart = 0;
    if (!std::isfinite(pathEnd)) pathEnd = -1;
    str = unhex(textHex); font = unhex(fontHex); align = std::clamp(al, 0, 2); vertical = v != 0;
    return true;
}

// after PhotoCraft (crates/doc/src/lib.rs TextLayer::char_runs): runs always cover the text and
// their boundaries are UTF-8 character boundaries; any missing tail inherits the layer style.
std::vector<TextRun> Text::normalizedRuns() const {
    TextStyle base; base.font=font; base.size=size; base.tracking=tracking; base.leading=leading;
    std::vector<TextRun> out;
    auto boundary = [&](size_t n) { n=std::min(n,str.size()); while (n<str.size() && n && (static_cast<unsigned char>(str[n])&0xc0)==0x80) ++n; return n; };
    size_t raw=0, at=0;
    if(str.empty()) return {{0,runs.empty()?base:runs.front().style}};
    for (const TextRun& run : runs) {
        const size_t end=boundary(run.bytes>str.size()-std::min(raw,str.size())?str.size():raw+run.bytes);
        if(end>at) out.push_back({end-at,run.style});
        at=end;
        if(raw<str.size()) raw=std::min(str.size(),raw+std::min(run.bytes,str.size()-raw));
        if(raw>=str.size()) break;
    }
    if(at<str.size()) { const TextStyle tail=out.empty()?(runs.empty()?base:runs.back().style):out.back().style; if(!out.empty()&&out.back().style==tail) out.back().bytes+=str.size()-at; else out.push_back({str.size()-at,tail}); }
    return out;
}

// after PhotoCraft (crates/doc/src/lib.rs TextLayer::char_runs): byte run lengths map to the
// character indices used by the editor and layout, not one entry per UTF-8 byte.
std::vector<TextStyle> Text::characterStyles() const {
    std::vector<TextStyle> out;
    size_t at = 0;
    for (const TextRun& run : normalizedRuns()) {
        const size_t end = std::min(str.size(), at + run.bytes);
        while (at < end) {
            size_t length = 0;
            utf8CodePoint(str, at, length);
            if (!length || at + length > end) break;
            out.push_back(run.style);
            at += length;
        }
    }
    return out;
}

// after PhotoCraft (crates/doc/src/lib.rs TextLayer::paragraph_runs): paragraph runs cover text bytes.
std::vector<ParagraphRun> Text::normalizedParagraphs() const {
    std::vector<ParagraphRun> out; size_t raw=0,at=0;
    if(str.empty()) return {{0,paragraphs.empty()?ParagraphStyle{}:paragraphs.front().style}};
    for(const ParagraphRun& run:paragraphs) { const size_t limit=run.bytes>str.size()-std::min(raw,str.size())?str.size():raw+run.bytes; size_t end=limit; while(end<str.size()&&end&&(static_cast<unsigned char>(str[end])&0xc0)==0x80)++end; if(end>at)out.push_back({end-at,run.style}); at=end; raw=limit; if(raw>=str.size())break; }
    if(at<str.size()) { const ParagraphStyle tail=out.empty()?(paragraphs.empty()?ParagraphStyle{}:paragraphs.back().style):out.back().style; if(!out.empty()&&out.back().style.align==tail.align&&out.back().style.autoLeading==tail.autoLeading&&out.back().style.firstIndent==tail.firstIndent&&out.back().style.startIndent==tail.startIndent&&out.back().style.endIndent==tail.endIndent&&out.back().style.spaceBefore==tail.spaceBefore&&out.back().style.spaceAfter==tail.spaceAfter)out.back().bytes+=str.size()-at; else out.push_back({str.size()-at,tail}); }
    // after PhotoCraft (crates/doc/src/lib.rs TextLayer::paragraph_runs): a trailing newline
    // has a final empty paragraph marker whose style may be stored in a zero-byte final run.
    if(!paragraphs.empty() && !str.empty() && (str.back()=='\n'||str.back()=='\r') && paragraphs.back().bytes<=1) {
        size_t preceding=0;
        for(size_t i=0;i+1<paragraphs.size();++i) {
            if(paragraphs[i].bytes>str.size()-std::min(preceding,str.size())) { preceding=str.size()+1; break; }
            preceding+=paragraphs[i].bytes;
        }
        if(preceding==str.size()) out.push_back({0,paragraphs.back().style});
    }
    return out;
}

// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs replace_text): insertions inherit the
// character before the caret, replacements inherit the first removed character, and paragraph
// insertion at a paragraph start belongs to that following paragraph.
bool Text::replaceText(size_t startByte,size_t endByte,const std::string& inserted){
    startByte=std::min(startByte,str.size());endByte=std::clamp(endByte,startByte,str.size());
    while(startByte<str.size()&&startByte&&(static_cast<unsigned char>(str[startByte])&0xc0)==0x80)++startByte;
    while(endByte<str.size()&&endByte&&(static_cast<unsigned char>(str[endByte])&0xc0)==0x80)++endByte;
    std::string replacement;replacement.reserve(inserted.size());
    for(size_t i=0;i<inserted.size();++i){if(inserted[i]=='\r'){if(i+1<inserted.size()&&inserted[i+1]=='\n')++i;replacement.push_back('\n');}else replacement.push_back(inserted[i]);}
    const TextStyle firstChar=normalizedRuns().front().style;
    const auto oldParas=normalizedParagraphs();const ParagraphStyle firstPara=oldParas.empty()?ParagraphStyle{}:oldParas.front().style;
    const bool hadTrailingEmpty=!oldParas.empty()&&oldParas.back().bytes==0&&(!str.empty()&&(str.back()=='\n'||str.back()=='\r'));
    auto charRuns=normalizedRuns();
    auto paraRuns=oldParas;
    auto adjust=[&](auto runs,size_t a,size_t b,size_t add,bool paragraph,const auto& fallback,auto equal){
        using Run=typename decltype(runs)::value_type;
        std::vector<Run> out;size_t at=0;
        auto push=[&](size_t len,const auto& style){if(!len){if(out.empty()&&add==0)out.push_back({0,style});return;}
            if(!out.empty()&&equal(out.back().style,style))out.back().bytes+=len;else out.push_back({len,style});};
        auto startsHere=[&]()->size_t{size_t s=0;for(size_t i=0;i<runs.size();++i){if(s==a&&runs[i].bytes>0)return i;s+=runs[i].bytes;}return runs.size();};
        size_t host=runs.empty()?0:runs.size()-1;
        if(paragraph&&a==b&&hadTrailingEmpty&&!runs.empty()&&runs.back().bytes==0&&a==str.size())host=runs.size()-1;
        else if(paragraph&&a==b&&a>0){const size_t next=startsHere();if(next<runs.size())host=next;else host=runs.empty()?0:runs.size()-1;}
        else if(!runs.empty()){
            for(size_t i=0;i<runs.size();++i){const size_t rs=at,re=at+runs[i].bytes;at=re;
                if(a==0?(re>0||rs==0):(a<b?(a>=rs&&a<re):(a>rs&&a<=re))){host=i;break;}}
        }
        at=0;
        for(size_t i=0;i<runs.size();++i){const size_t rs=at,re=at+runs[i].bytes;at=re;
            const size_t keep=(std::clamp(a,rs,re)-rs)+(re-std::clamp(b,rs,re));
            const size_t len=keep+(i==host?add:0);push(len,runs[i].style);
        }
        if(out.empty())out.push_back({0,runs.empty()?fallback:runs.front().style});
        return out;
    };
    auto sameChar=[](const TextStyle& a,const TextStyle& b){return a==b;};
    auto samePara=[](const ParagraphStyle& a,const ParagraphStyle& b){return a.align==b.align&&a.firstIndent==b.firstIndent&&a.startIndent==b.startIndent&&a.endIndent==b.endIndent&&a.spaceBefore==b.spaceBefore&&a.spaceAfter==b.spaceAfter&&a.autoLeading==b.autoLeading;};
    const size_t add=replacement.size();
    auto nextChars=adjust(charRuns,startByte,endByte,add,false,firstChar,sameChar);
    auto nextParas=adjust(paraRuns,startByte,endByte,add,true,firstPara,samePara);
    str.replace(startByte,endByte-startByte,replacement);
    runs=std::move(nextChars);paragraphs=std::move(nextParas);
    if(runs.empty())runs.push_back({0,firstChar});
    if(paragraphs.empty())paragraphs.push_back({0,firstPara});
    if(str.empty()){runs={{0,runs.front().style}};paragraphs={{0,paragraphs.front().style}};}
    else if((str.back()=='\n'||str.back()=='\r')&&(paragraphs.empty()||paragraphs.back().bytes!=0))paragraphs.push_back({0,paragraphs.empty()?firstPara:paragraphs.back().style});
    syncSummary();return true;
}

// after PhotoCraft (crates/doc/src/lib.rs TextLayer::sync_summary): simple layer fields mirror
// the first resolved character run so existing options controls show the start style.
void Text::syncSummary() {
    const auto normalized=normalizedRuns();
    if(normalized.empty()) return;
    font=normalized.front().style.font;
    size=normalized.front().style.size;
    tracking=normalized.front().style.tracking;
    leading=normalized.front().style.leading;
}

const std::vector<FontFile>& systemFonts() {
    static std::vector<FontFile> list;
    static bool done = false;
    if (done) return list;
    done = true;
    std::vector<fs::path> dirs;
    if (const char* w = std::getenv("WINDIR")) dirs.push_back(fs::path(w) / "Fonts");
    if (const char* l = std::getenv("LOCALAPPDATA")) dirs.push_back(fs::path(l) / "Microsoft" / "Windows" / "Fonts");
    dirs.push_back("/usr/share/fonts"); dirs.push_back("/usr/local/share/fonts");
    std::error_code ec;
    for (const auto& d : dirs) {
        for (auto it = fs::recursive_directory_iterator(d, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::string ext = it->path().extension().u8string(); for (auto& c : ext) c = (char)tolower((unsigned char)c);
            if (ext != ".ttf" && ext != ".otf" && ext != ".ttc") continue;
            // its family and style names from the file ("Segoe UI Bold"), else the file's name
            std::ifstream f(it->path(), std::ios::binary);
            std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)), {});
            std::string name;
            stbtt_fontinfo fi;
            const int off = stbtt_GetFontOffsetForIndex(data.data(), 0);
            if (off >= 0 && stbtt_InitFont(&fi, data.data(), off)) {
                auto get = [&](int id) {
                    int len = 0;
                    const char* n = stbtt_GetFontNameString(&fi, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, id);
                    std::string o;
                    for (int i = 0; n && i + 1 < len; i += 2) { const int c = ((unsigned char)n[i] << 8) | (unsigned char)n[i + 1]; o += c < 128 ? (char)c : '?'; }
                    return o;
                };
                const std::string fam = get(1), sub = get(2);
                name = fam.empty() ? "" : (sub.empty() || sub == "Regular" ? fam : fam + " " + sub);
            }
            if (name.empty()) name = it->path().stem().u8string();
            list.push_back({it->path().u8string(), name});
        }
        ec.clear();
    }
    std::sort(list.begin(), list.end(), [](const FontFile& a, const FontFile& b) { return a.name < b.name; });
    list.erase(std::unique(list.begin(), list.end(), [](const FontFile& a, const FontFile& b) { return a.name == b.name; }), list.end());
    return list;
}
std::string defaultFont() {
    for (const char* f : {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}) {
        std::error_code ec;
        if (fs::exists(f, ec)) return f;
    }
    const auto& all = systemFonts();
    return all.empty() ? std::string() : all.front().path;
}

// loaded fonts, kept
struct Loaded { std::vector<unsigned char> data; stbtt_fontinfo info; bool ok = false; };
static Loaded* fontFor(const std::string& path) {
    static std::map<std::string, Loaded> cache;
    static std::mutex lock;
    std::lock_guard<std::mutex> g(lock);
    auto it = cache.find(path);
    if (it != cache.end()) return it->second.ok ? &it->second : nullptr;
    Loaded& L = cache[path];
    std::ifstream f(fs::u8path(path), std::ios::binary);
    L.data.assign(std::istreambuf_iterator<char>(f), {});
    const int off = L.data.empty() ? -1 : stbtt_GetFontOffsetForIndex(L.data.data(), 0);
    L.ok = off >= 0 && stbtt_InitFont(&L.info, L.data.data(), off);
    return L.ok ? &L : nullptr;
}
// The OS/2 table stores strikeout size and position in font units. stb exposes the matching
// underline metrics from the post table, so both decorations use the font's own measurements.
static std::pair<int,int> strikeMetrics(Loaded& loaded) {
    const stbtt_uint32 table=stbtt__find_table(loaded.info.data,loaded.info.fontstart,"OS/2");
    if(!table||table+30>loaded.data.size())return {0,0};
    return {ttSHORT(loaded.data.data()+table+28),ttUSHORT(loaded.data.data()+table+26)};
}

// Keep the source character index with each glyph; wrapped lines must still resolve the
// character style that was attached before layout.
struct TextGlyph { int codepoint = 0; size_t character = 0; float sizeScale = 1.0f; };
static const TextStyle& styleForCharacter(const std::vector<TextStyle>& styles, size_t character, const TextStyle& fallback) {
    return character < styles.size() ? styles[character] : fallback;
}
static Loaded* fontForStyle(const TextStyle& style, const std::string& fallback) {
    const std::string& font = style.font.empty() ? fallback : style.font;
    return fontFor(font.empty() ? defaultFont() : font);
}
static float glyphAdvance(const TextGlyph& glyph, const TextGlyph* next, const std::vector<TextStyle>& styles,
                          const TextStyle& fallback, const std::string& fallbackFont) {
    const TextStyle& style = styleForCharacter(styles, glyph.character, fallback);
    Loaded* loaded = fontForStyle(style, fallbackFont);
    if (!loaded) return 0;
    const float size = std::max(1.0f, style.size*glyph.sizeScale);
    const float scale = stbtt_ScaleForPixelHeight(&loaded->info, size);
    int advance = 0, bearing = 0;
    stbtt_GetCodepointHMetrics(&loaded->info, glyph.codepoint, &advance, &bearing);
    float kern = next ? style.kern / 1000.0f * size : 0.0f;
    if (next && !style.kerningOff) {
        const TextStyle& nextStyle = styleForCharacter(styles, next->character, fallback);
        const std::string& font = style.font.empty() ? fallbackFont : style.font;
        const std::string& nextFont = nextStyle.font.empty() ? fallbackFont : nextStyle.font;
        if (font == nextFont && style.size == nextStyle.size)
            kern += stbtt_GetCodepointKernAdvance(&loaded->info, glyph.codepoint, next->codepoint) * scale;
    }
    const float horizontalScale=std::isfinite(style.horizontalScale)&&style.horizontalScale>0?style.horizontalScale:1.0f;
    return (advance * scale + kern + style.tracking / 1000.0f * size) * horizontalScale;
}
static std::vector<std::vector<TextGlyph>> linesOf(const std::string& s,const std::vector<TextStyle>& styles) {   // UTF-8 to styled glyphs, by line
    std::vector<std::vector<TextGlyph>> lines(1);
    size_t character = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        int cp = c, n = 1;
        if (c >= 0xF0) { cp = c & 7; n = 4; } else if (c >= 0xE0) { cp = c & 15; n = 3; } else if (c >= 0xC0) { cp = c & 31; n = 2; }
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 63);
        i += n;
        if (cp == '\n') lines.emplace_back();
        else if (cp != '\r') {
            static const TextStyle defaultStyle{};
            const TextStyle& style=styleForCharacter(styles,character,defaultStyle);
            if(style.caps==TextCaps::All||style.caps==TextCaps::Small){
                const auto upper=uppercaseCodePoints((uint32_t)cp);size_t mappedBytes=0;for(uint32_t value:upper)mappedBytes+=utf8Length(value);
                const bool isSmallCaps=style.caps==TextCaps::Small&&unicodeLowercase((uint32_t)cp);
                if(mappedBytes==(size_t)n){for(uint32_t value:upper)lines.back().push_back({(int)value,character,isSmallCaps?.7f:1.0f});}
                else lines.back().push_back({cp,character});
            }else lines.back().push_back({cp,character});
        }
        ++character;
    }
    return lines;
}

// after PhotoCraft (crates/text paragraph layout): wrap at whitespace without splitting a word.
static void wrapLines(std::vector<std::vector<TextGlyph>>& lines, const std::vector<TextStyle>& styles,
                      const TextStyle& fallback, const std::string& fallbackFont, float width) {
    if (width <= 0) return;
    std::vector<std::vector<TextGlyph>> wrapped;
    for (const auto& source : lines) {
        std::vector<TextGlyph> line;
        size_t at = 0;
        while (at < source.size()) {
            const size_t separator = at;
            while (at < source.size() && (source[at].codepoint == ' ' || source[at].codepoint == '\t')) ++at;
            if (at == source.size()) break;
            size_t end = at;
            while (end < source.size() && source[end].codepoint != ' ' && source[end].codepoint != '\t') ++end;
            std::vector<TextGlyph> word(source.begin() + at, source.begin() + end);
            float used = 0;
            for (size_t i = 0; i < line.size(); ++i) used += glyphAdvance(line[i], i + 1 < line.size() ? &line[i + 1] : (word.empty() ? nullptr : &word[0]), styles, fallback, fallbackFont);
            float wordWidth = 0;
            for (size_t i = 0; i < word.size(); ++i) wordWidth += glyphAdvance(word[i], i + 1 < word.size() ? &word[i + 1] : nullptr, styles, fallback, fallbackFont);
            TextGlyph spaceGlyph{' ', source[separator].character};
            const float space = line.empty() ? 0 : glyphAdvance(spaceGlyph, word.empty() ? nullptr : &word[0], styles, fallback, fallbackFont);
            if (!line.empty() && used + space + wordWidth > width) { wrapped.push_back(std::move(line)); line.clear(); }
            else if (!line.empty()) line.push_back({' ', source[separator].character});
            line.insert(line.end(), word.begin(), word.end());
            at = end;
        }
        wrapped.push_back(std::move(line));
    }
    lines = std::move(wrapped);
}

// after PhotoCraft (crates/text/src/warp.rs PathFrame): arc-length samples keep glyphs upright
// to the path tangent and make closed paths wrap their distance at the seam.
struct TextPathMetric {
    std::vector<float> points, cumulative;
    bool closed = false;
    float length = 0;
    bool sample(float at, float& x, float& y, float& tx, float& ty) const {
        if (points.size() < 4 || length <= 0) return false;
        if (closed) { at = std::fmod(at, length); if (at < 0) at += length; }
        else if (at < 0 || at > length) return false;
        const size_t n = points.size() / 2;
        size_t seg = 0;
        if (!closed && at >= length) seg = n - 2;
        else while (seg + 1 < cumulative.size() && cumulative[seg + 1] < at) ++seg;
        const size_t next = closed ? (seg + 1) % n : seg + 1;
        const float a = cumulative[seg], b = seg + 1 < cumulative.size() ? cumulative[seg + 1] : length;
        const float dx = points[next * 2] - points[seg * 2], dy = points[next * 2 + 1] - points[seg * 2 + 1];
        const float len = std::hypot(dx, dy); if (len <= 1e-6f) return false;
        const float f = std::clamp((at - a) / std::max(1e-6f, b - a), 0.0f, 1.0f);
        x = points[seg * 2] + dx * f; y = points[seg * 2 + 1] + dy * f; tx = dx / len; ty = dy / len;
        return true;
    }
};

static TextPathMetric pathMetric(const Text& t) {
    TextPathMetric metric;
    const auto flattened = flattenPath(t.path);
    size_t source = 0;
    while (source < t.path.size() && t.path[source].knots.empty()) ++source;
    if (source >= t.path.size() || flattened.empty()) return metric;
    metric.points = flattened.front(); metric.closed = t.path[source].closed;
    if (metric.points.size() < 4) return metric;
    if (t.pathFlip) {
        std::vector<float> reverse;
        for (size_t i = metric.points.size(); i >= 2; i -= 2) { reverse.push_back(metric.points[i - 2]); reverse.push_back(metric.points[i - 1]); }
        metric.points = std::move(reverse);
    }
    const size_t n = metric.points.size() / 2;
    metric.cumulative.resize(n);
    for (size_t i = 1; i < n; ++i) {
        metric.length += std::hypot(metric.points[i * 2] - metric.points[(i - 1) * 2], metric.points[i * 2 + 1] - metric.points[(i - 1) * 2 + 1]);
        metric.cumulative[i] = metric.length;
    }
    if (metric.closed) metric.length += std::hypot(metric.points[0] - metric.points[(n - 1) * 2], metric.points[1] - metric.points[(n - 1) * 2 + 1]);
    return metric;
}

float textAdvance(const Text& t) {
    const auto styles = t.characterStyles();
    TextStyle fallback; fallback.font=t.font; fallback.size=t.size; fallback.tracking=t.tracking; fallback.leading=t.leading;
    const auto lines = linesOf(t.str,styles);
    if (lines.empty() || t.vertical) return 0;
    const auto& line = lines.front(); float width = 0;
    for (size_t i = 0; i < line.size(); ++i)
        width += glyphAdvance(line[i], i + 1 < line.size() ? &line[i + 1] : nullptr, styles, fallback, t.font);
    return std::max(0.0f, width);
}

bool textCoverage(const Text& t, int w, int h, std::vector<float>& cov, float box[4], float dpi) {
    cov.assign((size_t)w * h, 0.0f);
    box[0] = t.x; box[1] = t.y; box[2] = t.x; box[3] = t.y;
    Loaded* F = fontFor(t.font.empty() ? defaultFont() : t.font);
    if (!F) return false;
    const stbtt_fontinfo* fi = &F->info;
    const float scale = stbtt_ScaleForPixelHeight(fi, std::max(1.0f, t.size));
    const float transformX = std::clamp(std::isfinite(t.scaleX) ? t.scaleX : 1.0f, 0.01f, 100.0f);
    const float transformY = std::clamp(std::isfinite(t.scaleY) ? t.scaleY : 1.0f, 0.01f, 100.0f);
    const float rotation = std::isfinite(t.rotation) ? t.rotation : 0.0f;
    const float pointsToPixels=std::isfinite(dpi)&&dpi>0?dpi/72.0f:1.0f;
    int asc, desc, gap; stbtt_GetFontVMetrics(fi, &asc, &desc, &gap);
    const auto styles = t.characterStyles();
    TextStyle fallback; fallback.font=t.font; fallback.size=t.size; fallback.tracking=t.tracking; fallback.leading=t.leading;
    auto lines = linesOf(t.str,styles);
    if (!t.vertical) wrapLines(lines, styles, fallback, t.font, t.boxWidth);
    const TextPathMetric path = t.pathText ? pathMetric(t) : TextPathMetric();
    const bool pathOn = t.pathText && !t.vertical && path.length > 0;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    auto blit = [&](int cp, float gx, float gy, const stbtt_fontinfo* glyphFi, float glyphScale, float fontSize,
                    size_t characterIndex,float advanceWidth,int strikePosition,int strikeSize,
                    float horizontalScale, float verticalScale, float baselineShift, bool shiftAlongX,
                    bool fauxBold, bool fauxItalic, bool underline, bool strikethrough,
                    float pathCenter = std::numeric_limits<float>::quiet_NaN()) {   // one glyph, its origin at (gx, gy) (the baseline)
        const float sx = gx - std::floor(gx);
        int x0, y0, x1, y1;
        const float rasterScaleX=glyphScale*horizontalScale,rasterScaleY=glyphScale*verticalScale;
        stbtt_GetCodepointBitmapBoxSubpixel(glyphFi, cp, rasterScaleX, rasterScaleY, sx, 0, &x0, &y0, &x1, &y1);
        const int bw = x1 - x0, bh = y1 - y0;
        if (bw <= 0 || bh <= 0) return;
        std::vector<unsigned char> bm((size_t)bw * bh);
        stbtt_MakeCodepointBitmapSubpixel(glyphFi, bm.data(), bw, bh, bw, rasterScaleX, rasterScaleY, sx, 0, cp);
        int italicLeft=0,italicRight=0;
        const float shear=fauxItalic?std::tan(12.0f*0.017453292519943295f):0.0f;
        if(fauxItalic){
            const float a=-shear*y0,b=-shear*(y0+bh);
            italicLeft=(int)std::ceil(std::max(0.0f,-std::min(a,b)));
            italicRight=(int)std::ceil(std::max(0.0f,std::max(a,b)));
        }
        std::vector<unsigned char> sheared;
        int shearedW=bw+italicLeft+italicRight;
        if(fauxItalic){
            sheared.assign((size_t)shearedW*bh,0);
            for(int yy=0;yy<bh;++yy){
                const int move=italicLeft+(int)std::lround(-shear*(y0+yy));
                for(int xx=0;xx<bw;++xx){const unsigned char a=bm[(size_t)yy*bw+xx];unsigned char& b=sheared[(size_t)yy*shearedW+move+xx];b=std::max(a,b);}
            }
            bm=std::move(sheared);
        }
        const float baseBoldRadius=std::max(fontSize/72.0f,std::min(fontSize/40.0f,.45f));
        const float radiusX=fauxBold?baseBoldRadius*horizontalScale:0.0f;
        const float radiusY=fauxBold?baseBoldRadius*verticalScale:0.0f;
        const int boldPadX=fauxBold?(int)std::ceil(radiusX):0;
        const int boldPadY=fauxBold?(int)std::ceil(radiusY):0;
        if(fauxBold){
            const int sourceW=shearedW,sourceH=bh,outW=sourceW+2*boldPadX,outH=sourceH+2*boldPadY;
            std::vector<unsigned char> bold((size_t)outW*outH,0);
            auto sample=[&](float x,float y){
                const int x0=(int)std::floor(x),y0=(int)std::floor(y);const float fx=x-x0,fy=y-y0;
                auto at=[&](int px,int py){return px<0||px>=sourceW||py<0||py>=sourceH?0.0f:(float)bm[(size_t)py*sourceW+px];};
                const float top=at(x0,y0)*(1-fx)+at(x0+1,y0)*fx,bottom=at(x0,y0+1)*(1-fx)+at(x0+1,y0+1)*fx;
                return top*(1-fy)+bottom*fy;
            };
            for(int yy=0;yy<outH;++yy)for(int xx=0;xx<outW;++xx){
                float alpha=sample((float)(xx-boldPadX),(float)(yy-boldPadY));
                for(int ring=1;ring<=4;++ring){
                    const float radius=ring*.25f;
                    for(int angle=0;angle<24;++angle){
                        const float theta=angle*6.28318530718f/24.0f;
                        const float dx=std::cos(theta)*radiusX*radius,dy=std::sin(theta)*radiusY*radius;
                        alpha=std::max(alpha,sample(xx-boldPadX-dx,yy-boldPadY-dy));
                    }
                }
                bold[(size_t)yy*outW+xx]=(unsigned char)std::clamp((int)std::lround(alpha),0,255);
            }
            bm=std::move(bold);
        }
        const int drawW=bw+italicLeft+italicRight+2*boldPadX,drawH=bh+2*boldPadY;
        if(shiftAlongX)gx+=baselineShift;else if(!pathOn)gy-=baselineShift;
        const int ox = (int)std::floor(gx) + x0-italicLeft-boldPadX, oy = (int)std::round(gy) + y0-boldPadY;
        const float angle = rotation * 0.017453292519943295f, cs = std::cos(angle), sn = std::sin(angle);
        auto transform = [&](float x, float y) {
            const float dx = (x - t.x) * transformX, dy = (y - t.y) * transformY;
            return std::pair<float, float>(t.x + cs * dx - sn * dy, t.y + sn * dx + cs * dy);
        };
        auto paintQuad = [&](const std::array<std::pair<float,float>,4>& p) {
            float left=p[0].first,right=left,top=p[0].second,bottom=top;
            for(const auto& q:p){left=std::min(left,q.first);right=std::max(right,q.first);top=std::min(top,q.second);bottom=std::max(bottom,q.second);}
            minX=std::min(minX,left);maxX=std::max(maxX,right);minY=std::min(minY,top);maxY=std::max(maxY,bottom);
            const auto e1=std::pair<float,float>{p[1].first-p[0].first,p[1].second-p[0].second};
            const auto e2=std::pair<float,float>{p[3].first-p[0].first,p[3].second-p[0].second};
            const float det=e1.first*e2.second-e1.second*e2.first;if(std::fabs(det)<1e-8f)return;
            for(int py=std::max(0,(int)std::floor(top));py<std::min(h,(int)std::ceil(bottom));++py)
                for(int px=std::max(0,(int)std::floor(left));px<std::min(w,(int)std::ceil(right));++px){
                    int inside=0;
                    for(float oySample:{.125f,.375f,.625f,.875f})for(float oxSample:{.125f,.375f,.625f,.875f}){
                        const float qx=px+oxSample-p[0].first,qy=py+oySample-p[0].second;
                        const float u=(qx*e2.second-qy*e2.first)/det,v=(e1.first*qy-e1.second*qx)/det;
                        if(u>=0&&u<=1&&v>=0&&v<=1)++inside;
                    }
                    if(inside){float& out=cov[(size_t)py*w+px];out=std::max(out,inside/16.0f);}
                }
        };
        auto decoration = [&](float offset, float thickness) {
            const float down=offset-(pathOn?baselineShift:0.0f);
            std::array<std::pair<float,float>,4> quad;
            if(pathOn&&std::isfinite(pathCenter)){
                float cx=0,cy=0,tx=0,ty=0;
                if(!path.sample(t.pathStart+pathCenter,cx,cy,tx,ty))return;
                const float nx=-ty,ny=tx,half=advanceWidth*.5f;
                auto point=[&](float along,float localDown){return transform(cx+tx*along+nx*localDown,cy+ty*along+ny*localDown);};
                quad={point(-half,down),point(half,down),point(half,down+thickness),point(-half,down+thickness)};
            }else{
                const float y=gy+down;
                quad={transform(gx,y),transform(gx+advanceWidth,y),transform(gx+advanceWidth,y+thickness),transform(gx,y+thickness)};
            }
            paintQuad(quad);
        };
        if (pathOn && std::isfinite(pathCenter)) {
            const float naturalSpan = path.closed ? path.length : std::max(0.0f, path.length - t.pathStart);
            const float allowedSpan = t.pathEnd >= 0 ? std::min(naturalSpan, t.pathEnd - t.pathStart) : naturalSpan;
            if (pathCenter < 0 || pathCenter > allowedSpan) return;
            float cx = 0, cy = 0, tx = 0, ty = 0;
            if (!path.sample(t.pathStart + pathCenter, cx, cy, tx, ty)) return;
            const float nx = -ty, ny = tx, centerX = (ox + ox + drawW) * 0.5f;
            float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
            for (int yy : {0, drawH}) for (int xx : {0, drawW}) {
                const float along = ox + xx - centerX, down = oy + yy - gy - (shiftAlongX?0:baselineShift);
                const auto transformed = transform(cx + tx * along + nx * down, cy + ty * along + ny * down);
                const float px = transformed.first, py = transformed.second;
                left = std::min(left, px); top = std::min(top, py); right = std::max(right, px); bottom = std::max(bottom, py);
            }
            minX = std::min(minX, left); minY = std::min(minY, top); maxX = std::max(maxX, right); maxY = std::max(maxY, bottom);
            for (int yy = 0; yy < drawH; ++yy) for (int xx = 0; xx < drawW; ++xx) {
                const float along = ox + xx + 0.5f - centerX, down = oy + yy + 0.5f - gy - (shiftAlongX?0:baselineShift);
                const auto transformed = transform(cx + tx * along + nx * down, cy + ty * along + ny * down);
                const int px = (int)std::floor(transformed.first), py = (int)std::floor(transformed.second);
                if (px < 0 || px >= w || py < 0 || py >= h) continue;
                const unsigned char alpha = bm[(size_t)yy * drawW + xx]; if (!alpha) continue;
                float& out = cov[(size_t)py * w + px]; out = std::min(1.0f, out + alpha / 255.0f);
            }
            if(!shiftAlongX&&(underline||strikethrough)){
                int underlinePos=0,underlineSize=0;
                const bool hasUnderline=stbtt_GetFontUnderlineMetrics(glyphFi,&underlinePos,&underlineSize)!=0;
                const float underlineScale=glyphScale*verticalScale;
                if(underline&&hasUnderline)decoration(-underlinePos*underlineScale,std::max(1.0f,underlineSize*underlineScale));
                if(strikethrough&&strikeSize>0)decoration(-strikePosition*underlineScale-strikeSize*underlineScale*.5f,strikeSize*underlineScale);
            }
            return;
        }
        const auto corners = {transform((float)ox, (float)oy), transform((float)(ox + drawW), (float)oy),
                              transform((float)(ox + drawW), (float)(oy + drawH)), transform((float)ox, (float)(oy + drawH))};
        float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
        for (const auto& p : corners) { left = std::min(left, p.first); top = std::min(top, p.second); right = std::max(right, p.first); bottom = std::max(bottom, p.second); }
        minX = std::min(minX, left); minY = std::min(minY, top); maxX = std::max(maxX, right); maxY = std::max(maxY, bottom);
        const int dx0 = std::max(0, (int)std::floor(left)), dy0 = std::max(0, (int)std::floor(top));
        const int dx1 = std::min(w, (int)std::ceil(right)), dy1 = std::min(h, (int)std::ceil(bottom));
        for (int cy = dy0; cy < dy1; ++cy) {
            for (int cx = dx0; cx < dx1; ++cx) {
                const float px = cx + 0.5f - t.x, py = cy + 0.5f - t.y;
                const float sourceX = t.x + (cs * px + sn * py) / transformX;
                const float sourceY = t.y + (-sn * px + cs * py) / transformY;
                const int bx = (int)std::floor(sourceX - ox), by = (int)std::floor(sourceY - oy);
                if (bx < 0 || bx >= drawW || by < 0 || by >= drawH) continue;
                const unsigned char alpha = bm[(size_t)by * drawW + bx];
                if (!alpha) continue;
                float& o = cov[(size_t)cy * w + cx];
                o = std::min(1.0f, o + alpha / 255.0f);
            }
        }
        if(!shiftAlongX&&(underline||strikethrough)){
            int underlinePos=0,underlineSize=0;
            const bool hasUnderline=stbtt_GetFontUnderlineMetrics(glyphFi,&underlinePos,&underlineSize)!=0;
            const float underlineScale=glyphScale*verticalScale;
            if(underline&&hasUnderline)decoration(-underlinePos*underlineScale,std::max(1.0f,underlineSize*underlineScale));
            if(strikethrough&&strikeSize>0)decoration(-strikePosition*underlineScale-strikeSize*underlineScale*.5f,strikeSize*underlineScale);
        }
    };
    auto loadedForGlyph = [&](const TextGlyph& glyph) {
        return fontForStyle(styleForCharacter(styles, glyph.character, fallback), t.font);
    };
    auto scaleForGlyph = [&](const TextGlyph& glyph) {
        return stbtt_ScaleForPixelHeight(&loadedForGlyph(glyph)->info,
            std::max(1.0f, styleForCharacter(styles, glyph.character, fallback).size*glyph.sizeScale));
    };
    struct LineMetrics { float ascent=0, descent=0, step=0; };
    std::vector<LineMetrics> metrics(lines.size());
    std::vector<float> baselines(lines.size());
    float lineTop=t.y;
    for(size_t li=0;li<lines.size();++li){
        LineMetrics& m=metrics[li];
        for(const TextGlyph& glyph:lines[li]){
            const TextStyle& style=styleForCharacter(styles,glyph.character,fallback);
            Loaded* loaded=loadedForGlyph(glyph);if(!loaded)continue;
            int fontAsc=0,fontDesc=0,fontGap=0;stbtt_GetFontVMetrics(&loaded->info,&fontAsc,&fontDesc,&fontGap);
            const float glyphScale=scaleForGlyph(glyph);
            const float verticalScale=std::isfinite(style.verticalScale)&&style.verticalScale>0?style.verticalScale:1.0f;
            m.ascent=std::max(m.ascent,fontAsc*glyphScale*verticalScale);
            m.descent=std::max(m.descent,-fontDesc*glyphScale*verticalScale);
            m.step=std::max(m.step,std::max(.5f,style.leading)*style.size*verticalScale);
        }
        if(lines[li].empty()){
            m.ascent=asc*scale;m.descent=-desc*scale;m.step=std::max(.5f,fallback.leading)*fallback.size;
        }
        m.step=std::max(m.step,m.ascent+m.descent);
        baselines[li]=lineTop+m.ascent;
        lineTop+=m.step;
    }
    if (!t.vertical) {
        // each line's width, for centring / right
        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& L = lines[li];
            float wsum = 0; for (size_t i = 0; i < L.size(); ++i) wsum += glyphAdvance(L[i], i + 1 < L.size() ? &L[i + 1] : nullptr, styles, fallback, t.font);
            float gx = t.x - (t.align == 1 ? wsum / 2 : t.align == 2 ? wsum : 0);
            const float gy = baselines[li];
            for (size_t i = 0; i < L.size(); ++i) {
                const float adv = glyphAdvance(L[i], i + 1 < L.size() ? &L[i + 1] : nullptr, styles, fallback, t.font);
                Loaded* glyphFont=loadedForGlyph(L[i]);
                if(!glyphFont)continue;
                const TextStyle& style=styleForCharacter(styles,L[i].character,fallback);
                const float horizontalScale=std::isfinite(style.horizontalScale)&&style.horizontalScale>0?style.horizontalScale:1.0f;
                const float verticalScale=std::isfinite(style.verticalScale)&&style.verticalScale>0?style.verticalScale:1.0f;
                const auto strike=strikeMetrics(*glyphFont);
                blit(L[i].codepoint, gx, gy, &glyphFont->info, scaleForGlyph(L[i]),style.size*L[i].sizeScale,L[i].character,adv,strike.first,strike.second,horizontalScale,verticalScale,
                     style.baselineShift*pointsToPixels,false,style.bold,style.italic,style.underline,style.strikethrough,
                     pathOn ? gx - t.x + adv * 0.5f : std::numeric_limits<float>::quiet_NaN());
                gx += adv;
            }
            // an empty line still takes room in the box
            if (!pathOn) { minX = std::min(minX, t.x); maxY = std::max(maxY, gy - metrics[li].descent); }
        }
    } else {
        // letters top to bottom, each centred on its column; lines right to left from x
        float colX=t.x;
        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& L = lines[li];
            float total = 0; for(const TextGlyph& glyph:L){const TextStyle& style=styleForCharacter(styles,glyph.character,fallback);const float hs=std::isfinite(style.horizontalScale)&&style.horizontalScale>0?style.horizontalScale:1.0f;const float glyphSize=style.size*glyph.sizeScale;total+=(glyphSize+style.tracking/1000.0f*glyphSize)*hs;}
            float gy = t.y - (t.align == 1 ? total / 2 : t.align == 2 ? total : 0) + metrics[li].ascent;
            const float cx = colX;
            for (size_t i = 0; i < L.size(); ++i) {
                Loaded* glyphFont=loadedForGlyph(L[i]);if(!glyphFont)continue;
                const float glyphScale=scaleForGlyph(L[i]);int adv=0,lsb=0;stbtt_GetCodepointHMetrics(&glyphFont->info,L[i].codepoint,&adv,&lsb);
                const TextStyle& style=styleForCharacter(styles,L[i].character,fallback);
                const float hs=std::isfinite(style.horizontalScale)&&style.horizontalScale>0?style.horizontalScale:1.0f;
                const float vs=std::isfinite(style.verticalScale)&&style.verticalScale>0?style.verticalScale:1.0f;
                const auto strike=strikeMetrics(*glyphFont);
                const float glyphSize=style.size*L[i].sizeScale,step=glyphSize*vs+style.tracking/1000.0f*glyphSize*hs;
                blit(L[i].codepoint, cx - adv * glyphScale * hs / 2, gy,&glyphFont->info,glyphScale,glyphSize,L[i].character,step,strike.first,strike.second,hs,vs,
                     style.baselineShift*pointsToPixels,true,style.bold,style.italic,style.underline,style.strikethrough);
                gy+=style.size*vs+style.tracking/1000.0f*style.size*hs;
            }
            colX-=metrics[li].step;
        }
    }
    if (minX <= maxX) { box[0] = minX; box[1] = minY; box[2] = maxX; box[3] = maxY; }
    else { box[0] = t.x; box[1] = t.y; box[2] = t.x + t.size * 0.6f; box[3] = t.y + t.size; }
    return true;
}

// after PhotoCraft (crates/engine/src/cmd/type_cmds.rs type.edit kernPair): a manual pair value
// starts from the current font kerning, then replaces it so the pair's appearance changes by `by`.
bool textAdjustKerningPair(Text& text,size_t caret,float by){
    if(!std::isfinite(by))return false;
    const auto styles=text.characterStyles();const size_t count=styles.size();
    if(caret==0||caret>=count)return false;
    std::vector<uint32_t> chars;std::vector<size_t> offsets;size_t byte=0;
    while(byte<text.str.size()){size_t len=0;chars.push_back(utf8CodePoint(text.str,byte,len));offsets.push_back(byte);byte+=std::max<size_t>(1,len);}
    if(caret>=chars.size()||chars[caret-1]=='\n'||chars[caret]=='\n')return false;
    const TextStyle& style=styles[caret-1];const TextStyle& nextStyle=styles[caret];
    float current=style.kern;
    if(!style.kerningOff){
        const std::string& font=style.font.empty()?text.font:style.font;
        const std::string& nextFont=nextStyle.font.empty()?text.font:nextStyle.font;
        if(font==nextFont&&style.size==nextStyle.size){
            Loaded* loaded=fontForStyle(style,text.font);
            if(!loaded)return false;
            const float scale=stbtt_ScaleForPixelHeight(&loaded->info,std::max(1.0f,style.size));
            const float metric=stbtt_GetCodepointKernAdvance(&loaded->info,(int)chars[caret-1],(int)chars[caret])*scale;
            current+=metric/std::max(1.0f,style.size)*1000.0f;
        }
    }
    TextStyle updated=style;updated.kerningOff=true;updated.kern=std::clamp(std::round(current)+by,-1000.0f,10000.0f);
    if(updated.kerningOff==style.kerningOff&&updated.kern==style.kern)return false;
    std::vector<TextStyle> changed=styles;changed[caret-1]=updated;
    text.runs.clear();byte=0;
    for(size_t i=0;i<changed.size();++i){
        const size_t end=i+1<offsets.size()?offsets[i+1]:text.str.size();const size_t len=end-byte;
        if(!text.runs.empty()&&text.runs.back().style==changed[i])text.runs.back().bytes+=len;
        else text.runs.push_back({len,changed[i]});
        byte=end;
    }
    text.syncSummary();return true;
}
