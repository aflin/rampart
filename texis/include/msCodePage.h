#ifndef MS_CODEPAGE_H
#define MS_CODEPAGE_H

#define MS_CODE_PAGE_SYMBOLS_LIST        \
I(37,    IBM037,                "IBM EBCDIC US-Canada") \
I(437,   IBM437,                "OEM United States") \
I(500,   IBM500,                "IBM EBCDIC International") \
I(708,   ASMO_708,              "Arabic (ASMO 708)") \
I(709,   ASMO_449,              "Arabic (ASMO-449+, BCON V4)") \
I(710,   Arabic_Transparent,    "Transparent Arabic") \
I(720,   DOS_720,               "Arabic (Transparent ASMO); Arabic (DOS)") \
I(737,   ibm737,                "OEM Greek (formerly 437G); Greek (DOS)") \
I(775,   ibm775,                "OEM Baltic; Baltic (DOS)") \
I(850,   ibm850,                "OEM Multilingual Latin 1; Western European (DOS)") \
I(852,   ibm852,                "OEM Latin 2; Central European (DOS)") \
I(855,   IBM855,                "OEM Cyrillic (primarily Russian)") \
I(857,   ibm857,                "OEM Turkish; Turkish (DOS)") \
I(858,   IBM00858,              "OEM Multilingual Latin 1 + Euro symbol") \
I(860,   IBM860,                "OEM Portuguese; Portuguese (DOS)") \
I(861,   ibm861,                "OEM Icelandic; Icelandic (DOS)") \
I(862,   DOS_862,               "OEM Hebrew; Hebrew (DOS)") \
I(863,   IBM863,                "OEM French Canadian; French Canadian (DOS)") \
I(864,   IBM864,                "OEM Arabic; Arabic (864)") \
I(865,   IBM865,                "OEM Nordic; Nordic (DOS)") \
I(866,   cp866,                 "OEM Russian; Cyrillic (DOS)") \
I(869,   ibm869,                "OEM Modern Greek (DOS)") \
I(870,   IBM870,                "IBM EBCDIC Multilingual/ROECE (Latin 2); IBM EBCDIC Multilingual Latin 2") \
I(874,   windows_874,           "ANSI/OEM Thai (ISO 8859-11); Thai (Windows)") \
I(875,   cp875,                 "IBM EBCDIC Greek Modern") \
I(932,   shift_jis,             "ANSI/OEM Japanese; Japanese (Shift-JIS)") \
I(936,   gb2312,                "ANSI/OEM Simplified Chinese (PRC, Singapore); Chinese Simplified (GB2312)") \
I(949,   ks_c_5601_1987,        "ANSI/OEM Korean (Unified Hangul Code)") \
I(950,   big5,                  "ANSI/OEM Traditional Chinese (Taiwan; Hong Kong SAR, PRC); Chinese Traditional (Big5)") \
I(1026,  IBM1026,               "IBM EBCDIC Turkish (Latin 5)") \
I(1047,  IBM01047,              "IBM EBCDIC Latin 1/Open System") \
I(1140,  IBM01140,              "IBM EBCDIC US-Canada (037 + Euro symbol); IBM EBCDIC (US-Canada-Euro)") \
I(1141,  IBM01141,              "IBM EBCDIC Germany (20273 + Euro symbol); IBM EBCDIC (Germany-Euro)") \
I(1142,  IBM01142,              "IBM EBCDIC Denmark-Norway (20277 + Euro symbol); IBM EBCDIC (Denmark-Norway-Euro)") \
I(1143,  IBM01143,              "IBM EBCDIC Finland-Sweden (20278 + Euro symbol); IBM EBCDIC (Finland-Sweden-Euro)") \
I(1144,  IBM01144,              "IBM EBCDIC Italy (20280 + Euro symbol); IBM EBCDIC (Italy-Euro)") \
I(1145,  IBM01145,              "IBM EBCDIC Latin America-Spain (20284 + Euro symbol); IBM EBCDIC (Spain-Euro)") \
I(1146,  IBM01146,              "IBM EBCDIC United Kingdom (20285 + Euro symbol); IBM EBCDIC (UK-Euro)") \
I(1147,  IBM01147,              "IBM EBCDIC France (20297 + Euro symbol); IBM EBCDIC (France-Euro)") \
I(1148,  IBM01148,              "IBM EBCDIC International (500 + Euro symbol); IBM EBCDIC (International-Euro)") \
I(1149,  IBM01149,              "IBM EBCDIC Icelandic (20871 + Euro symbol); IBM EBCDIC (Icelandic-Euro)") \
I(1200,  utf_16,                "Unicode UTF-16, little endian byte order (BMP of ISO 10646)") \
I(1201,  unicodeFFFE,           "Unicode UTF-16, big endian byte order") \
I(1250,  windows_1250,          "ANSI Central European; Central European (Windows)") \
I(1251,  windows_1251,          "ANSI Cyrillic; Cyrillic (Windows)") \
I(1252,  windows_1252,          "ANSI Latin 1; Western European (Windows)") \
I(1253,  windows_1253,          "ANSI Greek; Greek (Windows)") \
I(1254,  windows_1254,          "ANSI Turkish; Turkish (Windows)") \
I(1255,  windows_1255,          "ANSI Hebrew; Hebrew (Windows)") \
I(1256,  windows_1256,          "ANSI Arabic; Arabic (Windows)") \
I(1257,  windows_1257,          "ANSI Baltic; Baltic (Windows)") \
I(1258,  windows_1258,          "ANSI/OEM Vietnamese; Vietnamese (Windows)") \
I(1361,  Johab,                 "Korean (Johab)") \
I(10000, macintosh,             "MAC Roman; Western European (Mac)") \
I(10001, x_mac_japanese,        "Japanese (Mac)") \
I(10002, x_mac_chinesetrad,     "MAC Traditional Chinese (Big5); Chinese Traditional (Mac)") \
I(10003, x_mac_korean,          "Korean (Mac)") \
I(10004, x_mac_arabic,          "Arabic (Mac)") \
I(10005, x_mac_hebrew,          "Hebrew (Mac)") \
I(10006, x_mac_greek,           "Greek (Mac)") \
I(10007, x_mac_cyrillic,        "Cyrillic (Mac)") \
I(10008, x_mac_chinesesimp,     "MAC Simplified Chinese (GB 2312); Chinese Simplified (Mac)") \
I(10010, x_mac_romanian,        "Romanian (Mac)") \
I(10017, x_mac_ukrainian,       "Ukrainian (Mac)") \
I(10021, x_mac_thai,            "Thai (Mac)") \
I(10029, x_mac_ce,              "MAC Latin 2; Central European (Mac)") \
I(10079, x_mac_icelandic,       "Icelandic (Mac)") \
I(10081, x_mac_turkish,         "Turkish (Mac)") \
I(10082, x_mac_croatian,        "Croatian (Mac)") \
I(12000, utf_32,                "Unicode UTF-32, little endian byte order") \
I(12001, utf_32BE,              "Unicode UTF-32, big endian byte order") \
I(20000, x_Chinese_CNS,         "CNS Taiwan; Chinese Traditional (CNS)") \
I(20001, x_cp20001,             "TCA Taiwan") \
I(20002, x_Chinese_Eten,        "Eten Taiwan; Chinese Traditional (Eten)") \
I(20003, x_cp20003,             "IBM5550 Taiwan") \
I(20004, x_cp20004,             "TeleText Taiwan") \
I(20005, x_cp20005,             "Wang Taiwan") \
I(20105, x_IA5,                 "IA5 (IRV International Alphabet No. 5, 7-bit); Western European (IA5)") \
I(20106, x_IA5_German,          "IA5 German (7-bit)") \
I(20107, x_IA5_Swedish,         "IA5 Swedish (7-bit)") \
I(20108, x_IA5_Norwegian,       "IA5 Norwegian (7-bit)") \
I(20127, us_ascii,              "US-ASCII (7-bit)") \
I(20261, x_cp20261,             "T.61") \
I(20269, x_cp20269,             "ISO 6937 Non-Spacing Accent") \
I(20273, IBM273,                "IBM EBCDIC Germany") \
I(20277, IBM277,                "IBM EBCDIC Denmark-Norway") \
I(20278, IBM278,                "IBM EBCDIC Finland-Sweden") \
I(20280, IBM280,                "IBM EBCDIC Italy") \
I(20284, IBM284,                "IBM EBCDIC Latin America-Spain") \
I(20285, IBM285,                "IBM EBCDIC United Kingdom") \
I(20290, IBM290,                "IBM EBCDIC Japanese Katakana Extended") \
I(20297, IBM297,                "IBM EBCDIC France") \
I(20420, IBM420,                "IBM EBCDIC Arabic") \
I(20423, IBM423,                "IBM EBCDIC Greek") \
I(20424, IBM424,                "IBM EBCDIC Hebrew") \
I(20833, x_EBCDIC_KoreanExtended, "IBM EBCDIC Korean Extended") \
I(20838, IBM_Thai,              "IBM EBCDIC Thai") \
I(20866, koi8_r,                "Russian (KOI8-R); Cyrillic (KOI8-R)") \
I(20871, IBM871,                "IBM EBCDIC Icelandic") \
I(20880, IBM880,                "IBM EBCDIC Cyrillic Russian") \
I(20905, IBM905,                "IBM EBCDIC Turkish") \
I(20924, IBM00924,              "IBM EBCDIC Latin 1/Open System (1047 + Euro symbol)") \
I(20932, EUC_JP,                "Japanese (JIS 0208-1990 and 0212-1990)") \
I(20936, x_cp20936,             "Simplified Chinese (GB2312); Chinese Simplified (GB2312-80)") \
I(20949, x_cp20949,             "Korean Wansung") \
I(21025, cp1025,                "IBM EBCDIC Cyrillic Serbian-Bulgarian") \
I(21027, deprecated,            "(deprecated)") \
I(21866, koi8_u,                "Ukrainian (KOI8-U); Cyrillic (KOI8-U)") \
I(28591, iso_8859_1,            "ISO 8859-1 Latin 1; Western European (ISO)") \
I(28592, iso_8859_2,            "ISO 8859-2 Central European; Central European (ISO)") \
I(28593, iso_8859_3,            "ISO 8859-3 Latin 3") \
I(28594, iso_8859_4,            "ISO 8859-4 Baltic") \
I(28595, iso_8859_5,            "ISO 8859-5 Cyrillic") \
I(28596, iso_8859_6,            "ISO 8859-6 Arabic") \
I(28597, iso_8859_7,            "ISO 8859-7 Greek") \
I(28598, iso_8859_8,            "ISO 8859-8 Hebrew; Hebrew (ISO-Visual)") \
I(28599, iso_8859_9,            "ISO 8859-9 Turkish") \
I(28603, iso_8859_13,           "ISO 8859-13 Estonian") \
I(28605, iso_8859_15,           "ISO 8859-15 Latin 9") \
I(29001, x_Europa,              "Europa 3") \
I(38598, iso_8859_8_i,          "ISO 8859-8 Hebrew; Hebrew (ISO-Logical)") \
I(50220, iso_2022_jp,           "ISO 2022 Japanese with no halfwidth Katakana; Japanese (JIS)") \
I(50221, csISO2022JP,           "ISO 2022 Japanese with halfwidth Katakana; Japanese (JIS-Allow 1 byte Kana)") \
I(50222, iso_2022_jp_kana,      "ISO 2022 Japanese JIS X 0201-1989; Japanese (JIS-Allow 1 byte Kana - SO/SI)") \
I(50225, iso_2022_kr,           "ISO 2022 Korean") \
I(50227, x_cp50227,             "ISO 2022 Simplified Chinese; Chinese Simplified (ISO 2022)") \
I(50229, ISO_2022,              "ISO 2022 Traditional Chinese") \
I(50930, EBCDIC_CP50930,        "EBCDIC Japanese (Katakana) Extended") \
I(50931, EBCDIC_CP50931,        "EBCDIC US-Canada and Japanese") \
I(50933, EBCDIC_CP50933,        "EBCDIC Korean Extended and Korean") \
I(50935, EBCDIC_CP50935,        "EBCDIC Simplified Chinese Extended and Simplified Chinese") \
I(50936, EBCDIC_CP50936,        "EBCDIC Simplified Chinese") \
I(50937, EBCDIC_CP50937,        "EBCDIC US-Canada and Traditional Chinese") \
I(50939, EBCDIC_CP50939,        "EBCDIC Japanese (Latin) Extended and Japanese") \
I(51932, euc_jp,                "EUC Japanese") \
I(51936, EUC_CN,                "EUC Simplified Chinese; Chinese Simplified (EUC)") \
I(51949, euc_kr,                "EUC Korean") \
I(51950, EUC,                   "Traditional Chinese") \
I(52936, hz_gb_2312,            "HZ-GB2312 Simplified Chinese; Chinese Simplified (HZ)") \
I(54936, GB18030,               "Windows XP and later: GB18030 Simplified Chinese (4 byte); Chinese Simplified (GB18030)") \
I(57002, x_iscii_de,            "ISCII Devanagari") \
I(57003, x_iscii_be,            "ISCII Bangla") \
I(57004, x_iscii_ta,            "ISCII Tamil") \
I(57005, x_iscii_te,            "ISCII Telugu") \
I(57006, x_iscii_as,            "ISCII Assamese") \
I(57007, x_iscii_or,            "ISCII Odia") \
I(57008, x_iscii_ka,            "ISCII Kannada") \
I(57009, x_iscii_ma,            "ISCII Malayalam") \
I(57010, x_iscii_gu,            "ISCII Gujarati") \
I(57011, x_iscii_pa,            "ISCII Punjabi") \
I(65000, utf_7,                 "Unicode (UTF-7)") \
I(65001, utf_8,                 "Unicode (UTF-8)")

#endif /* !MS_CODEPAGE_H */
