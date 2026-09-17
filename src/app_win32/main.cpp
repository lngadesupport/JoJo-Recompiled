#ifdef _WIN32
#define NOMINMAX
#include "core/conversion.h"
#include "core/ps1_boot_report_io.h"
#include "core/ps1_installation.h"
#include "core/runtime.h"
#include "core/settings.h"
#include <windows.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj_core.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace {
namespace fs = std::filesystem;
constexpr UINT WM_PROGRESS = WM_APP + 10;
constexpr UINT WM_FINISHED = WM_APP + 11;
constexpr int ID_SOURCE_PATH = 1001;
constexpr int ID_SELECT_SOURCE = 1002;
constexpr int ID_PREPARE = 1003;
constexpr int ID_INSTALL_PATH = 1004;
constexpr int ID_SELECT_INSTALL = 1005;
constexpr int ID_RUN_CHECKPOINT = 1006;
constexpr COLORREF BG=RGB(13,8,22), PANEL=RGB(35,21,53), TEXT=RGB(248,244,252), MUTED=RGB(185,169,198);
constexpr COLORREF PURPLE=RGB(119,73,196), MAGENTA=RGB(220,64,166), GOLD=RGB(235,193,83);
HWND win{}, source_box{}, source_btn{}, install_box{}, install_btn{}, prepare_btn{}, checkpoint_btn{};
HFONT title_font{}, body_font{}, small_font{}, button_font{};
HBRUSH edit_brush{};
fs::path game_dir, settings_path;
jojo::AppSettings app_settings{};
std::wstring source, status=L"Selecione a imagem da sua própria cópia do jogo.";
std::deque<std::wstring> logs;
int percent=0;
bool running=false, converted=false;
std::atomic_bool closing{false};
struct ProgressMsg { jojo::ConversionProgress p; };
struct FinishMsg { jojo::Result<jojo::ConversionManifest> r; };

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(n<=0) return L"[UTF-8 inválido]";
    std::wstring out(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);
    return out;
}

std::string utf8(std::wstring_view s) {
    if (s.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if(n<=0) return {};
    std::string out(static_cast<size_t>(n),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}

fs::path app_root() {
    PWSTR raw=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&raw)))
        return fs::current_path()/L"JOJO Recompiled User Data";
    fs::path p(raw); CoTaskMemFree(raw); return p/L"JOJO Recompiled";
}

void add_log(std::wstring s) {
    logs.push_back(std::move(s));
    while(logs.size()>5) logs.pop_front();
}

void draw_text(HDC dc,const std::wstring& text,RECT r,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE){
    auto old=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
    DrawTextW(dc,text.c_str(),-1,&r,flags); SelectObject(dc,old);
}

void fill_round(HDC dc,RECT r,COLORREF c,int radius=14){
    HBRUSH b=CreateSolidBrush(c); HPEN p=CreatePen(PS_SOLID,1,c);
    auto ob=SelectObject(dc,b), op=SelectObject(dc,p); RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(p); DeleteObject(b);
}

