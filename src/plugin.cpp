#include "plugin_api.h"

#include <array>
#include <cwctype>
#include <string>
#include <string_view>

namespace {

constexpr TCHAR kPluginName[] = TEXT("Bunyan Log Viewer");
constexpr wchar_t kPluginVersion[] = L"1.2.0";
constexpr wchar_t kPluginAuthor[] = L"Jayapal B, Asim B";
constexpr size_t kCommandCount = 4;
constexpr wchar_t kLogLevelPromptWindowClass[] = L"BunyanLogViewerLevelPrompt";
constexpr wchar_t kPreviewWindowClass[] = L"BunyanLogViewerPreview";
constexpr wchar_t kPreviewPaneName[] = L"Bunyan Log Viewer";
constexpr wchar_t kPreviewPaneInfo[] = L"Read-only Bunyan short-log preview";
constexpr wchar_t kPluginModuleName[] = L"BunyanLogViewer.dll";
constexpr UINT_PTR kPreviewRefreshTimerId = 1;
constexpr UINT kPreviewRefreshIntervalMs = 750;

enum class ViewMode : int {
    None = 0,
    Short = 1,
    LevelFilter = 2,
};

struct PreviewWindowState {
    HWND windowHandle = nullptr;
    HWND contentHandle = nullptr;
    HFONT contentFont = nullptr;
    DockedWidgetData dockData{};
    ViewMode mode = ViewMode::None;
    std::array<bool, 4> selectedLevels{};
    UINT_PTR lastBufferId = 0;
    std::string lastSourceEncodedText;
    bool dockRegistered = false;
    bool visible = false;
};

enum CommandIndex : size_t {
    ShortViewCommand = 0,
    LogLevelCommand = 1,
    ResetCommand = 2,
    AboutCommand = 3,
};

constexpr std::array<CommandIndex, 2> kToolbarCommands = {
    ShortViewCommand,
    LogLevelCommand,
};

struct LogLevelPromptState {
    std::array<bool, 4> selectedLevels{};
    bool accepted = false;
    bool finished = false;
    std::array<HWND, 4> checkboxHandles{};
};

HINSTANCE g_instance = nullptr;
NppData g_nppData{};
std::array<FuncItem, kCommandCount> g_commands{};
std::array<HBITMAP, kToolbarCommands.size()> g_toolbarBitmaps{};
std::array<HICON, kToolbarCommands.size()> g_toolbarIconsLight{};
std::array<HICON, kToolbarCommands.size()> g_toolbarIconsDark{};
bool g_toolbarIconRegistered = false;
PreviewWindowState g_previewWindow{};

constexpr std::array<int, 4> kSelectableLogLevels = {
    20,
    30,
    40,
    50,
};

constexpr std::array<const wchar_t*, 4> kSelectableLogLevelLabels = {
    L"DEBUG",
    L"INFO",
    L"WARN",
    L"ERROR",
};

std::array<bool, 4> g_lastSelectedLogLevels = {false, true, false, false};

void applyDefaultGuiFont(HWND control);
bool matchesSelectedLogLevel(int level, const std::array<bool, 4>& selectedLevels);

std::wstring utf8ToWide(std::string_view input, UINT codePage) {
    if (input.empty()) {
        return {};
    }

    const int requiredSize = MultiByteToWideChar(codePage, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (requiredSize <= 0) {
        return {};
    }

    std::wstring output(static_cast<size_t>(requiredSize), L'\0');
    MultiByteToWideChar(codePage, 0, input.data(), static_cast<int>(input.size()), output.data(), requiredSize);
    return output;
}

void cleanupToolbarIcons() {
    for (size_t index = 0; index < kToolbarCommands.size(); ++index) {
        if (g_toolbarBitmaps[index] != nullptr) {
            ::DeleteObject(g_toolbarBitmaps[index]);
            g_toolbarBitmaps[index] = nullptr;
        }

        if (g_toolbarIconsLight[index] != nullptr) {
            ::DestroyIcon(g_toolbarIconsLight[index]);
            g_toolbarIconsLight[index] = nullptr;
        }

        if (g_toolbarIconsDark[index] != nullptr) {
            ::DestroyIcon(g_toolbarIconsDark[index]);
            g_toolbarIconsDark[index] = nullptr;
        }
    }

    g_toolbarIconRegistered = false;
}

HBITMAP createToolbarBitmap(COLORREF backgroundColor, COLORREF detailColor, bool filterGlyph) {
    BITMAPV5HEADER bitmapHeader{};
    bitmapHeader.bV5Size = sizeof(bitmapHeader);
    bitmapHeader.bV5Width = 16;
    bitmapHeader.bV5Height = -16;
    bitmapHeader.bV5Planes = 1;
    bitmapHeader.bV5BitCount = 32;
    bitmapHeader.bV5Compression = BI_BITFIELDS;
    bitmapHeader.bV5RedMask = 0x00FF0000;
    bitmapHeader.bV5GreenMask = 0x0000FF00;
    bitmapHeader.bV5BlueMask = 0x000000FF;
    bitmapHeader.bV5AlphaMask = 0xFF000000;

    void* pixels = nullptr;
    HDC screenDc = ::GetDC(nullptr);
    HBITMAP colorBitmap = ::CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO*>(&bitmapHeader), DIB_RGB_COLORS, &pixels, nullptr, 0);
    ::ReleaseDC(nullptr, screenDc);
    if (colorBitmap == nullptr || pixels == nullptr) {
        return nullptr;
    }

    HDC memoryDc = ::CreateCompatibleDC(nullptr);
    HGDIOBJ previousBitmap = ::SelectObject(memoryDc, colorBitmap);

    HBRUSH backgroundBrush = ::CreateSolidBrush(backgroundColor);
    RECT bounds{0, 0, 16, 16};
    ::FillRect(memoryDc, &bounds, backgroundBrush);
    ::DeleteObject(backgroundBrush);

    HPEN detailPen = ::CreatePen(PS_SOLID, 2, detailColor);
    HGDIOBJ previousPen = ::SelectObject(memoryDc, detailPen);
    HGDIOBJ previousBrush = ::SelectObject(memoryDc, ::GetStockObject(NULL_BRUSH));
    if (filterGlyph) {
        HBRUSH detailBrush = ::CreateSolidBrush(detailColor);
        ::SelectObject(memoryDc, detailBrush);
        POINT funnel[] = {
            {3, 3},
            {13, 3},
            {9, 8},
            {9, 12},
            {7, 13},
            {7, 8},
        };
        ::Polygon(memoryDc, funnel, static_cast<int>(std::size(funnel)));
        ::SelectObject(memoryDc, previousBrush);
        previousBrush = ::GetStockObject(NULL_BRUSH);
        ::DeleteObject(detailBrush);
    } else {
        ::Rectangle(memoryDc, 2, 2, 14, 14);
        ::MoveToEx(memoryDc, 4, 5, nullptr);
        ::LineTo(memoryDc, 12, 5);
        ::MoveToEx(memoryDc, 4, 8, nullptr);
        ::LineTo(memoryDc, 12, 8);
        ::MoveToEx(memoryDc, 4, 11, nullptr);
        ::LineTo(memoryDc, 9, 11);
    }

    ::SelectObject(memoryDc, previousBrush);
    ::SelectObject(memoryDc, previousPen);
    ::DeleteObject(detailPen);
    ::SelectObject(memoryDc, previousBitmap);
    ::DeleteDC(memoryDc);

    return colorBitmap;
}

HICON createToolbarIcon(COLORREF backgroundColor, COLORREF detailColor, bool filterGlyph) {
    HBITMAP colorBitmap = createToolbarBitmap(backgroundColor, detailColor, filterGlyph);
    if (colorBitmap == nullptr) {
        return nullptr;
    }

    HBITMAP maskBitmap = ::CreateBitmap(16, 16, 1, 1, nullptr);
    if (maskBitmap == nullptr) {
        ::DeleteObject(colorBitmap);
        return nullptr;
    }

    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = maskBitmap;
    iconInfo.hbmColor = colorBitmap;
    HICON icon = ::CreateIconIndirect(&iconInfo);

    ::DeleteObject(maskBitmap);
    ::DeleteObject(colorBitmap);
    return icon;
}

void registerToolbarIcon() {
    if (g_toolbarIconRegistered) {
        return;
    }

    bool registeredAll = true;
    for (size_t index = 0; index < kToolbarCommands.size(); ++index) {
        const bool filterGlyph = kToolbarCommands[index] == LogLevelCommand;
        const COLORREF lightBackground = filterGlyph ? RGB(212, 120, 28) : RGB(32, 94, 210);
        const COLORREF lightDetail = RGB(255, 255, 255);
        const COLORREF darkBackground = RGB(244, 244, 244);
        const COLORREF darkDetail = filterGlyph ? RGB(212, 120, 28) : RGB(32, 32, 32);

        if (g_toolbarBitmaps[index] == nullptr) {
            g_toolbarBitmaps[index] = createToolbarBitmap(lightBackground, lightDetail, filterGlyph);
        }
        if (g_toolbarIconsLight[index] == nullptr) {
            g_toolbarIconsLight[index] = createToolbarIcon(lightBackground, lightDetail, filterGlyph);
        }
        if (g_toolbarIconsDark[index] == nullptr) {
            g_toolbarIconsDark[index] = createToolbarIcon(darkBackground, darkDetail, filterGlyph);
        }
        if (g_toolbarBitmaps[index] == nullptr || g_toolbarIconsLight[index] == nullptr || g_toolbarIconsDark[index] == nullptr) {
            cleanupToolbarIcons();
            return;
        }

        toolbarIconsWithDarkMode toolbarIcons{};
        toolbarIcons.hToolbarBmp = g_toolbarBitmaps[index];
        toolbarIcons.hToolbarIcon = g_toolbarIconsLight[index];
        toolbarIcons.hToolbarIconDarkMode = g_toolbarIconsDark[index];

        const LRESULT added = ::SendMessage(
            g_nppData._nppHandle,
            NPPM_ADDTOOLBARICON_FORDARKMODE,
            static_cast<WPARAM>(g_commands[kToolbarCommands[index]]._cmdID),
            reinterpret_cast<LPARAM>(&toolbarIcons));
        registeredAll = registeredAll && (added != 0);
    }

    g_toolbarIconRegistered = registeredAll;
}

void setShortViewChecked(bool checked) {
    ::SendMessage(
        g_nppData._nppHandle,
        NPPM_SETMENUITEMCHECK,
        static_cast<WPARAM>(g_commands[ShortViewCommand]._cmdID),
        static_cast<LPARAM>(checked ? TRUE : FALSE));
}

std::string getEditorEncodedText(HWND scintilla) {
    const auto textLength = static_cast<int>(::SendMessage(scintilla, SCI_GETTEXTLENGTH, 0, 0));
    if (textLength <= 0) {
        return {};
    }

    std::string text(static_cast<size_t>(textLength + 1), '\0');
    ::SendMessage(scintilla, SCI_GETTEXT, static_cast<WPARAM>(text.size()), reinterpret_cast<LPARAM>(text.data()));
    if (!text.empty() && text.back() == '\0') {
        text.pop_back();
    }
    return text;
}

UINT getEditorCodePage(HWND scintilla) {
    const auto codePage = static_cast<UINT>(::SendMessage(scintilla, SCI_GETCODEPAGE, 0, 0));
    return codePage == 0 ? CP_ACP : codePage;
}

UINT_PTR getCurrentBufferId() {
    return static_cast<UINT_PTR>(::SendMessage(g_nppData._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
}

HWND getCurrentEditor() {
    int currentView = 0;
    ::SendMessage(g_nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&currentView));
    return currentView == 0 ? g_nppData._scintillaMainHandle : g_nppData._scintillaSecondHandle;
}

std::string getEditorLineEncoded(HWND scintilla, int lineIndex) {
    const auto lineLength = static_cast<int>(::SendMessage(scintilla, SCI_LINELENGTH, static_cast<WPARAM>(lineIndex), 0));
    if (lineLength <= 0) {
        return {};
    }

    std::string line(static_cast<size_t>(lineLength + 1), '\0');
    const auto copied = static_cast<int>(::SendMessage(scintilla, SCI_GETLINE, static_cast<WPARAM>(lineIndex), reinterpret_cast<LPARAM>(line.data())));
    line.resize(static_cast<size_t>(copied));
    if (!line.empty() && line.back() == '\0') {
        line.pop_back();
    }
    return line;
}

void splitEncodedLine(std::string_view fullLine, std::string_view& lineContent, std::string_view& lineEnding) {
    size_t contentEnd = fullLine.size();
    if (contentEnd > 0 && fullLine[contentEnd - 1] == '\n') {
        --contentEnd;
        if (contentEnd > 0 && fullLine[contentEnd - 1] == '\r') {
            --contentEnd;
        }
    } else if (contentEnd > 0 && fullLine[contentEnd - 1] == '\r') {
        --contentEnd;
    }

    lineContent = fullLine.substr(0, contentEnd);
    lineEnding = fullLine.substr(contentEnd);
}

bool readNextEncodedLine(std::string_view source, size_t& offset, std::string_view& lineContent, std::string_view& lineEnding) {
    if (offset >= source.size()) {
        return false;
    }

    const size_t lineStart = offset;
    const size_t lineBreak = source.find_first_of("\r\n", offset);
    if (lineBreak == std::string_view::npos) {
        offset = source.size();
        lineContent = source.substr(lineStart);
        lineEnding = {};
        return true;
    }

    size_t nextOffset = lineBreak + 1;
    if (source[lineBreak] == '\r' && nextOffset < source.size() && source[nextOffset] == '\n') {
        ++nextOffset;
    }

    splitEncodedLine(source.substr(lineStart, nextOffset - lineStart), lineContent, lineEnding);
    offset = nextOffset;
    return true;
}

std::wstring getLevelName(int level) {
    switch (level) {
    case 10:
        return L"TRACE";
    case 20:
        return L"DEBUG";
    case 30:
        return L"INFO";
    case 40:
        return L"WARN";
    case 50:
        return L"ERROR";
    case 60:
        return L"FATAL";
    default:
        return std::to_wstring(level);
    }
}

void skipWhitespace(std::wstring_view json, size_t& position) {
    while (position < json.size() && iswspace(json[position])) {
        ++position;
    }
}

bool parseJsonString(std::wstring_view json, size_t& position, std::wstring& value) {
    if (position >= json.size() || json[position] != L'"') {
        return false;
    }

    ++position;
    value.clear();
    while (position < json.size()) {
        const wchar_t ch = json[position++];
        if (ch == L'"') {
            return true;
        }

        if (ch != L'\\') {
            value.push_back(ch);
            continue;
        }

        if (position >= json.size()) {
            return false;
        }

        const wchar_t escaped = json[position++];
        switch (escaped) {
        case L'"':
        case L'\\':
        case L'/':
            value.push_back(escaped);
            break;
        case L'b':
            value.push_back(L'\b');
            break;
        case L'f':
            value.push_back(L'\f');
            break;
        case L'n':
            value.push_back(L'\n');
            break;
        case L'r':
            value.push_back(L'\r');
            break;
        case L't':
            value.push_back(L'\t');
            break;
        case L'u': {
            if (position + 4 > json.size()) {
                return false;
            }

            unsigned int codePoint = 0;
            for (int index = 0; index < 4; ++index) {
                codePoint <<= 4;
                const wchar_t hexChar = json[position++];
                if (hexChar >= L'0' && hexChar <= L'9') {
                    codePoint |= static_cast<unsigned int>(hexChar - L'0');
                } else if (hexChar >= L'a' && hexChar <= L'f') {
                    codePoint |= static_cast<unsigned int>(hexChar - L'a' + 10);
                } else if (hexChar >= L'A' && hexChar <= L'F') {
                    codePoint |= static_cast<unsigned int>(hexChar - L'A' + 10);
                } else {
                    return false;
                }
            }
            value.push_back(static_cast<wchar_t>(codePoint));
            break;
        }
        default:
            value.push_back(escaped);
            break;
        }
    }

    return false;
}

bool skipJsonValue(std::wstring_view json, size_t& position);

bool skipJsonComposite(std::wstring_view json, size_t& position, wchar_t openingChar, wchar_t closingChar) {
    if (position >= json.size() || json[position] != openingChar) {
        return false;
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    while (position < json.size()) {
        const wchar_t ch = json[position++];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == L'\\') {
                escaped = true;
            } else if (ch == L'"') {
                inString = false;
            }
            continue;
        }

        if (ch == L'"') {
            inString = true;
        } else if (ch == openingChar) {
            ++depth;
        } else if (ch == closingChar) {
            --depth;
            if (depth == 0) {
                return true;
            }
        }
    }

    return false;
}

bool skipJsonValue(std::wstring_view json, size_t& position) {
    skipWhitespace(json, position);
    if (position >= json.size()) {
        return false;
    }

    std::wstring ignored;
    const wchar_t ch = json[position];
    if (ch == L'"') {
        return parseJsonString(json, position, ignored);
    }
    if (ch == L'{') {
        return skipJsonComposite(json, position, L'{', L'}');
    }
    if (ch == L'[') {
        return skipJsonComposite(json, position, L'[', L']');
    }

    while (position < json.size()) {
        const wchar_t current = json[position];
        if (current == L',' || current == L'}' || current == L']') {
            return true;
        }
        ++position;
    }

    return true;
}

bool findTopLevelFieldValue(std::wstring_view json, std::wstring_view fieldName, size_t& valuePosition) {
    size_t position = 0;
    skipWhitespace(json, position);
    if (position >= json.size() || json[position] != L'{') {
        return false;
    }

    ++position;
    while (position < json.size()) {
        skipWhitespace(json, position);
        if (position >= json.size() || json[position] == L'}') {
            return false;
        }

        std::wstring key;
        if (!parseJsonString(json, position, key)) {
            return false;
        }

        skipWhitespace(json, position);
        if (position >= json.size() || json[position] != L':') {
            return false;
        }
        ++position;
        skipWhitespace(json, position);

        if (key == fieldName) {
            valuePosition = position;
            return true;
        }

        if (!skipJsonValue(json, position)) {
            return false;
        }

        skipWhitespace(json, position);
        if (position < json.size() && json[position] == L',') {
            ++position;
        }
    }

    return false;
}

bool tryGetJsonStringField(std::wstring_view json, std::wstring_view fieldName, std::wstring& value) {
    size_t position = 0;
    if (!findTopLevelFieldValue(json, fieldName, position)) {
        return false;
    }
    return parseJsonString(json, position, value);
}

bool tryGetJsonIntField(std::wstring_view json, std::wstring_view fieldName, int& value) {
    size_t position = 0;
    if (!findTopLevelFieldValue(json, fieldName, position)) {
        return false;
    }

    skipWhitespace(json, position);
    bool negative = false;
    if (position < json.size() && json[position] == L'-') {
        negative = true;
        ++position;
    }

    if (position >= json.size() || !iswdigit(json[position])) {
        return false;
    }

    int parsedValue = 0;
    while (position < json.size() && iswdigit(json[position])) {
        parsedValue = (parsedValue * 10) + static_cast<int>(json[position] - L'0');
        ++position;
    }

    value = negative ? -parsedValue : parsedValue;
    return true;
}

bool extractEmbeddedJsonObject(std::wstring_view line, std::wstring_view& jsonPart) {
    size_t searchStart = 0;
    const size_t stdoutMarker = line.find(L"stdout.");
    if (stdoutMarker != std::wstring_view::npos) {
        searchStart = stdoutMarker + 7;
    }

    const size_t objectStart = line.find(L'{', searchStart);
    if (objectStart == std::wstring_view::npos) {
        return false;
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t index = objectStart; index < line.size(); ++index) {
        const wchar_t ch = line[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == L'\\') {
                escaped = true;
            } else if (ch == L'"') {
                inString = false;
            }
            continue;
        }

        if (ch == L'"') {
            inString = true;
        } else if (ch == L'{') {
            ++depth;
        } else if (ch == L'}') {
            --depth;
            if (depth == 0) {
                jsonPart = line.substr(objectStart, index - objectStart + 1);
                return true;
            }
        }
    }

