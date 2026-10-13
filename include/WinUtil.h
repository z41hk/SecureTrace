#pragma once

#ifdef _WIN32

#include <algorithm>
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>

inline std::wstring toWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &out[0], n);
    if (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
}

inline std::string toUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &out[0], n, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
}

inline std::string getText(HWND hwnd) {
    const int n = GetWindowTextLengthW(hwnd);
    std::wstring buf(static_cast<std::size_t>(n) + 1, L'\0');
    GetWindowTextW(hwnd, &buf[0], n + 1);
    buf.resize(static_cast<std::size_t>(n));
    return toUtf8(buf);
}

inline void setText(HWND hwnd, const std::string& value) {
    SetWindowTextW(hwnd, toWide(value).c_str());
}

inline HWND addButton(HWND parent, int id, const wchar_t* label, int x, int y, int w, int h) {
    return CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                         x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                         GetModuleHandleW(nullptr), nullptr);
}

inline HWND addEdit(HWND parent, int id, int x, int y, int w, int h, bool password) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    if (password) {
        style |= ES_PASSWORD;
    }
    return CreateWindowW(L"EDIT", L"", style, x, y, w, h, parent,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                         GetModuleHandleW(nullptr), nullptr);
}

inline HWND addStatic(HWND parent, const wchar_t* label, int x, int y, int w, int h) {
    return CreateWindowW(L"STATIC", label, WS_CHILD | WS_VISIBLE,
                         x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

#ifndef WC_COMBOBOXW
#define WC_COMBOBOXW L"ComboBox"
#endif

inline HWND addCombo(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                         x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                         GetModuleHandleW(nullptr), nullptr);
}

inline void comboAdd(HWND combo, const std::vector<std::string>& items) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& item : items) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(toWide(item).c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

inline std::string comboText(HWND combo) {
    const int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (sel < 0) {
        return {};
    }
    wchar_t buf[256];
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(buf));
    return toUtf8(buf);
}

inline HWND addList(HWND parent, int id, int x, int y, int w, int h) {
    HWND list = CreateWindowW(WC_LISTVIEWW, L"",
                              WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                              x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                              GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    return list;
}

inline void listClear(HWND list) {
    ListView_DeleteAllItems(list);
    while (ListView_DeleteColumn(list, 0)) {
    }
}

inline void listHeaders(HWND list, const std::vector<std::string>& headers) {
    listClear(list);
    RECT rc{};
    GetClientRect(list, &rc);
    const int colW = std::max(90, static_cast<int>(rc.right / std::max<std::size_t>(1, headers.size())));
    for (int i = 0; i < static_cast<int>(headers.size()); ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        std::wstring text = toWide(headers[static_cast<std::size_t>(i)]);
        col.pszText = text.data();
        col.cx = colW;
        col.iSubItem = i;
        ListView_InsertColumn(list, i, &col);
    }
}

inline void listAddRow(HWND list, const std::vector<std::string>& row) {
    if (row.empty()) {
        return;
    }
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(list);
    std::wstring first = toWide(row[0]);
    item.pszText = first.data();
    const int index = ListView_InsertItem(list, &item);
    for (int i = 1; i < static_cast<int>(row.size()); ++i) {
        std::wstring cell = toWide(row[static_cast<std::size_t>(i)]);
        ListView_SetItemText(list, index, i, cell.data());
    }
}

inline int listSelected(HWND list) {
    return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

inline std::string listCell(HWND list, int row, int col) {
    wchar_t buf[512];
    ListView_GetItemText(list, row, col, buf, 512);
    return toUtf8(buf);
}

inline void infoBox(HWND parent, const std::string& text) {
    MessageBoxW(parent, toWide(text).c_str(), L"SecureTrace", MB_OK | MB_ICONINFORMATION);
}

inline void errorBox(HWND parent, const std::string& text) {
    MessageBoxW(parent, toWide(text).c_str(), L"SecureTrace", MB_OK | MB_ICONERROR);
}

inline bool askBox(HWND parent, const std::string& text) {
    return MessageBoxW(parent, toWide(text).c_str(), L"SecureTrace",
                       MB_YESNO | MB_ICONQUESTION) == IDYES;
}

#endif
