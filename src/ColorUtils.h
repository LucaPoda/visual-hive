// ColorUtils.h
#pragma once
#include <string>
#include <algorithm>
#include <cctype>

namespace ColorUtils {

inline std::string normalizeColorName(const std::string& raw) {
    if (raw.empty()) return "";
    std::string s = raw;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    
    // Trim spaces
    s.erase(0, s.find_first_not_of(" \t\n\r"));
    s.erase(s.find_last_not_of(" \t\n\r") + 1);
    
    if (s == "white" || s == "bianco") return "White";
    if (s == "black" || s == "nero") return "Black";
    if (s == "red" || s == "rosso") return "Red";
    if (s == "green" || s == "verde") return "Green";
    if (s == "blue" || s == "blu") return "Blue";
    if (s == "cyan" || s == "ciano") return "Cyan";
    if (s == "magenta" || s == "viola" || s == "purple") return "Magenta";
    if (s == "yellow" || s == "giallo") return "Yellow";
    if (s == "orange" || s == "arancione") return "Orange";
    if (s == "nessuno" || s == "none" || s == "nessuna palette") return "";
    
    // Capitalize first letter if unrecognized
    std::string res = s;
    if (!res.empty()) res[0] = std::toupper(res[0]);
    return res;
}

inline std::string getItalianColorName(const std::string& canonical) {
    std::string c = normalizeColorName(canonical);
    if (c == "White") return "Bianco";
    if (c == "Black") return "Nero";
    if (c == "Red") return "Rosso";
    if (c == "Green") return "Verde";
    if (c == "Blue") return "Blu";
    if (c == "Cyan") return "Ciano";
    if (c == "Magenta") return "Viola";
    if (c == "Yellow") return "Giallo";
    if (c == "Orange") return "Arancione";
    return c;
}

} // namespace ColorUtils