    return false;
}

bool tryGetLineLogLevel(std::wstring_view line, int& level) {
    std::wstring_view jsonPart;
    return extractEmbeddedJsonObject(line, jsonPart) && tryGetJsonIntField(jsonPart, L"level", level);
}

std::wstring formatShortTime(std::wstring_view timestamp) {
    const size_t timeSeparator = timestamp.find(L'T');
    if (timeSeparator != std::wstring_view::npos && timeSeparator + 9 <= timestamp.size()) {
        return std::wstring(timestamp.substr(timeSeparator + 1, 8));
    }

    if (timestamp.size() >= 8) {
        return std::wstring(timestamp.substr(0, 8));
    }

    return std::wstring(timestamp);
}

bool tryFormatShortLogLine(std::wstring_view line, std::wstring& formattedLine) {
    std::wstring_view jsonPart;
    if (!extractEmbeddedJsonObject(line, jsonPart)) {
        return false;
    }

    int level = 0;
    std::wstring message;
    std::wstring method;
    std::wstring caseId;
    std::wstring timeValue;

    if (!tryGetJsonIntField(jsonPart, L"level", level) || !tryGetJsonStringField(jsonPart, L"msg", message)) {
        return false;
    }

    tryGetJsonStringField(jsonPart, L"method", method);
    tryGetJsonStringField(jsonPart, L"caseId", caseId);
    tryGetJsonStringField(jsonPart, L"time", timeValue);

    formattedLine.clear();
    if (!timeValue.empty()) {
        formattedLine += formatShortTime(timeValue);
        formattedLine += L" ";
    }

    formattedLine += getLevelName(level);
    formattedLine += L": ";
    formattedLine += message;

    if (!method.empty()) {
        formattedLine += L" (method=";
        formattedLine += method;
        formattedLine += L")";
    }

    if (!caseId.empty()) {
        formattedLine += L" (caseId=";
        formattedLine += caseId;
        formattedLine += L")";
    }

    return true;
}

