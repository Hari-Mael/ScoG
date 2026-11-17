#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ui_theme.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using std::string;
using std::vector;
using std::wstring;
using std::to_wstring;
using std::wostringstream;
using std::ifstream;
using std::ofstream;

namespace {

constexpr int kSidebarWidth = 230;
constexpr int kTopbarHeight = 48;
constexpr int kMargin = 10;
constexpr int kActionButtonWidth = 150;
constexpr int kActionSpacing = 160;
constexpr int kFieldSpacing = 56;
constexpr DWORD kPbkdfIterations = 200000;
constexpr size_t kMinPasswordLength = 8;
constexpr int kMaxLoginAttempts = 5;
constexpr ULONGLONG kLockDurationMs = 30000;
constexpr wchar_t kPasswordChar = L'\x2022';

enum ControlId {
    ID_TOGGLE = 2001,
    ID_CYCLE,
    ID_HOME,
    ID_ADMIN,
    ID_LOGOUT = 2999,
    ID_MENU_BASE = 3000,
    ID_ACT_ELEVE = 4001,
    ID_ACT_CLASSE,
    ID_ACT_PAIEMENT,
    ID_ACT_PARAMS,
    ID_EYE = 5001,
    ID_LOGIN,
    ID_FIELD_BASE = 100
};

struct Settings {
    wstring appName;
    wstring appLogo;
    wstring ecole;
    wstring matricule;
    wstring regime;
    wstring devise;
    wstring classesEcole = L"CP1, CP2, CE, CM1, CM2";
    wstring classesCollege = L"6eme, 5eme, 4eme, 3eme";
    wstring classesLycee = L"Seconde, Premiere, Terminale";
    wstring adminUser;
    wstring adminSalt;
    wstring adminHash;
};

struct Eleve {
    long long id;
    wstring nom;
    wstring classe;
    wstring email;
    wstring sexe;
    wstring cycle;
};

struct Classe {
    long long id;
    wstring nom;
    wstring annee;
    wstring cycle;
};

struct Paiement {
    wstring ref;
    wstring eleve;
    wstring classe;
    wstring type;
    wstring cycle;
    wstring statut;
    long long montant;
    long long reste;
};

struct DlgField {
    wstring label;
    wstring value;
    bool password;
};

struct DialogState {
    vector<DlgField>* fields;
    bool ok;
};

struct PageDef {
    const wchar_t* key;
    const wchar_t* label;
};

const PageDef kPages[] = {
    {L"dashboard", L"Tableau de bord"},
    {L"portal", L"Compte portail"},
    {L"cycles", L"Cycles scolaires"},
    {L"niveaux", L"Niveaux scolaires"},
    {L"salles", L"Salles"},
    {L"periodes", L"Periodes academiques"},
    {L"calendrier", L"Calendrier scolaire"},
    {L"classes", L"Classes"},
    {L"eleves", L"Eleves"},
    {L"inscriptions", L"Inscriptions"},
    {L"parents", L"Parents / Tuteurs"},
    {L"presences", L"Presences"},
    {L"discipline", L"Discipline"},
    {L"finance", L"Finance"},
    {L"etats", L"Etats financiers"},
    {L"paiements", L"Demandes de paiement"},
    {L"typesFrais", L"Types de frais"},
    {L"carte", L"Fiche eleve + Carte"},
    {L"certificats", L"Certificats"},
    {L"users", L"Utilisateurs"},
    {L"parametres", L"Parametres"}
};
constexpr int kPageCount = static_cast<int>(sizeof(kPages) / sizeof(kPages[0]));

Settings gS;
wstring gCycle = L"ecole";
bool gSidebarVisible = true;
vector<Eleve> gEleves;
vector<Classe> gClasses;
vector<Paiement> gPaiements;
long long gNextId = 1000;
wstring gCurrentPage = L"dashboard";
HINSTANCE gInstance = nullptr;
HFONT gFont = nullptr;

HWND gMain = nullptr;
HWND gTitle = nullptr;
HWND gTopLabel = nullptr;
HWND gTopCycle = nullptr;
HWND gHome = nullptr;
HWND gBell = nullptr;
HWND gAdminBtn = nullptr;
HWND gToggle = nullptr;
HWND gContent = nullptr;
HWND gLogout = nullptr;
HWND gLoginUser = nullptr;
HWND gLoginPass = nullptr;
HWND gLoginEye = nullptr;
HWND gLoginMsg = nullptr;
vector<HWND> gMenuBtns;
vector<HWND> gActionBtns;

bool gLoggedIn = false;
bool gLogoutRequested = false;
bool gPasswordHidden = true;
int gFailedAttempts = 0;
ULONGLONG gLockedUntil = 0;

HWND CreateCtl(LPCWSTR cls, LPCWSTR text, DWORD style, int x, int y, int w, int h, HWND parent, int id) {
    HWND ctl = CreateWindowExW(0, cls, text, style, x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), gInstance, nullptr);
    if (ctl && gFont) SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
    return ctl;
}

HWND CreateFixedWindow(LPCWSTR cls, LPCWSTR title, int clientW, int clientH, DWORD style, DWORD exStyle, HWND parent, void* param) {
    RECT rc{ 0, 0, clientW, clientH };
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    return CreateWindowExW(exStyle, cls, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top, parent, nullptr, gInstance, param);
}

bool RegisterWindowClass(LPCWSTR name, WNDPROC proc, HBRUSH background) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = gInstance;
    wc.lpszClassName = name;
    wc.hbrBackground = background;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    return RegisterClassW(&wc) != 0;
}