void paint(HDC dc,RECT c){
    HBRUSH b=CreateSolidBrush(BG); FillRect(dc,&c,b); DeleteObject(b);
    POINT a[]={{c.right-390,0},{c.right,0},{c.right,210},{c.right-510,118}};
    b=CreateSolidBrush(RGB(48,25,73)); auto old=SelectObject(dc,b); Polygon(dc,a,4); SelectObject(dc,old); DeleteObject(b);
    for(int x=c.right-300;x<c.right;x+=38){ HPEN p=CreatePen(PS_SOLID,2,RGB(82,50,98)); auto op=SelectObject(dc,p); MoveToEx(dc,x,15,nullptr); LineTo(dc,x+120,145); SelectObject(dc,op); DeleteObject(p); }

    draw_text(dc,L"JOJO RECOMPILED",{78,36,800,90},title_font,TEXT);
    draw_text(dc,L"HERITAGE FOR THE FUTURE  •  PROJETO NATIVO WINDOWS",{82,92,820,126},body_font,GOLD);
    draw_text(dc,L"Use uma imagem obtida da sua própria cópia. O projeto não distribui ROM, BIOS, arte, música ou dados do jogo.",{82,140,905,194},body_font,MUTED,DT_LEFT|DT_TOP|DT_WORDBREAK);

    draw_text(dc,L"IMAGEM PS1 DA SUA CÓPIA",{82,211,500,241},body_font,TEXT);
    draw_text(dc,L"PASTA DE INSTALAÇÃO",{82,309,500,339},body_font,TEXT);
    draw_text(dc,status,{82,407,815,454},body_font,converted?GOLD:MUTED,DT_LEFT|DT_TOP|DT_WORDBREAK);
    draw_text(dc,std::to_wstring(percent)+L"%",{820,414,920,449},body_font,GOLD,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    RECT track{80,462,920,495}; fill_round(dc,track,RGB(34,21,47));
    RECT bar{84,466,84+(832*std::clamp(percent,0,100))/100,491};
    if(bar.right>bar.left){ b=CreateSolidBrush(MAGENTA); FillRect(dc,&bar,b); DeleteObject(b); }

    RECT card{80,525,920,660}; fill_round(dc,card,PANEL);
    draw_text(dc,L"ATIVIDADE",{102,536,400,567},body_font,GOLD);
    int y=569; for(const auto& l:logs){ draw_text(dc,l,{102,y,892,y+21},small_font,MUTED); y+=18; }

    const std::wstring destination=L"Destino: "+game_dir.wstring();
    draw_text(dc,destination,{80,762,920,815},small_font,RGB(144,128,155),DT_LEFT|DT_TOP|DT_WORDBREAK);
}

bool supported_image(const fs::path& image) {
    const auto ext=image.extension().wstring();
    const auto is=[](const std::wstring& lhs,const wchar_t* rhs){return CompareStringOrdinal(lhs.c_str(),-1,rhs,-1,TRUE)==CSTR_EQUAL;};
    return is(ext,L".iso")||is(ext,L".bin")||is(ext,L".cue");
}

bool usable_image(const fs::path& image) {
    const DWORD attributes=GetFileAttributesW(image.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY)==0 && supported_image(image);
}

void refresh_install(){
    converted=false;
    auto kind=jojo::classify_installation(game_dir);
    if(!kind){
        percent=0;
        status=L"Instalação inválida: "+wide(kind.detail);
        add_log(L"A instalação selecionada não pôde ser classificada.");
        SetWindowTextW(prepare_btn,L"PREPARAR JOGO");
        EnableWindow(checkpoint_btn,FALSE);
        InvalidateRect(win,nullptr,FALSE);
        return;
    }

    switch(kind.value){
    case jojo::InstallationKind::absent:
        percent=0;
        status=source.empty()
            ? L"Selecione a imagem PS1 da sua própria cópia do jogo."
            : L"Imagem PS1 selecionada. Pronto para preparar na pasta escolhida.";
        SetWindowTextW(prepare_btn,L"PREPARAR JOGO");
        break;
    case jojo::InstallationKind::legacy_v1:
        percent=0;
        status=L"Instalação legada Dreamcast/SH-4 incompatível detectada. Reconverta sua imagem PS1 nesta pasta.";
        add_log(L"Instalação legada preservada; uma nova geração PS1 será criada ao reconverter.");
        SetWindowTextW(prepare_btn,L"RECONVERTER NESTA PASTA");
        break;
    case jojo::InstallationKind::ps1_m1:{
        auto validated=jojo::validate_installation(game_dir);
        if(!validated){
            percent=0;
            status=L"Instalação PS1 inválida: "+wide(validated.detail);
            add_log(L"A geração PS1 ativa falhou na validação.");
            SetWindowTextW(prepare_btn,L"REFAZER PREPARAÇÃO");
            break;
        }
        converted=true;
        percent=100;
        status=L"Executável PS1 identificado e instalação validada. Checkpoint R3000A disponível.";
        add_log(L"Geração PS1 M1 validada a partir dos dados locais preparados.");
        SetWindowTextW(prepare_btn,L"REFAZER PREPARAÇÃO");
        break;
    }
    }
    EnableWindow(checkpoint_btn,converted&&!running);
    InvalidateRect(win,nullptr,FALSE);
}

void select_image(const fs::path& image) {
    source=image.wstring();
    SetWindowTextW(source_box,source.c_str());
    add_log(L"Imagem PS1 selecionada.");
    refresh_install();
}

std::wstring choose_image(){
    IFileOpenDialog* d=nullptr; if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d)))) return {};
    const COMDLG_FILTERSPEC f[]={{L"Imagens PS1 suportadas",L"*.iso;*.bin;*.cue"},{L"Todos os arquivos",L"*.*"}};
    d->SetFileTypes(2,f); d->SetTitle(L"Selecione a imagem PS1 da sua própria cópia"); std::wstring out;
    if(SUCCEEDED(d->Show(win))){ IShellItem* item=nullptr; if(SUCCEEDED(d->GetResult(&item))){ PWSTR p=nullptr; if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){out=p;CoTaskMemFree(p);} item->Release(); }}
    d->Release(); return out;
}