std::wstring buildEmptyPreviewText(ViewMode mode) {
    if (mode == ViewMode::LevelFilter) {
        return L"No Bunyan log lines matched the selected levels.\r\n\r\nThis preview refreshes automatically while the window is open.";
    }

    return L"No Bunyan log lines were detected in the current document.\r\n\r\nThis preview refreshes automatically while the window is open.";
}

std::wstring buildPreviewText(
    std::string_view sourceEncoded,
    UINT codePage,
    ViewMode mode,
    const std::array<bool, 4>& selectedLevels,
    size_t& matchCount) {
    std::wstring previewText;
    previewText.reserve(sourceEncoded.size() / 2);

    size_t offset = 0;
    std::string_view lineContent;
    std::string_view lineEnding;
    while (readNextEncodedLine(sourceEncoded, offset, lineContent, lineEnding)) {
        const std::wstring wideLine = utf8ToWide(lineContent, codePage);
        std::wstring formattedLine;

        if (mode == ViewMode::Short) {
            if (!tryFormatShortLogLine(wideLine, formattedLine)) {
                continue;
            }

            previewText += formattedLine;
            previewText += L"\r\n";
            ++matchCount;
            continue;
        }

        int lineLevel = 0;
        if (!tryGetLineLogLevel(wideLine, lineLevel) || !matchesSelectedLogLevel(lineLevel, selectedLevels)) {
            continue;
        }

        if (tryFormatShortLogLine(wideLine, formattedLine)) {
            previewText += formattedLine;
            previewText += L"\r\n";
        } else {
            previewText += wideLine;
            if (lineEnding == "\r\n") {
                previewText += L"\r\n";
            } else if (lineEnding == "\n") {
                previewText += L"\n";
            } else if (lineEnding == "\r") {
                previewText += L"\r";
            }
        }
        ++matchCount;
    }

    if (matchCount == 0) {
        return buildEmptyPreviewText(mode);
    }

    return previewText;
}