wstring ReadWindowText(HWND h) {
    const int len = GetWindowTextLengthW(h);
    vector<wchar_t> buf(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(h, buf.data(), len + 1);
    return wstring(buf.data());
}

void ShowWarning(HWND owner, const wchar_t* text) {
    MessageBoxW(owner, text, L"Gestion scolaire", MB_OK | MB_ICONWARNING);
}

wstring Trim(const wstring& s) {
    const size_t b = s.find_first_not_of(L" \t");
    if (b == wstring::npos) return L"";
    const size_t e = s.find_last_not_of(L" \t");
    return s.substr(b, e - b + 1);
}

vector<wstring> SplitComma(const wstring& s) {
    vector<wstring> out;
    wstring cur;
    for (wchar_t c : s) {
        if (c == L',') {
            cur = Trim(cur);
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    cur = Trim(cur);
    if (!cur.empty()) out.push_back(cur);
    return out;
}

wstring JoinList(const vector<wstring>& items, const wstring& sep) {
    wstring r;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) r += sep;
        r += items[i];
    }
    return r;
}

wstring CycleLabel(const wstring& c) {
    if (c == L"college") return L"Collège";
    if (c == L"lycee") return L"Lycée";
    return L"École";
}

int CycleIndex() {
    if (gCycle == L"college") return 1;
    if (gCycle == L"lycee") return 2;
    return 0;
}

vector<wstring> ClassesForCycle(const wstring& c) {
    if (c == L"college") return SplitComma(gS.classesCollege);
    if (c == L"lycee") return SplitComma(gS.classesLycee);
    return SplitComma(gS.classesEcole);
}

wstring PageLabel(const wstring& key) {
    for (const auto& p : kPages) if (key == p.key) return p.label;
    return key;
}

string WideToUtf8(const wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

wstring Utf8ToWide(const string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

wstring ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    const wstring p(buf);
    return p.substr(0, p.find_last_of(L"\\/") + 1);
}

wstring DataDir() {
    const wstring portable = ExeDir();
    if (GetFileAttributesW((portable + L"Gestion.ini").c_str()) != INVALID_FILE_ATTRIBUTES) return portable;
    wchar_t appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) return portable;
    const wstring dir = wstring(appData) + L"\\GestionScolaire\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

wstring IniPath() { return DataDir() + L"Gestion.ini"; }
wstring DataPath() { return DataDir() + L"donnees.txt"; }

wstring ReadIni(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[2048];
    GetPrivateProfileStringW(L"CONFIG", key, def, buf, 2048, IniPath().c_str());
    return buf;
}

void WriteIni(const wchar_t* key, const wstring& value) {
    WritePrivateProfileStringW(L"CONFIG", key, value.c_str(), IniPath().c_str());
}

void LoadSettings() {
    gS.appName = ReadIni(L"appName", L"");
    gS.appLogo = ReadIni(L"appLogo", L"");
    gS.ecole = ReadIni(L"ecole", L"");
    gS.matricule = ReadIni(L"matricule", L"");
    gS.regime = ReadIni(L"regime", L"");
    gS.devise = ReadIni(L"devise", L"");
    gS.classesEcole = ReadIni(L"classesEcole", L"CP1, CP2, CE, CM1, CM2");
    gS.classesCollege = ReadIni(L"classesCollege", L"6eme, 5eme, 4eme, 3eme");
    gS.classesLycee = ReadIni(L"classesLycee", L"Seconde, Premiere, Terminale");
    gS.adminUser = ReadIni(L"adminUser", L"");
    gS.adminSalt = ReadIni(L"adminSalt", L"");
    gS.adminHash = ReadIni(L"adminHash", L"");
    gCycle = ReadIni(L"cycle", L"ecole");
    if (gCycle != L"ecole" && gCycle != L"college" && gCycle != L"lycee") gCycle = L"ecole";
}

void SaveSettings() {
    WriteIni(L"appName", gS.appName);
    WriteIni(L"appLogo", gS.appLogo);
    WriteIni(L"ecole", gS.ecole);
    WriteIni(L"matricule", gS.matricule);
    WriteIni(L"regime", gS.regime);
    WriteIni(L"devise", gS.devise);
    WriteIni(L"classesEcole", gS.classesEcole);
    WriteIni(L"classesCollege", gS.classesCollege);
    WriteIni(L"classesLycee", gS.classesLycee);
    WriteIni(L"adminUser", gS.adminUser);
    WriteIni(L"adminSalt", gS.adminSalt);
    WriteIni(L"adminHash", gS.adminHash);
    WriteIni(L"cycle", gCycle);
}

wstring EscapeField(const wstring& s) {
    wstring out;
    for (wchar_t c : s) {
        switch (c) {
            case L'\\': out += L"\\\\"; break;
            case L'|': out += L"\\p"; break;
            case L'\r': out += L"\\r"; break;
            case L'\n': out += L"\\n"; break;
            default: out += c;
        }
    }
    return out;
}

wstring UnescapeField(const wstring& s) {
    wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            const wchar_t n = s[++i];
            switch (n) {
                case L'p': out += L'|'; break;
                case L'r': out += L'\r'; break;
                case L'n': out += L'\n'; break;
                default: out += n;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

wstring JoinRecord(const vector<wstring>& fields) {
    wstring line;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) line += L'|';
        line += EscapeField(fields[i]);
    }
    return line;
}

vector<wstring> SplitRecord(const wstring& line) {
    vector<wstring> out;
    wstring cur;
    for (wchar_t c : line) {
        if (c == L'|') {
            out.push_back(UnescapeField(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(UnescapeField(cur));
    return out;
}

void BumpId(long long id) {
    gNextId = std::max(gNextId, id + 1);
}

void SaveData() {
    const wstring path = DataPath();
    const wstring tmp = path + L".tmp";
    {
        ofstream out(std::filesystem::path(tmp), std::ios::binary | std::ios::trunc);
        if (!out) return;
        for (const auto& e : gEleves)
            out << WideToUtf8(JoinRecord({ L"E", to_wstring(e.id), e.nom, e.classe, e.email, e.sexe, e.cycle })) << '\n';
        for (const auto& c : gClasses)
            out << WideToUtf8(JoinRecord({ L"C", to_wstring(c.id), c.nom, c.annee, c.cycle })) << '\n';
        for (const auto& p : gPaiements)
            out << WideToUtf8(JoinRecord({ L"P", p.ref, p.eleve, p.classe, p.type, p.cycle,
                to_wstring(p.montant), to_wstring(p.reste), p.statut })) << '\n';
        out.flush();
        if (!out.good()) {
            out.close();
            DeleteFileW(tmp.c_str());
            return;
        }
    }
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

void LoadData() {
    ifstream in(std::filesystem::path(DataPath()), std::ios::binary);
    if (!in) return;
    string raw;
    while (std::getline(in, raw)) {
        wstring line = Utf8ToWide(raw);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const vector<wstring> f = SplitRecord(line);
        if (f.empty()) continue;
        if (f[0] == L"E" && f.size() >= 7) {
            Eleve e{ wcstoll(f[1].c_str(), nullptr, 10), f[2], f[3], f[4], f[5], f[6] };
            BumpId(e.id);
            gEleves.push_back(e);
        } else if (f[0] == L"C" && f.size() >= 5) {
            Classe c{ wcstoll(f[1].c_str(), nullptr, 10), f[2], f[3], f[4] };
            BumpId(c.id);
            gClasses.push_back(c);
        } else if (f[0] == L"P" && f.size() >= 9) {
            Paiement p{ f[1], f[2], f[3], f[4], f[5], f[8],
                wcstoll(f[6].c_str(), nullptr, 10), wcstoll(f[7].c_str(), nullptr, 10) };
            gPaiements.push_back(p);
        }
    }
}

bool RandomBytes(vector<BYTE>& out) {
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(out.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

wstring ToHex(const vector<BYTE>& bytes) {
    static const wchar_t digits[] = L"0123456789abcdef";
    wstring s;
    for (BYTE b : bytes) {
        s += digits[b >> 4];
        s += digits[b & 0x0F];
    }
    return s;
}

vector<BYTE> FromHex(const wstring& h) {
    vector<BYTE> out;
    for (size_t i = 0; i + 1 < h.size(); i += 2)
        out.push_back(static_cast<BYTE>(wcstoul(h.substr(i, 2).c_str(), nullptr, 16)));
    return out;
}

bool DeriveHash(const wstring& password, const vector<BYTE>& salt, vector<BYTE>& outHash) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
        return false;
    const string pwd = WideToUtf8(password);
    outHash.assign(32, 0);
    const NTSTATUS st = BCryptDeriveKeyPBKDF2(alg,
        reinterpret_cast<PUCHAR>(const_cast<char*>(pwd.data())), static_cast<ULONG>(pwd.size()),
        reinterpret_cast<PUCHAR>(const_cast<BYTE*>(salt.data())), static_cast<ULONG>(salt.size()),
        kPbkdfIterations, outHash.data(), static_cast<ULONG>(outHash.size()), 0);
    BCryptCloseAlgorithmProvider(alg, 0);
    return BCRYPT_SUCCESS(st);
}

bool HasAccount() {
    return !gS.adminUser.empty() && !gS.adminSalt.empty() && !gS.adminHash.empty();
}

bool StoreAccount(const wstring& user, const wstring& password) {
    vector<BYTE> salt(16);
    vector<BYTE> hash;
    if (!RandomBytes(salt) || !DeriveHash(password, salt, hash)) return false;
    gS.adminUser = user;
    gS.adminSalt = ToHex(salt);
    gS.adminHash = ToHex(hash);
    SaveSettings();
    return true;
}

bool VerifyAccount(const wstring& user, const wstring& password) {
    if (!HasAccount()) return false;
    vector<BYTE> hash;
    if (!DeriveHash(password, FromHex(gS.adminSalt), hash)) return false;
    const bool userOk = (user == gS.adminUser);
    const bool passOk = (ToHex(hash) == gS.adminHash);
    return userOk && passOk;
}

void PumpUntilClosed(HWND window, int enterId, int escId) {
    MSG msg;
    while (IsWindow(window) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && enterId) {
            PostMessageW(window, WM_COMMAND, enterId, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && escId) {
            PostMessageW(window, WM_COMMAND, escId, 0);
            continue;
        }
        if (IsDialogMessageW(window, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK InputDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        auto* st = static_cast<DialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        int y = 16;
        for (size_t i = 0; i < st->fields->size(); ++i) {
            const DlgField& f = (*st->fields)[i];
            CreateCtl(L"STATIC", f.label.c_str(), WS_CHILD | WS_VISIBLE, 20, y, 340, 18, hwnd, 0);
            DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
            if (f.password) style |= ES_PASSWORD;
            HWND edit = CreateCtl(L"EDIT", f.value.c_str(), style, 20, y + 20, 340, 26, hwnd, ID_FIELD_BASE + static_cast<int>(i));
            if (f.password && edit) SendMessageW(edit, EM_SETPASSWORDCHAR, kPasswordChar, 0);
            y += kFieldSpacing;
        }
        CreateCtl(L"BUTTON", L"Enregistrer", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 120, y, 110, 32, hwnd, IDOK);
        CreateCtl(L"BUTTON", L"Annuler", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 240, y, 110, 32, hwnd, IDCANCEL);
        return 0;
    }
    case WM_COMMAND: {
        auto* st = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        const int id = LOWORD(wParam);
        if (id == IDOK && st) {
            for (size_t i = 0; i < st->fields->size(); ++i)
                (*st->fields)[i].value = ReadWindowText(GetDlgItem(hwnd, ID_FIELD_BASE + static_cast<int>(i)));
            st->ok = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool InputDialog(HWND owner, LPCWSTR title, vector<DlgField>& fields) {
    DialogState st{ &fields, false };
    const int clientH = 16 + static_cast<int>(fields.size()) * kFieldSpacing + 32 + 16;
    HWND dlg = CreateFixedWindow(L"GS_Dialog", title, 380, clientH,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME, owner, &st);
    if (!dlg) return false;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(GetDlgItem(dlg, ID_FIELD_BASE));
    PumpUntilClosed(dlg, IDOK, IDCANCEL);
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return st.ok;
}

bool EnsureAccount(HWND owner) {
    while (!HasAccount()) {
        vector<DlgField> fields{
            { L"Identifiant administrateur", L"", false },
            { L"Mot de passe (" + to_wstring(kMinPasswordLength) + L" caracteres minimum)", L"", true },
            { L"Confirmer le mot de passe", L"", true }
        };
        if (!InputDialog(owner, L"Creation du compte administrateur", fields)) return false;
        const wstring user = Trim(fields[0].value);
        if (user.empty()) {
            ShowWarning(owner, L"L'identifiant est obligatoire.");
            continue;
        }
        if (fields[1].value.size() < kMinPasswordLength) {
            ShowWarning(owner, L"Le mot de passe doit contenir au moins 8 caracteres.");
            continue;
        }
        if (fields[1].value != fields[2].value) {
            ShowWarning(owner, L"Les deux mots de passe ne correspondent pas.");
            continue;
        }
        if (!StoreAccount(user, fields[1].value)) {
            ShowWarning(owner, L"Erreur cryptographique : impossible de creer le compte.");
            return false;
        }
    }
    return true;
}

void HandleLogin(HWND hwnd) {
    const ULONGLONG now = GetTickCount64();
    if (now < gLockedUntil) {
        const ULONGLONG seconds = (gLockedUntil - now + 999) / 1000;
        SetWindowTextW(gLoginMsg, (L"Trop de tentatives. Reessayez dans " + to_wstring(seconds) + L" s.").c_str());
        return;
    }
    const wstring user = Trim(ReadWindowText(gLoginUser));
    const wstring pass = ReadWindowText(gLoginPass);
    if (VerifyAccount(user, pass)) {
        gLoggedIn = true;
        DestroyWindow(hwnd);
        return;
    }
    SetWindowTextW(gLoginPass, L"");
    SetFocus(gLoginPass);
    if (++gFailedAttempts >= kMaxLoginAttempts) {
        gFailedAttempts = 0;
        gLockedUntil = now + kLockDurationMs;
        SetWindowTextW(gLoginMsg, L"Trop de tentatives. Compte verrouille 30 secondes.");
    } else {
        SetWindowTextW(gLoginMsg, L"Identifiant ou mot de passe incorrect.");
    }
}

LRESULT CALLBACK LoginProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateCtl(L"STATIC", L"Identifiant", WS_CHILD | WS_VISIBLE, 20, 20, 340, 18, hwnd, 0);
        gLoginUser = CreateCtl(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 20, 42, 340, 26, hwnd, 0);
        CreateCtl(L"STATIC", L"Mot de passe", WS_CHILD | WS_VISIBLE, 20, 80, 340, 18, hwnd, 0);
        gLoginPass = CreateCtl(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, 20, 102, 280, 26, hwnd, 0);
        SendMessageW(gLoginPass, EM_SETPASSWORDCHAR, kPasswordChar, 0);
        gPasswordHidden = true;
        gLoginEye = CreateCtl(L"BUTTON", L"Voir", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 306, 102, 54, 26, hwnd, ID_EYE);
        gLoginMsg = CreateCtl(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 140, 340, 36, hwnd, 0);
        CreateCtl(L"BUTTON", L"Se connecter", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 20, 190, 340, 36, hwnd, ID_LOGIN);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::RectF full(0.0f, 0.0f, static_cast<float>(rc.right), static_cast<float>(rc.bottom));
        Gdiplus::LinearGradientBrush bg(full, Gdiplus::Color(255, 232, 238, 255),
            Gdiplus::Color(255, 210, 222, 250), Gdiplus::LinearGradientModeVertical);
        g.FillRectangle(&bg, full);
        const Gdiplus::RectF card(12.0f, 12.0f, static_cast<float>(rc.right - 24), static_cast<float>(rc.bottom - 24));
        ui::FillRoundRect(g, card, 14.0f, ui::palette::Card());
        ui::StrokeRoundRect(g, card, 14.0f, ui::palette::Border());
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, ui::ToColorRef(ui::palette::TextMain()));
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        ui::DrawButton(item, item->CtlID == ID_LOGIN ? ui::ButtonKind::Primary : ui::ButtonKind::Secondary);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_EYE:
            gPasswordHidden = !gPasswordHidden;
            SendMessageW(gLoginPass, EM_SETPASSWORDCHAR, gPasswordHidden ? kPasswordChar : 0, 0);
            SetWindowTextW(gLoginEye, gPasswordHidden ? L"Voir" : L"Masquer");
            InvalidateRect(gLoginPass, nullptr, TRUE);
            SetFocus(gLoginPass);
            return 0;
        case ID_LOGIN:
            HandleLogin(hwnd);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool RunLogin() {
    gLoggedIn = false;
    HWND h = CreateFixedWindow(L"GS_Login", L"Connexion - Gestion scolaire", 380, 250,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, 0, nullptr, nullptr);
    if (!h) return false;
    ShowWindow(h, SW_SHOW);
    InvalidateRect(h, nullptr, TRUE);
    UpdateWindow(h);
    SetFocus(gLoginUser);
    PumpUntilClosed(h, ID_LOGIN, 0);
    return gLoggedIn;
}

long long CountElevesOfCycle() {
    long long n = 0;
    for (const auto& e : gEleves) if (e.cycle == gCycle) ++n;
    return n;
}

long long CountElevesInClasse(const wstring& classe) {
    long long n = 0;
    for (const auto& e : gEleves) if (e.cycle == gCycle && e.classe == classe) ++n;
    return n;
}

wstring BuildPageText() {
    wostringstream ss;
    const wstring separator(40, L'-');
    const wstring cycleName = CycleLabel(gCycle);
    const vector<wstring> classes = ClassesForCycle(gCycle);

    if (gCurrentPage == L"dashboard") {
        long long nbPaiements = 0;
        long long reste = 0;
        for (const auto& p : gPaiements) if (p.cycle == gCycle) { ++nbPaiements; reste += p.reste; }
        ss << L"TABLEAU DE BORD - " << cycleName << L"\r\n";
        ss << (gS.ecole.empty() ? wstring(L"Configurez l'etablissement dans Parametres.") : gS.ecole) << L"\r\n";
        ss << L"Matricule : " << gS.matricule << L" | Regime : " << gS.regime << L"\r\n";
        ss << separator << L"\r\n";
        ss << L"Eleves : " << CountElevesOfCycle() << L"\r\n";
        ss << L"Classes : " << classes.size() << L"\r\n";
        ss << L"Demandes de paiement : " << nbPaiements << L"\r\n";
        ss << L"Reste a percevoir : " << reste << L" " << gS.devise << L"\r\n";
        ss << L"\r\nRepartition par classe\r\n";
        for (const auto& c : classes) ss << L"  " << c << L" : " << CountElevesInClasse(c) << L" eleve(s)\r\n";
    } else if (gCurrentPage == L"eleves") {
        ss << L"ELEVES - Cycle : " << cycleName << L"\r\n" << separator << L"\r\n";
        bool any = false;
        for (const auto& e : gEleves) {
            if (e.cycle != gCycle) continue;
            any = true;
            ss << e.id << L" | " << e.nom << L" | " << e.classe << L" | " << e.email << L" | " << e.sexe << L"\r\n";
        }
        if (!any) ss << L"Aucun eleve. Utilisez + Nouvel eleve.\r\n";
    } else if (gCurrentPage == L"classes") {
        ss << L"CLASSES - Cycle : " << cycleName << L"\r\n" << separator << L"\r\n";
        bool any = false;
        for (const auto& c : gClasses) {
            if (c.cycle != gCycle) continue;
            any = true;
            ss << c.id << L" | " << c.nom << L" | " << c.annee << L" | " << CountElevesInClasse(c.nom) << L" eleve(s)\r\n";
        }
        if (!any) ss << L"Aucune classe creee. Utilisez + Nouvelle classe.\r\n";
    } else if (gCurrentPage == L"paiements") {
        ss << L"DEMANDES DE PAIEMENT - Cycle : " << cycleName << L"\r\n" << separator << L"\r\n";
        bool any = false;
        for (const auto& p : gPaiements) {
            if (p.cycle != gCycle) continue;
            any = true;
            ss << p.ref << L" | " << p.eleve << L" | " << p.classe << L" | " << p.type << L" | "
               << p.montant << L" " << gS.devise << L" | Reste " << p.reste << L" | " << p.statut << L"\r\n";
        }
        if (!any) ss << L"Aucune demande.\r\n";
    } else if (gCurrentPage == L"carte") {
        ss << L"CARTE ELEVE\r\n" << separator << L"\r\n";
        const Eleve* e = nullptr;
        for (const auto& x : gEleves) if (x.cycle == gCycle) { e = &x; break; }
        if (!e) {
            ss << L"Aucun eleve dans ce cycle.\r\n";
        } else {
            ss << (gS.ecole.empty() ? wstring(L"Etablissement") : gS.ecole) << L"\r\n";
            ss << L"Nom : " << e->nom << L"\r\nClasse : " << e->classe << L"\r\nSexe : " << e->sexe
               << L"\r\nEmail : " << e->email << L"\r\nMatricule : " << gS.matricule
               << L" | Regime : " << gS.regime << L"\r\n";
        }
    } else if (gCurrentPage == L"parametres") {
        ss << L"PARAMETRES\r\n" << separator << L"\r\n";
        ss << L"Nom du logiciel : " << gS.appName << L"\r\n";
        ss << L"Logo : " << gS.appLogo << L"\r\n";
        ss << L"Etablissement : " << gS.ecole << L"\r\n";
        ss << L"Matricule : " << gS.matricule << L"\r\n";
        ss << L"Regime : " << gS.regime << L"\r\n";
        ss << L"Devise : " << gS.devise << L"\r\n";
        ss << L"Classes Ecole : " << gS.classesEcole << L"\r\n";
        ss << L"Classes College : " << gS.classesCollege << L"\r\n";
        ss << L"Classes Lycee : " << gS.classesLycee << L"\r\n";
        ss << L"\r\nUtilisez Parametres en bas pour modifier ces valeurs.\r\n";
    } else {
        ss << PageLabel(gCurrentPage) << L"\r\n" << separator << L"\r\n";
        ss << L"Cycle actif : " << cycleName << L"\r\n";
        ss << L"Classes : " << JoinList(classes, L", ") << L"\r\n\r\n";
        ss << L"Module en cours de developpement.\r\n";
    }
    return ss.str();
}

void DrawKpiCard(Gdiplus::Graphics& g, const Gdiplus::RectF& r, const wchar_t* label,
                 const wstring& value, const wchar_t* sub, const Gdiplus::Color& accent) {
    ui::FillRoundRect(g, r, 12.0f, ui::palette::Card());
    ui::StrokeRoundRect(g, r, 12.0f, ui::palette::Border());
    ui::FillRoundRect(g, Gdiplus::RectF(r.X + r.Width - 40.0f, r.Y + 16.0f, 22.0f, 22.0f), 11.0f, accent);
    ui::DrawUiText(g, label, Gdiplus::RectF(r.X + 16.0f, r.Y + 14.0f, r.Width - 60.0f, 20.0f),
        13.0f, ui::palette::TextMuted());
    ui::DrawUiText(g, value.c_str(), Gdiplus::RectF(r.X + 16.0f, r.Y + 38.0f, r.Width - 32.0f, 40.0f),
        28.0f, ui::palette::TextMain(), true);
    ui::DrawUiText(g, sub, Gdiplus::RectF(r.X + 16.0f, r.Y + 82.0f, r.Width - 32.0f, 18.0f),
        12.0f, ui::palette::TextMuted());
}

void DrawDashboard(Gdiplus::Graphics& g, int left, int top, int width) {
    const float x = static_cast<float>(left);
    const float y = static_cast<float>(top);
    const float w = static_cast<float>(width);

    const wstring subtitle = gS.ecole.empty()
        ? wstring(L"Configurez l'etablissement dans Parametres.")
        : gS.ecole;
    ui::FillRoundRect(g, Gdiplus::RectF(x, y, w, 96.0f), 14.0f, Gdiplus::Color(255, 30, 58, 138));
    ui::DrawUiText(g, L"Tableau de bord", Gdiplus::RectF(x + 24.0f, y + 16.0f, w - 48.0f, 32.0f),
        22.0f, ui::palette::White(), true);
    ui::DrawUiText(g, subtitle.c_str(), Gdiplus::RectF(x + 24.0f, y + 52.0f, w - 48.0f, 22.0f),
        14.0f, Gdiplus::Color(255, 190, 210, 255));

    long long nbPaiements = 0;
    long long reste = 0;
    for (const auto& p : gPaiements) if (p.cycle == gCycle) { ++nbPaiements; reste += p.reste; }
    const long long nbEleves = CountElevesOfCycle();
    const size_t nbClasses = ClassesForCycle(gCycle).size();

    const float gap = 16.0f;
    const float cardW = (w - 3.0f * gap) / 4.0f;
    const float cardY = y + 116.0f;
    const float cardH = 110.0f;
    DrawKpiCard(g, Gdiplus::RectF(x, cardY, cardW, cardH), L"Eleves",
        to_wstring(nbEleves), L"eleve(s) dans le cycle", ui::palette::Accent());
    DrawKpiCard(g, Gdiplus::RectF(x + (cardW + gap), cardY, cardW, cardH), L"Classes",
        to_wstring(nbClasses), L"classe(s) configurees", ui::palette::Success());
    DrawKpiCard(g, Gdiplus::RectF(x + 2.0f * (cardW + gap), cardY, cardW, cardH), L"Demandes de paiement",
        to_wstring(nbPaiements), L"demande(s) enregistree(s)", ui::palette::Warning());
    DrawKpiCard(g, Gdiplus::RectF(x + 3.0f * (cardW + gap), cardY, cardW, cardH), L"Reste a percevoir",
        to_wstring(reste) + L" " + gS.devise, L"montant restant du", ui::palette::Danger());

    const vector<wstring> classes = ClassesForCycle(gCycle);
    const float listY = cardY + cardH + 24.0f;
    const float rowH = 28.0f;
    const float listH = 56.0f + static_cast<float>(classes.size()) * rowH;
    ui::FillRoundRect(g, Gdiplus::RectF(x, listY, w, listH), 12.0f, ui::palette::Card());
    ui::StrokeRoundRect(g, Gdiplus::RectF(x, listY, w, listH), 12.0f, ui::palette::Border());
    ui::DrawUiText(g, L"Repartition par classe", Gdiplus::RectF(x + 20.0f, listY + 14.0f, w - 40.0f, 24.0f),
        16.0f, ui::palette::TextMain(), true);
    float rowY = listY + 48.0f;
    for (const auto& c : classes) {
        const wstring count = to_wstring(CountElevesInClasse(c)) + L" eleve(s)";
        ui::DrawUiText(g, c.c_str(), Gdiplus::RectF(x + 20.0f, rowY, w * 0.5f, rowH), 14.0f, ui::palette::TextMain());
        ui::DrawUiText(g, count.c_str(), Gdiplus::RectF(x + w * 0.5f, rowY, w * 0.5f - 20.0f, rowH),
            14.0f, ui::palette::TextMuted(), false, false);
        rowY += rowH;
    }
}

void RedrawChrome() {
    if (!gMain) return;
    InvalidateRect(gMain, nullptr, TRUE);
    for (HWND b : gMenuBtns) InvalidateRect(b, nullptr, TRUE);
    for (HWND b : gActionBtns) InvalidateRect(b, nullptr, TRUE);
    if (gLogout) InvalidateRect(gLogout, nullptr, TRUE);
    if (gHome) InvalidateRect(gHome, nullptr, TRUE);
    if (gToggle) InvalidateRect(gToggle, nullptr, TRUE);
    if (gAdminBtn) InvalidateRect(gAdminBtn, nullptr, TRUE);
}

void RefreshContent() {
    SetWindowTextW(gTitle, PageLabel(gCurrentPage).c_str());
    SetWindowTextW(gContent, BuildPageText().c_str());
    if (gContent) ShowWindow(gContent, gCurrentPage == L"dashboard" ? SW_HIDE : SW_SHOW);
    RedrawChrome();
}

void UpdateWindowTitle() {
    if (!gMain) return;
    const wstring base = gS.appName.empty() ? wstring(L"Gestion scolaire") : gS.appName;
    SetWindowTextW(gMain, base.c_str());
}

void UpdateAdminLabel() {
    if (gAdminBtn) SetWindowTextW(gAdminBtn, (L"Administrateur : " + gS.adminUser).c_str());
}

void AddEleve(HWND owner) {
    const vector<wstring> classes = ClassesForCycle(gCycle);
    if (classes.empty()) {
        ShowWarning(owner, L"Aucune classe configuree pour ce cycle. Completez-les dans Parametres.");
        return;
    }
    vector<DlgField> fields{
        { L"Nom complet", L"", false },
        { L"Email", L"", false },
        { L"Sexe (M / F)", L"", false },
        { L"Classe (" + JoinList(classes, L", ") + L")", classes.front(), false }
    };
    if (!InputDialog(owner, L"Nouvel eleve", fields)) return;

    const wstring nom = Trim(fields[0].value);
    const wstring email = Trim(fields[1].value);
    wstring sexe = Trim(fields[2].value);
    for (auto& c : sexe) c = static_cast<wchar_t>(towupper(c));
    const wstring classe = Trim(fields[3].value);

    if (nom.empty()) { ShowWarning(owner, L"Le nom est obligatoire."); return; }
    if (sexe != L"M" && sexe != L"F") { ShowWarning(owner, L"Le sexe doit etre M ou F."); return; }
    if (std::find(classes.begin(), classes.end(), classe) == classes.end()) {
        ShowWarning(owner, L"Classe inconnue pour ce cycle.");
        return;
    }
    gEleves.push_back(Eleve{ gNextId++, nom, classe, email, sexe, gCycle });
    SaveData();
    RefreshContent();
}

void AddClasse(HWND owner) {
    vector<DlgField> fields{
        { L"Nom de la classe (ex : CP1)", L"", false },
        { L"Annee scolaire (ex : 2026-2027)", L"", false }
    };
    if (!InputDialog(owner, L"Nouvelle classe", fields)) return;

    const wstring nom = Trim(fields[0].value);
    if (nom.empty()) { ShowWarning(owner, L"Le nom de la classe est obligatoire."); return; }
    for (const auto& c : gClasses) {
        if (c.cycle == gCycle && c.nom == nom) {
            ShowWarning(owner, L"Cette classe existe deja pour ce cycle.");
            return;
        }
    }
    gClasses.push_back(Classe{ gNextId++, nom, Trim(fields[1].value), gCycle });
    SaveData();
    RefreshContent();
}

void AddPaiement(HWND owner) {
    vector<DlgField> fields{
        { L"Reference (ex : GPA-2026-001)", L"", false },
        { L"Nom de l'eleve", L"", false },
        { L"Type de frais", L"Inscription", false },
        { L"Montant", L"", false }
    };
    if (!InputDialog(owner, L"Nouvelle demande de paiement", fields)) return;

    const wstring ref = Trim(fields[0].value);
    const wstring nomEleve = Trim(fields[1].value);
    const wstring type = Trim(fields[2].value);
    const wstring montantTxt = Trim(fields[3].value);

    wchar_t* end = nullptr;
    const long long montant = wcstoll(montantTxt.c_str(), &end, 10);
    if (ref.empty() || type.empty() || montantTxt.empty() || *end != L'\0' || montant <= 0) {
        ShowWarning(owner, L"Reference, type et montant (entier positif) sont obligatoires.");
        return;
    }
    const Eleve* eleve = nullptr;
    for (const auto& e : gEleves) {
        if (e.cycle == gCycle && e.nom == nomEleve) { eleve = &e; break; }
    }
    if (!eleve) {
        ShowWarning(owner, L"Eleve introuvable dans ce cycle.");
        return;
    }
    gPaiements.push_back(Paiement{ ref, eleve->nom, eleve->classe, type, gCycle, L"Impaye", montant, montant });
    SaveData();
    RefreshContent();
}

void OpenParams(HWND owner) {
    vector<DlgField> fields{
        { L"Nom du logiciel", gS.appName, false },
        { L"Logo (emoji ou lettre)", gS.appLogo, false },
        { L"Nom de l'etablissement", gS.ecole, false },
        { L"Matricule", gS.matricule, false },
        { L"Regime", gS.regime, false },
        { L"Devise (ex : Ar, FCFA, EUR)", gS.devise, false },
        { L"Classes Ecole (separees par des virgules)", gS.classesEcole, false },
        { L"Classes College", gS.classesCollege, false },
        { L"Classes Lycee", gS.classesLycee, false }
    };
    if (!InputDialog(owner, L"Parametres", fields)) return;

    gS.appName = Trim(fields[0].value);
    gS.appLogo = Trim(fields[1].value);
    gS.ecole = Trim(fields[2].value);
    gS.matricule = Trim(fields[3].value);
    gS.regime = Trim(fields[4].value);
    gS.devise = Trim(fields[5].value);
    gS.classesEcole = fields[6].value;
    gS.classesCollege = fields[7].value;
    gS.classesLycee = fields[8].value;
    SaveSettings();
    UpdateWindowTitle();
    RefreshContent();
}

void CreateMainControls(HWND hwnd) {
    gMenuBtns.clear();
    gActionBtns.clear();

    gToggle = CreateCtl(L"BUTTON", L"\u22EE", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, ID_TOGGLE);
    gTitle = CreateCtl(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, 0);
    gTopLabel = CreateCtl(L"STATIC", L"Etablissement", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, 0);
    gTopCycle = CreateCtl(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, hwnd, ID_CYCLE);
    SendMessageW(gTopCycle, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"École"));
    SendMessageW(gTopCycle, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Collège"));
    SendMessageW(gTopCycle, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Lycée"));
    SendMessageW(gTopCycle, CB_SETCURSEL, static_cast<WPARAM>(CycleIndex()), 0);
    gHome = CreateCtl(L"BUTTON", L"Accueil", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, ID_HOME);
    gBell = CreateCtl(L"STATIC", L"\U0001F514", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0, hwnd, 0);
    gAdminBtn = CreateCtl(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, ID_ADMIN);
    UpdateAdminLabel();

    for (int i = 0; i < kPageCount; ++i) {
        gMenuBtns.push_back(CreateCtl(L"BUTTON", kPages[i].label, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, hwnd, ID_MENU_BASE + i));
    }
    gLogout = CreateCtl(L"BUTTON", L"Deconnexion", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, ID_LOGOUT);

    gContent = CreateCtl(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd, 0);

    const wchar_t* actions[] = { L"+ Nouvel eleve", L"+ Nouvelle classe", L"+ Nouvelle demande", L"Parametres" };
    for (int i = 0; i < 4; ++i) {
        gActionBtns.push_back(CreateCtl(L"BUTTON", actions[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, hwnd, ID_ACT_ELEVE + i));
    }
}

void LayoutMain(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int width = rc.right;
    const int height = rc.bottom;
    const int center = width / 2;

    MoveWindow(gToggle, 10, 8, 36, 32, TRUE);
    MoveWindow(gTitle, 56, 14, 230, 22, TRUE);
    MoveWindow(gTopLabel, center - 210, 14, 110, 22, TRUE);
    MoveWindow(gTopCycle, center - 95, 10, 150, 200, TRUE);
    MoveWindow(gHome, center + 70, 9, 90, 30, TRUE);
    MoveWindow(gBell, width - 250, 13, 30, 24, TRUE);
    MoveWindow(gAdminBtn, width - 210, 8, 200, 32, TRUE);

    const bool show = gSidebarVisible;
    for (size_t i = 0; i < gMenuBtns.size(); ++i) {
        ShowWindow(gMenuBtns[i], show ? SW_SHOW : SW_HIDE);
        MoveWindow(gMenuBtns[i], 10, kTopbarHeight + 8 + static_cast<int>(i) * 30, kSidebarWidth - 20, 26, TRUE);
    }
    ShowWindow(gLogout, show ? SW_SHOW : SW_HIDE);
    MoveWindow(gLogout, 10, height - 52, kSidebarWidth - 20, 32, TRUE);

    const int left = show ? kSidebarWidth + kMargin : kMargin;
    const int contentTop = kTopbarHeight + 8;
    const int actionsY = height - 44;
    MoveWindow(gContent, left, contentTop, width - left - kMargin, actionsY - contentTop - kMargin, TRUE);
    int x = left;
    for (HWND b : gActionBtns) {
        MoveWindow(b, x, actionsY, kActionButtonWidth, 32, TRUE);
        x += kActionSpacing;
    }
}

void OnCommand(HWND hwnd, int id, int code) {
    if (id == ID_TOGGLE) {
        gSidebarVisible = !gSidebarVisible;
        LayoutMain(hwnd);
        return;
    }
    if (id == ID_CYCLE && code == CBN_SELCHANGE) {
        const int sel = static_cast<int>(SendMessageW(gTopCycle, CB_GETCURSEL, 0, 0));
        gCycle = sel == 1 ? L"college" : sel == 2 ? L"lycee" : L"ecole";
        SaveSettings();
        RefreshContent();
        return;
    }
    if (id == ID_HOME) {
        gCurrentPage = L"dashboard";
        RefreshContent();
        return;
    }
    if (id == ID_ADMIN) {
        OpenParams(hwnd);
        return;
    }
    if (id == ID_LOGOUT) {
        if (MessageBoxW(hwnd, L"Voulez-vous vraiment vous deconnecter ?", L"Deconnexion", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            gLogoutRequested = true;
            DestroyWindow(hwnd);
        }
        return;
    }
    if (id >= ID_MENU_BASE && id < ID_MENU_BASE + kPageCount) {
        gCurrentPage = kPages[id - ID_MENU_BASE].key;
        if (gCurrentPage == L"parametres") OpenParams(hwnd);
        RefreshContent();
        return;
    }
    switch (id) {
    case ID_ACT_ELEVE: AddEleve(hwnd); break;
    case ID_ACT_CLASSE: AddClasse(hwnd); break;
    case ID_ACT_PAIEMENT: AddPaiement(hwnd); break;
    case ID_ACT_PARAMS: OpenParams(hwnd); break;
    }
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        gMain = hwnd;
        CreateMainControls(hwnd);
        UpdateWindowTitle();
        LayoutMain(hwnd);
        RefreshContent();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const float w = static_cast<float>(rc.right);
        const float h = static_cast<float>(rc.bottom);
        Gdiplus::SolidBrush background(ui::palette::Background());
        g.FillRectangle(&background, 0.0f, 0.0f, w, h);
        Gdiplus::SolidBrush topbar(ui::palette::Card());
        g.FillRectangle(&topbar, 0.0f, 0.0f, w, static_cast<float>(kTopbarHeight));
        Gdiplus::Pen separator(ui::palette::Border());
        g.DrawLine(&separator, 0.0f, static_cast<float>(kTopbarHeight), w, static_cast<float>(kTopbarHeight));
        if (gSidebarVisible) {
            Gdiplus::SolidBrush sidebar(ui::palette::Sidebar());
            g.FillRectangle(&sidebar, 0.0f, static_cast<float>(kTopbarHeight), static_cast<float>(kSidebarWidth),
                h - static_cast<float>(kTopbarHeight));
        }
        if (gCurrentPage == L"dashboard") {
            const int left = gSidebarVisible ? kSidebarWidth + kMargin : kMargin;
            const int top = kTopbarHeight + 8;
            DrawDashboard(g, left, top, rc.right - left - kMargin);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        if (reinterpret_cast<HWND>(lParam) == gContent) {
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, ui::ToColorRef(ui::palette::TextMain()));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, ui::ToColorRef(ui::palette::TextMain()));
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item->CtlType != ODT_BUTTON) break;
        const int id = static_cast<int>(item->CtlID);
        ui::ButtonKind kind = ui::ButtonKind::Secondary;
        if (id >= ID_MENU_BASE && id < ID_MENU_BASE + kPageCount) {
            const std::wstring key(kPages[id - ID_MENU_BASE].key);
            kind = (key == gCurrentPage) ? ui::ButtonKind::NavActive : ui::ButtonKind::Nav;
        } else if (id == ID_LOGOUT) {
            kind = ui::ButtonKind::Nav;
        } else if (id == ID_ACT_ELEVE || id == ID_ACT_CLASSE || id == ID_ACT_PAIEMENT || id == ID_ACT_PARAMS) {
            kind = ui::ButtonKind::Primary;
        }
        ui::DrawButton(item, kind);
        return TRUE;
    }
    case WM_SIZE:
        LayoutMain(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = POINT{ 1024, 780 };
        return 0;
    case WM_COMMAND:
        OnCommand(hwnd, LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    gInstance = hInstance;
    ui::GdiplusSession gdiplusSession;

    gFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    if (!RegisterWindowClass(L"GS_Main", MainProc, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1)) ||
        !RegisterWindowClass(L"GS_Login", LoginProc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1)) ||
        !RegisterWindowClass(L"GS_Dialog", InputDialogProc, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1))) {
        MessageBoxW(nullptr, L"Impossible d'enregistrer les fenetres.", L"Erreur", MB_OK | MB_ICONERROR);
        return 1;
    }

    LoadSettings();
    LoadData();

    if (EnsureAccount(nullptr)) {
        while (RunLogin()) {
            gMain = CreateWindowExW(0, L"GS_Main", L"Gestion scolaire", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                CW_USEDEFAULT, CW_USEDEFAULT, 1100, 820, nullptr, nullptr, gInstance, nullptr);
            if (!gMain) break;
            ShowWindow(gMain, SW_SHOW);
            UpdateWindow(gMain);

            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            gMain = nullptr;
            if (!gLogoutRequested) break;
            gLogoutRequested = false;
        }
    }

    SaveData();
    SaveSettings();
    DeleteObject(gFont);
    return 0;
}