std::wstring choose_install_root(){
    IFileOpenDialog* d=nullptr;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d)))) return {};
    DWORD options{};
    if(SUCCEEDED(d->GetOptions(&options))) d->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
    d->SetTitle(L"Selecione a pasta de instalação do JOJO Recompiled");
    std::wstring out;
    if(SUCCEEDED(d->Show(win))){
        IShellItem* item=nullptr;
        if(SUCCEEDED(d->GetResult(&item))){
            PWSTR p=nullptr;
            if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){out=p;CoTaskMemFree(p);}
            item->Release();
        }
    }
    d->Release();
    return out;
}

void select_install_root(const fs::path& root){
    game_dir=root;
    SetWindowTextW(install_box,game_dir.wstring().c_str());
    app_settings.install_root=utf8(game_dir.wstring());
    const auto saved=jojo::save_settings_atomic(settings_path,app_settings);
    if(!saved){
        status=L"Pasta selecionada, mas não foi possível salvar a configuração: "+wide(saved.detail);
        add_log(L"Falha ao salvar settings.ini.");
    }else{
        add_log(L"Pasta de instalação atualizada.");
    }
    refresh_install();
}

void set_enabled(bool on){
    EnableWindow(source_btn,on);
    EnableWindow(install_btn,on);
    EnableWindow(prepare_btn,on);
    EnableWindow(checkpoint_btn,on&&converted);
}

void run_checkpoint(){
    if(!converted||running) return;

    const auto report_path=app_root()/L"diagnostics"/L"m3a-checkpoint.txt";
    const auto result=jojo::bootstrap_runtime_local_evidence_to_file(game_dir,report_path);
    if(!result){
        status=L"Checkpoint profundo falhou: "+wide(result.detail);
        add_log(L"Falha ao gerar diagnóstico derivado do checkpoint.");
    }else{
        status=L"Checkpoint profundo concluído. Relatório: "+report_path.wstring();
        add_log(L"Parada: "+wide(jojo::ps1_boot_stop_reason_name(result.value.stop_reason)));
    }
    InvalidateRect(win,nullptr,FALSE);
}

void start_conversion(){
    if(running) return;
    if(source.empty()){
        status=L"Selecione uma imagem .ISO, .BIN ou .CUE.";
        add_log(L"Nenhuma imagem PS1 selecionada.");
        InvalidateRect(win,nullptr,FALSE);
        return;
    }
    running=true;converted=false;percent=0;logs.clear();status=L"Iniciando preparação PS1...";add_log(L"Processo PS1 iniciado.");set_enabled(false);InvalidateRect(win,nullptr,FALSE);
    const std::wstring src=source; const fs::path dest=game_dir; const HWND target=win;
    std::thread([src,dest,target](){
        auto r=jojo::convert_image(fs::path(src),dest,[&](const jojo::ConversionProgress& p){
            if(closing.load()) return;
            auto* m=new ProgressMsg{p};
            if(!PostMessageW(target,WM_PROGRESS,0,reinterpret_cast<LPARAM>(m))) delete m;
        });
        if(closing.load()) return;
        auto* m=new FinishMsg{std::move(r)};
        if(!PostMessageW(target,WM_FINISHED,0,reinterpret_cast<LPARAM>(m))) delete m;
    }).detach();
}

void make_fonts(){
    title_font=CreateFontW(-42,0,0,0,FW_HEAVY,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI Black");
    body_font=CreateFontW(-19,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    small_font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    button_font=body_font;
}

void create_controls(HWND parent){
    source_box=CreateWindowExW(0,L"EDIT",L"Nenhuma imagem selecionada",WS_CHILD|WS_VISIBLE|ES_READONLY|ES_AUTOHSCROLL,82,249,616,42,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SOURCE_PATH)),GetModuleHandleW(nullptr),nullptr);
    source_btn=CreateWindowExW(0,L"BUTTON",L"SELECIONAR IMAGEM",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,712,249,208,42,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SELECT_SOURCE)),GetModuleHandleW(nullptr),nullptr);
    install_box=CreateWindowExW(0,L"EDIT",game_dir.wstring().c_str(),WS_CHILD|WS_VISIBLE|ES_READONLY|ES_AUTOHSCROLL,82,347,616,42,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_INSTALL_PATH)),GetModuleHandleW(nullptr),nullptr);
    install_btn=CreateWindowExW(0,L"BUTTON",L"SELECIONAR PASTA",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,712,347,208,42,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SELECT_INSTALL)),GetModuleHandleW(nullptr),nullptr);
    checkpoint_btn=CreateWindowExW(0,L"BUTTON",L"EXECUTAR CHECKPOINT",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,300,690,300,50,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RUN_CHECKPOINT)),GetModuleHandleW(nullptr),nullptr);
    prepare_btn=CreateWindowExW(0,L"BUTTON",L"PREPARAR JOGO",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,620,690,300,50,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PREPARE)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(source_box,WM_SETFONT,reinterpret_cast<WPARAM>(body_font),TRUE);
    SendMessageW(install_box,WM_SETFONT,reinterpret_cast<WPARAM>(body_font),TRUE);
    DragAcceptFiles(parent,TRUE);
}