HFONT createPreviewFont() {
    LOGFONTW logFont{};
    logFont.lfHeight = -13;
    logFont.lfWeight = FW_NORMAL;
    logFont.lfCharSet = DEFAULT_CHARSET;
    logFont.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    lstrcpyW(logFont.lfFaceName, L"Consolas");
    return ::CreateFontIndirectW(&logFont);
}

std::wstring getPreviewWindowTitle() {
    switch (g_previewWindow.mode) {
    case ViewMode::Short:
        return L"Bunyan Log Viewer Preview - Short View";
    case ViewMode::LevelFilter:
        return L"Bunyan Log Viewer Preview - Filtered View";
    default:
        return L"Bunyan Log Viewer Preview";
    }
}

void updatePreviewWindowTitle() {
    if (g_previewWindow.windowHandle == nullptr || !g_previewWindow.visible) {
        return;
    }

    const std::wstring title = getPreviewWindowTitle();
    ::SetWindowTextW(g_previewWindow.windowHandle, title.c_str());
    ::SendMessage(g_nppData._nppHandle, NPPM_DMMUPDATEDISPINFO, 0, reinterpret_cast<LPARAM>(g_previewWindow.windowHandle));
}

void invalidatePreviewSnapshot() {
    g_previewWindow.lastBufferId = 0;
    g_previewWindow.lastSourceEncodedText.clear();
}

