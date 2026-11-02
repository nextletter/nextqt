// 由 tools/ucdgen/ucdgen.py 生成，请勿手改。
// Unicode 版本: 16.0.0
#pragma once

#include <cstdint>
#include <string_view>

namespace nq::unicode {

enum class Gcb : uint8_t {
  kOther = 0,
  kCR = 1,
  kLF = 2,
  kControl = 3,
  kExtend = 4,
  kZWJ = 5,
  kRegionalIndicator = 6,
  kPrepend = 7,
  kSpacingMark = 8,
  kL = 9,
  kV = 10,
  kT = 11,
  kLV = 12,
  kLVT = 13,
};
inline constexpr std::string_view gcb_name(Gcb v) noexcept {
  switch (v) {
    case Gcb::kOther: return "Other";
    case Gcb::kCR: return "CR";
    case Gcb::kLF: return "LF";
    case Gcb::kControl: return "Control";
    case Gcb::kExtend: return "Extend";
    case Gcb::kZWJ: return "ZWJ";
    case Gcb::kRegionalIndicator: return "Regional_Indicator";
    case Gcb::kPrepend: return "Prepend";
    case Gcb::kSpacingMark: return "SpacingMark";
    case Gcb::kL: return "L";
    case Gcb::kV: return "V";
    case Gcb::kT: return "T";
    case Gcb::kLV: return "LV";
    case Gcb::kLVT: return "LVT";
  }
  return "?";
}

enum class IncB : uint8_t {
  kNone = 0,
  kLinker = 1,
  kConsonant = 2,
  kExtend = 3,
};
inline constexpr std::string_view incb_name(IncB v) noexcept {
  switch (v) {
    case IncB::kNone: return "None";
    case IncB::kLinker: return "Linker";
    case IncB::kConsonant: return "Consonant";
    case IncB::kExtend: return "Extend";
  }
  return "?";
}

enum class Lb : uint8_t {
  kXX = 0,
  kAI = 1,
  kAK = 2,
  kAL = 3,
  kAP = 4,
  kAS = 5,
  kB2 = 6,
  kBA = 7,
  kBB = 8,
  kBK = 9,
  kCB = 10,
  kCJ = 11,
  kCL = 12,
  kCM = 13,
  kCP = 14,
  kCR = 15,
  kEB = 16,
  kEM = 17,
  kEX = 18,
  kGL = 19,
  kH2 = 20,
  kH3 = 21,
  kHL = 22,
  kHY = 23,
  kID = 24,
  kIN = 25,
  kIS = 26,
  kJL = 27,
  kJT = 28,
  kJV = 29,
  kLF = 30,
  kNL = 31,
  kNS = 32,
  kNU = 33,
  kOP = 34,
  kPO = 35,
  kPR = 36,
  kQU = 37,
  kRI = 38,
  kSA = 39,
  kSG = 40,
  kSP = 41,
  kSY = 42,
  kVF = 43,
  kVI = 44,
  kWJ = 45,
  kZW = 46,
  kZWJ = 47,
};
inline constexpr std::string_view lb_name(Lb v) noexcept {
  switch (v) {
    case Lb::kXX: return "XX";
    case Lb::kAI: return "AI";
    case Lb::kAK: return "AK";
    case Lb::kAL: return "AL";
    case Lb::kAP: return "AP";
    case Lb::kAS: return "AS";
    case Lb::kB2: return "B2";
    case Lb::kBA: return "BA";
    case Lb::kBB: return "BB";
    case Lb::kBK: return "BK";
    case Lb::kCB: return "CB";
    case Lb::kCJ: return "CJ";
    case Lb::kCL: return "CL";
    case Lb::kCM: return "CM";
    case Lb::kCP: return "CP";
    case Lb::kCR: return "CR";
    case Lb::kEB: return "EB";
    case Lb::kEM: return "EM";
    case Lb::kEX: return "EX";
    case Lb::kGL: return "GL";
    case Lb::kH2: return "H2";
    case Lb::kH3: return "H3";
    case Lb::kHL: return "HL";
    case Lb::kHY: return "HY";
    case Lb::kID: return "ID";
    case Lb::kIN: return "IN";
    case Lb::kIS: return "IS";
    case Lb::kJL: return "JL";
    case Lb::kJT: return "JT";
    case Lb::kJV: return "JV";
    case Lb::kLF: return "LF";
    case Lb::kNL: return "NL";
    case Lb::kNS: return "NS";
    case Lb::kNU: return "NU";
    case Lb::kOP: return "OP";
    case Lb::kPO: return "PO";
    case Lb::kPR: return "PR";
    case Lb::kQU: return "QU";
    case Lb::kRI: return "RI";
    case Lb::kSA: return "SA";
    case Lb::kSG: return "SG";
    case Lb::kSP: return "SP";
    case Lb::kSY: return "SY";
    case Lb::kVF: return "VF";
    case Lb::kVI: return "VI";
    case Lb::kWJ: return "WJ";
    case Lb::kZW: return "ZW";
    case Lb::kZWJ: return "ZWJ";
  }
  return "?";
}

enum class Bc : uint8_t {
  kL = 0,
  kR = 1,
  kAL = 2,
  kEN = 3,
  kES = 4,
  kET = 5,
  kAN = 6,
  kCS = 7,
  kB = 8,
  kS = 9,
  kWS = 10,
  kON = 11,
  kLRE = 12,
  kLRO = 13,
  kRLE = 14,
  kRLO = 15,
  kPDF = 16,
  kNSM = 17,
  kBN = 18,
  kFS = 19,
  kLRI = 20,
  kRLI = 21,
  kFSI = 22,
  kPDI = 23,
};
inline constexpr std::string_view bc_name(Bc v) noexcept {
  switch (v) {
    case Bc::kL: return "L";
    case Bc::kR: return "R";
    case Bc::kAL: return "AL";
    case Bc::kEN: return "EN";
    case Bc::kES: return "ES";
    case Bc::kET: return "ET";
    case Bc::kAN: return "AN";
    case Bc::kCS: return "CS";
    case Bc::kB: return "B";
    case Bc::kS: return "S";
    case Bc::kWS: return "WS";
    case Bc::kON: return "ON";
    case Bc::kLRE: return "LRE";
    case Bc::kLRO: return "LRO";
    case Bc::kRLE: return "RLE";
    case Bc::kRLO: return "RLO";
    case Bc::kPDF: return "PDF";
    case Bc::kNSM: return "NSM";
    case Bc::kBN: return "BN";
    case Bc::kFS: return "FS";
    case Bc::kLRI: return "LRI";
    case Bc::kRLI: return "RLI";
    case Bc::kFSI: return "FSI";
    case Bc::kPDI: return "PDI";
  }
  return "?";
}

enum class Gc : uint8_t {
  kCn = 0,
  kLu = 1,
  kLl = 2,
  kLt = 3,
  kLm = 4,
  kLo = 5,
  kMn = 6,
  kMc = 7,
  kMe = 8,
  kNd = 9,
  kNl = 10,
  kNo = 11,
  kPc = 12,
  kPd = 13,
  kPs = 14,
  kPe = 15,
  kPi = 16,
  kPf = 17,
  kPo = 18,
  kSm = 19,
  kSc = 20,
  kSk = 21,
  kSo = 22,
  kZs = 23,
  kZl = 24,
  kZp = 25,
  kCc = 26,
  kCf = 27,
  kCs = 28,
  kCo = 29,
};
inline constexpr std::string_view gc_name(Gc v) noexcept {
  switch (v) {
    case Gc::kCn: return "Cn";
    case Gc::kLu: return "Lu";
    case Gc::kLl: return "Ll";
    case Gc::kLt: return "Lt";
    case Gc::kLm: return "Lm";
    case Gc::kLo: return "Lo";
    case Gc::kMn: return "Mn";
    case Gc::kMc: return "Mc";
    case Gc::kMe: return "Me";
    case Gc::kNd: return "Nd";
    case Gc::kNl: return "Nl";
    case Gc::kNo: return "No";
    case Gc::kPc: return "Pc";
    case Gc::kPd: return "Pd";
    case Gc::kPs: return "Ps";
    case Gc::kPe: return "Pe";
    case Gc::kPi: return "Pi";
    case Gc::kPf: return "Pf";
    case Gc::kPo: return "Po";
    case Gc::kSm: return "Sm";
    case Gc::kSc: return "Sc";
    case Gc::kSk: return "Sk";
    case Gc::kSo: return "So";
    case Gc::kZs: return "Zs";
    case Gc::kZl: return "Zl";
    case Gc::kZp: return "Zp";
    case Gc::kCc: return "Cc";
    case Gc::kCf: return "Cf";
    case Gc::kCs: return "Cs";
    case Gc::kCo: return "Co";
  }
  return "?";
}

// 字素簇边界属性（UAX #29）
[[nodiscard]] Gcb gcb(char32_t cp) noexcept;
// 印度语系连写属性（UAX #29 GB9c）
[[nodiscard]] IncB incb(char32_t cp) noexcept;
// 换行类（UAX #14，待实现）
[[nodiscard]] Lb lbc(char32_t cp) noexcept;
// 双向类（UAX #9，待实现）
[[nodiscard]] Bc bidi_class(char32_t cp) noexcept;
// 通用类别
[[nodiscard]] Gc general_category(char32_t cp) noexcept;
// 扩展象形（emoji，UAX #29 GB11）
[[nodiscard]] bool is_extended_pictographic(char32_t cp) noexcept;

inline constexpr std::string_view kUnicodeVersion = "16.0.0";

}  // namespace nq::unicode