void draw_button(DRAWITEMSTRUCT* d){
    const bool off=(d->itemState&ODS_DISABLED)!=0, press=(d->itemState&ODS_SELECTED)!=0;
    COLORREF c=d->CtlID==ID_PREPARE?MAGENTA:PURPLE;
    if(press)c=RGB(GetRValue(c)*3/4,GetGValue(c)*3/4,GetBValue(c)*3/4);
    if(off)c=RGB(68,54,76);
    fill_round(d->hDC,d->rcItem,c);
    wchar_t t[96]{};GetWindowTextW(d->hwndItem,t,96);draw_text(d->hDC,t,d->rcItem,button_font,off?MUTED:TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE:
        win=h;create_controls(h);refresh_install();return 0;
    case WM_COMMAND:
        if(LOWORD(w)==ID_SELECT_SOURCE){
            auto p=choose_image();
            if(!p.empty()&&usable_image(fs::path(p)))select_image(fs::path(p));
            return 0;
        }
        if(LOWORD(w)==ID_SELECT_INSTALL){
            auto p=choose_install_root();
            if(!p.empty())select_install_root(fs::path(p));
            return 0;
        }
        if(LOWORD(w)==ID_RUN_CHECKPOINT){run_checkpoint();return 0;}
        if(LOWORD(w)==ID_PREPARE){start_conversion();return 0;}
        break;
    case WM_DROPFILES:{
        const auto drop=reinterpret_cast<HDROP>(w); const UINT count=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);
        if(count==1){
            const UINT length=DragQueryFileW(drop,0,nullptr,0); std::wstring path(static_cast<size_t>(length)+1,L'\0');
            if(DragQueryFileW(drop,0,path.data(),length+1)){path.resize(length);const fs::path image(path);if(usable_image(image))select_image(image);}
        }
        DragFinish(drop);return 0;
    }
    case WM_PROGRESS:{
        std::unique_ptr<ProgressMsg> p(reinterpret_cast<ProgressMsg*>(l));
        if(p){percent=std::clamp(p->p.percent,0,100);status=wide(p->p.detail);add_log(L"["+std::to_wstring(percent)+L"%] "+wide(p->p.detail));InvalidateRect(h,nullptr,FALSE);}
        return 0;
    }
    case WM_FINISHED:{
        std::unique_ptr<FinishMsg> p(reinterpret_cast<FinishMsg*>(l));
        running=false;set_enabled(true);
        if(!p||!p->r){
            status=L"Falha na preparação PS1."+(p?L" "+wide(p->r.detail):L"");
            if(p)add_log(L"ERRO: "+wide(p->r.detail));
        }else{
            add_log(L"Conversão PS1 concluída; validando geração ativa.");
            refresh_install();
        }
        InvalidateRect(h,nullptr,FALSE);return 0;
    }
    case WM_DRAWITEM:draw_button(reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:{HDC dc=reinterpret_cast<HDC>(w);SetTextColor(dc,TEXT);SetBkColor(dc,PANEL);return reinterpret_cast<INT_PTR>(edit_brush);}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT c{};GetClientRect(h,&c);paint(dc,c);EndPaint(h,&ps);return 0;}
    case WM_CLOSE:closing.store(true);DestroyWindow(h);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
}

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR,int show){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;

    const auto root=app_root();
    settings_path=root/L"settings.ini";
    const auto loaded=jojo::load_settings(settings_path);
    if(loaded) app_settings=loaded.value;
    if(!app_settings.install_root.empty()) game_dir=fs::path(wide(app_settings.install_root));
    if(game_dir.empty()) game_dir=root/L"game";

    make_fonts();edit_brush=CreateSolidBrush(PANEL);
    WNDCLASSEXW c{};c.cbSize=sizeof(c);c.lpfnWndProc=proc;c.hInstance=inst;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hIcon=LoadIconW(nullptr,IDI_APPLICATION);c.lpszClassName=L"JOJORecompiledWindow";
    if(!RegisterClassExW(&c)){CoUninitialize();return 3;}
    win=CreateWindowExW(0,c.lpszClassName,L"JOJO Recompiled",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,1018,880,nullptr,nullptr,inst,nullptr);
    if(!win){CoUninitialize();return 4;}
    ShowWindow(win,show);UpdateWindow(win);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(title_font)DeleteObject(title_font);if(body_font)DeleteObject(body_font);if(small_font)DeleteObject(small_font);if(edit_brush)DeleteObject(edit_brush);CoUninitialize();return static_cast<int>(msg.wParam);
}
#endif