void refreshPreviewWindowContent(bool force) {
    if (g_previewWindow.contentHandle == nullptr || g_previewWindow.mode == ViewMode::None || !g_previewWindow.visible) {
        return;
    }

    const HWND editor = getCurrentEditor();
    const UINT_PTR bufferId = getCurrentBufferId();
    std::string encodedText = getEditorEncodedText(editor);

    if (!force && bufferId == g_previewWindow.lastBufferId && encodedText == g_previewWindow.lastSourceEncodedText) {
        return;
    }

    const UINT codePage = getEditorCodePage(editor);
    size_t matchCount = 0;
    const std::wstring previewText = buildPreviewText(
        encodedText,
        codePage,
        g_previewWindow.mode,
        g_previewWindow.selectedLevels,
        matchCount);

    ::SetWindowTextW(g_previewWindow.contentHandle, previewText.c_str());
    g_previewWindow.lastBufferId = bufferId;
    g_previewWindow.lastSourceEncodedText = std::move(encodedText);
}

LRESULT CALLBACK previewWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_previewWindow.windowHandle = window;
        g_previewWindow.contentHandle = ::CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0,
            0,
            0,
            0,
            window,
            nullptr,
            g_instance,
            nullptr);
        if (g_previewWindow.contentHandle == nullptr) {
            return -1;
        }

        ::SendMessageW(g_previewWindow.contentHandle, EM_LIMITTEXT, 0x7FFFFFFE, 0);
        g_previewWindow.contentFont = createPreviewFont();
        if (g_previewWindow.contentFont != nullptr) {
            ::SendMessageW(g_previewWindow.contentHandle, WM_SETFONT, reinterpret_cast<WPARAM>(g_previewWindow.contentFont), TRUE);
        } else {
            applyDefaultGuiFont(g_previewWindow.contentHandle);
        }

        ::SetTimer(window, kPreviewRefreshTimerId, kPreviewRefreshIntervalMs, nullptr);
        return 0;
    }
    case WM_SIZE: {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        if (g_previewWindow.contentHandle != nullptr) {
            ::MoveWindow(
                g_previewWindow.contentHandle,
                8,
                8,
                width > 16 ? width - 16 : 0,
                height > 16 ? height - 16 : 0,
                TRUE);
        }
        return 0;
    }
    case WM_SETFOCUS:
        if (g_previewWindow.contentHandle != nullptr) {
            ::SetFocus(g_previewWindow.contentHandle);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == kPreviewRefreshTimerId) {
            refreshPreviewWindowContent(false);
            return 0;
        }
        break;
    case WM_CLOSE:
        ::SendMessage(g_nppData._nppHandle, NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(window));
        g_previewWindow.visible = false;
        g_previewWindow.mode = ViewMode::None;
        invalidatePreviewSnapshot();
        setShortViewChecked(false);
        return 0;
    case WM_DESTROY:
        ::KillTimer(window, kPreviewRefreshTimerId);
        if (g_previewWindow.contentFont != nullptr) {
            ::DeleteObject(g_previewWindow.contentFont);
        }
        g_previewWindow = {};
        setShortViewChecked(false);
        return 0;
    default:
        break;
    }

    return ::DefWindowProcW(window, message, wParam, lParam);
}

bool ensurePreviewWindow() {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = previewWndProc;
        windowClass.hInstance = g_instance;
        windowClass.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kPreviewWindowClass;
        if (::RegisterClassW(&windowClass) == 0) {
            return false;
        }
        classRegistered = true;
    }

    if (g_previewWindow.windowHandle == nullptr) {
        HWND previewWindow = ::CreateWindowExW(
            0,
            kPreviewWindowClass,
            kPreviewPaneName,
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0,
            0,
            0,
            0,
            g_nppData._nppHandle,
            nullptr,
            g_instance,
            nullptr);

        if (previewWindow == nullptr) {
            return false;
        }

        g_previewWindow.dockData.hClient = previewWindow;
        g_previewWindow.dockData.pszName = kPreviewPaneName;
        g_previewWindow.dockData.dlgID = g_commands[ShortViewCommand]._cmdID;
        g_previewWindow.dockData.uMask = DWS_DF_CONT_BOTTOM | DWS_ICONTAB;
        g_previewWindow.dockData.hIconTab = nullptr;
        g_previewWindow.dockData.pszAddInfo = kPreviewPaneInfo;
        g_previewWindow.dockData.iPrevCont = 0;
        g_previewWindow.dockData.pszModuleName = kPluginModuleName;

        if (::SendMessage(g_nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&g_previewWindow.dockData)) == 0) {
            ::DestroyWindow(previewWindow);
            return false;
        }

        g_previewWindow.dockRegistered = true;
    }

    if (!g_previewWindow.visible) {
        ::SendMessage(g_nppData._nppHandle, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(g_previewWindow.windowHandle));
        g_previewWindow.visible = true;
    }

    ::SendMessage(g_nppData._nppHandle, NPPM_DMMUPDATEDISPINFO, 0, reinterpret_cast<LPARAM>(g_previewWindow.windowHandle));
    return true;
}

void closePreviewWindow() {
    if (g_previewWindow.windowHandle != nullptr && g_previewWindow.visible) {
        ::SendMessage(g_nppData._nppHandle, NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(g_previewWindow.windowHandle));
        g_previewWindow.visible = false;
        g_previewWindow.mode = ViewMode::None;
        invalidatePreviewSnapshot();
    }

    setShortViewChecked(false);
}

void destroyPreviewWindow() {
    if (g_previewWindow.windowHandle != nullptr) {
        ::DestroyWindow(g_previewWindow.windowHandle);
    }
}

void applyDefaultGuiFont(HWND control) {
    ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

POINT getDropdownAnchorPoint() {
    POINT cursorPoint{};
    ::GetCursorPos(&cursorPoint);
    cursorPoint.y += 18;
    return cursorPoint;
}

POINT clampPopupOrigin(POINT desiredOrigin, int popupWidth, int popupHeight) {
    HMONITOR monitor = ::MonitorFromPoint(desiredOrigin, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!::GetMonitorInfoW(monitor, &monitorInfo)) {
        return desiredOrigin;
    }

    POINT origin = desiredOrigin;
    const RECT& workArea = monitorInfo.rcWork;
    if (origin.x + popupWidth > workArea.right) {
        origin.x = workArea.right - popupWidth;
    }
    if (origin.y + popupHeight > workArea.bottom) {
        origin.y = workArea.bottom - popupHeight;
    }
    if (origin.x < workArea.left) {
        origin.x = workArea.left;
    }
    if (origin.y < workArea.top) {
        origin.y = workArea.top;
    }
    return origin;
}

bool hasAnySelectedLogLevel(const std::array<bool, 4>& selectedLevels) {
    for (bool selected : selectedLevels) {
        if (selected) {
            return true;
        }
    }
    return false;
}

bool matchesSelectedLogLevel(int level, const std::array<bool, 4>& selectedLevels) {
    for (size_t index = 0; index < kSelectableLogLevels.size(); ++index) {
        if (selectedLevels[index] && level == kSelectableLogLevels[index]) {
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK logLevelPromptWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* state = reinterpret_cast<LogLevelPromptState*>(createStruct->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    auto* state = reinterpret_cast<LogLevelPromptState*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state == nullptr) {
        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    if (message == WM_CREATE) {
        HWND label = ::CreateWindowExW(0, L"STATIC", L"Show only these levels:",
            WS_CHILD | WS_VISIBLE, 12, 14, 90, 18, window, nullptr, g_instance, nullptr);
        for (size_t index = 0; index < kSelectableLogLevelLabels.size(); ++index) {
            const int top = 40 + static_cast<int>(index) * 24;
            state->checkboxHandles[index] = ::CreateWindowExW(0, L"BUTTON", kSelectableLogLevelLabels[index],
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                20, top, 120, 20, window, nullptr, g_instance, nullptr);
            ::SendMessageW(state->checkboxHandles[index], BM_SETCHECK, state->selectedLevels[index] ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        HWND hint = ::CreateWindowExW(0, L"STATIC", L"DEBUG=20, INFO=30, WARN=40, ERROR=50",
            WS_CHILD | WS_VISIBLE, 12, 142, 296, 18, window, nullptr, g_instance, nullptr);
        HWND applyButton = ::CreateWindowExW(0, L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            156, 170, 70, 24, window, reinterpret_cast<HMENU>(IDOK), g_instance, nullptr);
        HWND cancelButton = ::CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            238, 170, 70, 24, window, reinterpret_cast<HMENU>(IDCANCEL), g_instance, nullptr);

        applyDefaultGuiFont(label);
        for (HWND checkboxHandle : state->checkboxHandles) {
            applyDefaultGuiFont(checkboxHandle);
        }
        applyDefaultGuiFont(hint);
        applyDefaultGuiFont(applyButton);
        applyDefaultGuiFont(cancelButton);

        ::SetFocus(state->checkboxHandles[0]);
        return 0;
    }

    if (message == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case IDOK: {
            for (size_t index = 0; index < state->checkboxHandles.size(); ++index) {
                state->selectedLevels[index] = ::SendMessageW(state->checkboxHandles[index], BM_GETCHECK, 0, 0) == BST_CHECKED;
            }
            state->accepted = true;
            state->finished = true;
            ::DestroyWindow(window);
            return 0;
        }
        case IDCANCEL:
            state->finished = true;
            ::DestroyWindow(window);
            return 0;
        default:
            break;
        }
    }

    if (message == WM_CLOSE) {
        state->finished = true;
        ::DestroyWindow(window);
        return 0;
    }

    return ::DefWindowProcW(window, message, wParam, lParam);
}

bool promptForLogLevels(std::array<bool, 4>& selectedLevels) {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = logLevelPromptWndProc;
        windowClass.hInstance = g_instance;
        windowClass.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kLogLevelPromptWindowClass;
        if (::RegisterClassW(&windowClass) == 0) {
            return false;
        }
        classRegistered = true;
    }

    LogLevelPromptState state{};
    state.selectedLevels = selectedLevels;
    constexpr int kPopupWidth = 332;
    constexpr int kPopupHeight = 240;
    POINT popupOrigin = clampPopupOrigin(getDropdownAnchorPoint(), kPopupWidth, kPopupHeight);
    HWND promptWindow = ::CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kLogLevelPromptWindowClass,
        L"Filter Log Levels",
        WS_POPUP | WS_BORDER,
        popupOrigin.x,
        popupOrigin.y,
        kPopupWidth,
        kPopupHeight,
        g_nppData._nppHandle,
        nullptr,
        g_instance,
        &state);

    if (promptWindow == nullptr) {
        return false;
    }

    ::EnableWindow(g_nppData._nppHandle, FALSE);
    ::ShowWindow(promptWindow, SW_SHOWNOACTIVATE);
    ::SetWindowPos(promptWindow, HWND_TOPMOST, popupOrigin.x, popupOrigin.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::UpdateWindow(promptWindow);

    MSG message{};
    while (!state.finished && ::GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (::IsDialogMessageW(promptWindow, &message)) {
            continue;
        }
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    ::EnableWindow(g_nppData._nppHandle, TRUE);
    ::SetActiveWindow(g_nppData._nppHandle);

    if (!state.accepted) {
        return false;
    }

    selectedLevels = state.selectedLevels;
    return true;
}

void showInfoMessage(const wchar_t* text, const wchar_t* title) {
    ::MessageBoxW(g_nppData._nppHandle, text, title, MB_OK | MB_ICONINFORMATION);
}

bool showPreviewWindow(ViewMode mode, const std::array<bool, 4>& selectedLevels) {
    g_previewWindow.mode = mode;
    g_previewWindow.selectedLevels = selectedLevels;
    invalidatePreviewSnapshot();

    if (!ensurePreviewWindow()) {
        g_previewWindow.mode = ViewMode::None;
        showInfoMessage(L"Unable to create the Bunyan preview pane.", L"Bunyan Log Viewer");
        return false;
    }

    g_previewWindow.visible = true;
    updatePreviewWindowTitle();
    refreshPreviewWindowContent(true);
    setShortViewChecked(mode == ViewMode::Short);
    return true;
}

void showShortLogView() {
    if (g_previewWindow.windowHandle != nullptr && g_previewWindow.mode == ViewMode::Short) {
        closePreviewWindow();
        return;
    }

    showPreviewWindow(ViewMode::Short, g_lastSelectedLogLevels);
}

void showSelectedLogLevel() {
    auto selectedLevels = g_lastSelectedLogLevels;
    if (!promptForLogLevels(selectedLevels)) {
        return;
    }

    if (!hasAnySelectedLogLevel(selectedLevels)) {
        showInfoMessage(L"Select at least one log level: DEBUG, INFO, WARN, or ERROR.", L"Bunyan Log Viewer");
        return;
    }

    g_lastSelectedLogLevels = selectedLevels;

    showPreviewWindow(ViewMode::LevelFilter, selectedLevels);
}

void resetLogView() {
    if (g_previewWindow.windowHandle == nullptr || !g_previewWindow.visible) {
        showInfoMessage(L"There is no preview window open.", L"Bunyan Log Viewer");
        return;
    }

    closePreviewWindow();
}

void showAbout() {
    std::wstring message;
    message.reserve(384);
    message += L"Bunyan Log Viewer\n";
    message += L"Version: ";
    message += kPluginVersion;
    message += L"\n";
    message += L"Author: ";
    message += kPluginAuthor;
    message += L"\n\n";
    message += L"Features:\n";
    message += L"- Open a read-only short-log pane docked at the bottom of Notepad++\n";
    message += L"- Filter DEBUG, INFO, WARN, and ERROR lines in the same pane\n";
    message += L"- Refresh the pane automatically while it stays open";

    showInfoMessage(message.c_str(), L"About Bunyan Log Viewer");
}

void configureCommand(FuncItem& item, const TCHAR* name, PFUNCPLUGINCMD command) {
    lstrcpyn(item._itemName, name, static_cast<int>(std::size(item._itemName)));
    item._pFunc = command;
    item._cmdID = 0;
    item._init2Check = false;
    item._pShKey = nullptr;
}

void initialiseCommands() {
    configureCommand(g_commands[ShortViewCommand], TEXT("Toggle Short Preview"), showShortLogView);
    configureCommand(g_commands[LogLevelCommand], TEXT("Filter Log Levels..."), showSelectedLogLevel);
    configureCommand(g_commands[ResetCommand], TEXT("Close Preview"), resetLogView);
    configureCommand(g_commands[AboutCommand], TEXT("About"), showAbout);
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = module;
        initialiseCommands();
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData notepadPlusData) {
    g_nppData = notepadPlusData;
}

extern "C" __declspec(dllexport) const TCHAR* getName() {
    return kPluginName;
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) {
    if (count != nullptr) {
        *count = static_cast<int>(g_commands.size());
    }
    return g_commands.data();
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notifyCode) {
    if (notifyCode == nullptr) {
        return;
    }

    switch (notifyCode->nmhdr.code) {
    case NPPN_TBMODIFICATION:
        registerToolbarIcon();
        break;
    case NPPN_BUFFERACTIVATED:
        if (g_previewWindow.windowHandle != nullptr && g_previewWindow.visible) {
            invalidatePreviewSnapshot();
            refreshPreviewWindowContent(true);
        }
        setShortViewChecked(g_previewWindow.visible && g_previewWindow.mode == ViewMode::Short);
        break;
    case NPPN_FILEBEFORECLOSE:
        if (static_cast<UINT_PTR>(notifyCode->nmhdr.idFrom) == g_previewWindow.lastBufferId) {
            invalidatePreviewSnapshot();
        }
        break;
    case NPPN_SHUTDOWN:
        destroyPreviewWindow();
        cleanupToolbarIcons();
        break;
    default:
        break;
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) {
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() {
    return TRUE;
}