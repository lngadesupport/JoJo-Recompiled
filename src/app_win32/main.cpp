#ifdef _WIN32
#define NOMINMAX
#include "core/game_source_binding.h"
#include "core/ps1_commercial_evidence.h"
#include "core/ps1_commercial_evidence_io.h"
#include "core/ps1_disc_session.h"
#include "core/settings.h"
#include "presentation_host.h"
#include <windows.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj_core.h>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <string_view>

namespace jojo::win32 {
namespace {
std::filesystem::path path_from_utf8(const std::string& text) {
#if defined(__cpp_lib_char8_t)
    std::u8string encoded;
    encoded.reserve(text.size());
    for (const unsigned char ch : text) encoded.push_back(static_cast<char8_t>(ch));
    return std::filesystem::path(encoded);
#else
    return std::filesystem::u8path(text);
#endif
}
}

Result<std::optional<std::filesystem::path>> resolve_startup_source(
    const std::filesystem::path& executable_dir,
    const AppSettings& settings,
    const Ps1DiscOpenOptions& open_options) {
    if (!settings.source_binding_path.empty()) {
        const auto binding_path = path_from_utf8(settings.source_binding_path);
        auto binding = load_game_source_binding(binding_path);
        if (binding) {
            auto reopened = reopen_bound_source(binding.value, open_options);
            if (reopened) {
                return Result<std::optional<std::filesystem::path>>::success(
                    reopened.value.binding().source_path);
            }
        }
    }

    return discover_single_ps1_source(executable_dir / "Data" / "ROM");
}
} // namespace jojo::win32

namespace {
namespace fs = std::filesystem;
constexpr int ID_SOURCE_PATH = 1001;
constexpr int ID_SELECT_SOURCE = 1002;
constexpr int ID_VALIDATE_SOURCE = 1003;
constexpr int ID_RUN_CHECKPOINT = 1006;
constexpr COLORREF BG=RGB(13,8,22), PANEL=RGB(35,21,53), TEXT=RGB(248,244,252), MUTED=RGB(185,169,198);
constexpr COLORREF PURPLE=RGB(119,73,196), MAGENTA=RGB(220,64,166), GOLD=RGB(235,193,83);
HWND win{}, source_box{}, source_btn{}, validate_btn{}, checkpoint_btn{}, game_window{};
HFONT title_font{}, body_font{}, small_font{}, button_font{};
HBRUSH edit_brush{};
std::optional<jojo::D3d11Ps1Presenter> game_presenter{};
jojo::Ps1DisplayFrame game_frame{};
fs::path settings_path, binding_path, executable_root;
jojo::AppSettings app_settings{};
jojo::Ps1DiscOpenOptions open_options{};
std::wstring source;
std::wstring status=L"Selecione a imagem da sua própria cópia do jogo.";
std::deque<std::wstring> logs;
bool validated=false;

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

fs::path executable_dir() {
    std::wstring buffer(32768,L'\0');
    const DWORD length=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(length==0 || length>=buffer.size()) return fs::current_path();
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}

void add_log(std::wstring s) {
    logs.push_back(std::move(s));
    while(logs.size()>6) logs.pop_front();
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
    draw_text(dc,L"HERITAGE FOR THE FUTURE  •  RUNTIME PS1 DIRETO",{82,92,820,126},body_font,GOLD);
    draw_text(dc,L"A imagem original é aberta somente para leitura. Nenhuma instalação extraída do jogo é criada.",{82,140,905,194},body_font,MUTED,DT_LEFT|DT_TOP|DT_WORDBREAK);

    draw_text(dc,L"IMAGEM PS1 DA SUA CÓPIA",{82,211,500,241},body_font,TEXT);
    draw_text(dc,status,{82,326,920,392},body_font,validated?GOLD:MUTED,DT_LEFT|DT_TOP|DT_WORDBREAK);

    RECT card{80,420,920,610}; fill_round(dc,card,PANEL);
    draw_text(dc,L"ATIVIDADE",{102,434,400,465},body_font,GOLD);
    int y=470; for(const auto& l:logs){ draw_text(dc,l,{102,y,892,y+22},small_font,MUTED); y+=22; }

    draw_text(dc,L"Fluxo direto: binding salvo → Data/ROM → seleção manual. Sem PREPARAR JOGO e sem pasta de instalação.",{80,762,920,815},small_font,RGB(144,128,155),DT_LEFT|DT_TOP|DT_WORDBREAK);
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

void refresh_actions(){
    EnableWindow(validate_btn,TRUE);
    EnableWindow(checkpoint_btn,validated?TRUE:FALSE);
}

void select_image(const fs::path& image) {
    source=image.wstring();
    validated=false;
    if(source_box) SetWindowTextW(source_box,source.c_str());
    status=L"Imagem selecionada. Clique em VALIDAR JOGO para confirmar a revisão e o PS-X EXE.";
    add_log(L"Imagem PS1 selecionada diretamente; nenhum conteúdo foi extraído.");
    refresh_actions();
    if(win) InvalidateRect(win,nullptr,FALSE);
}

std::wstring choose_image(){
    IFileOpenDialog* d=nullptr; if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&d)))) return {};
    const COMDLG_FILTERSPEC f[]={{L"Imagens PS1 suportadas",L"*.iso;*.bin;*.cue"},{L"Todos os arquivos",L"*.*"}};
    d->SetFileTypes(2,f); d->SetTitle(L"Selecione a imagem PS1 da sua própria cópia"); std::wstring out;
    if(SUCCEEDED(d->Show(win))){ IShellItem* item=nullptr; if(SUCCEEDED(d->GetResult(&item))){ PWSTR p=nullptr; if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))){out=p;CoTaskMemFree(p);} item->Release(); }}
    d->Release(); return out;
}

void validate_source(){
    if(source.empty()){
        validated=false;
        status=L"Selecione uma imagem .ISO, .BIN ou .CUE para validar.";
        add_log(L"Nenhuma imagem PS1 selecionada.");
        refresh_actions();
        InvalidateRect(win,nullptr,FALSE);
        return;
    }

    auto opened=jojo::Ps1DiscSession::open(fs::path(source),open_options);
    if(!opened){
        validated=false;
        status=L"A imagem não foi validada: "+wide(opened.detail);
        add_log(L"Validação falhou; a fonte permanece intacta e somente leitura.");
        refresh_actions();
        InvalidateRect(win,nullptr,FALSE);
        return;
    }

    validated=true;
    status=L"Imagem validada. Revisão: "+wide(opened.value.binding().revision_id)+L". Análise de frontier disponível.";
    add_log(L"SYSTEM.CNF e PS-X EXE foram lidos diretamente da imagem original.");

    const auto binding_saved=jojo::save_game_source_binding_atomic(binding_path,opened.value.binding());
    if(!binding_saved){
        add_log(L"Aviso: não foi possível persistir o binding: "+wide(binding_saved.detail));
    }else{
        app_settings.source_binding_path=utf8(binding_path.wstring());
        const auto settings_saved=jojo::save_settings_atomic(settings_path,app_settings);
        if(!settings_saved) add_log(L"Aviso: binding salvo, mas settings.ini não pôde ser atualizado.");
        else add_log(L"Binding persistente atualizado para os próximos launches.");
    }

    refresh_actions();
    InvalidateRect(win,nullptr,FALSE);
}

LRESULT CALLBACK game_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_PAINT:{
        PAINTSTRUCT ps{};
        BeginPaint(h,&ps);
        EndPaint(h,&ps);
        if(game_presenter && !game_frame.rgba8.empty()){
            const auto presented=game_presenter->present(game_frame);
            (void)presented;
        }
        return 0;
    }
    case WM_SIZE:
        if(w!=SIZE_MINIMIZED && game_presenter && !game_frame.rgba8.empty()){
            const auto presented=game_presenter->present(game_frame);
            (void)presented;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if(h==game_window){
            game_presenter.reset();
            game_frame={};
            game_window=nullptr;
        }
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

bool show_game_frame(jojo::Ps1DisplayFrame frame){
    if(frame.width==0u || frame.height==0u || frame.rgba8.empty()) return false;

    if(!game_window){
        game_window=CreateWindowExW(
            0,
            L"JOJORecompiledGameWindow",
            L"JOJO Recompiled — Game Output",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            960,
            720,
            win,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if(!game_window) return false;

        auto presenter=jojo::D3d11Ps1Presenter::create(game_window);
        if(!presenter){
            DestroyWindow(game_window);
            game_window=nullptr;
            return false;
        }
        game_presenter=std::move(presenter.value);
    }

    game_frame=std::move(frame);
    ShowWindow(game_window,SW_SHOWNORMAL);
    UpdateWindow(game_window);
    const auto presented=game_presenter->present(game_frame);
    return static_cast<bool>(presented);
}

void run_checkpoint(){
    if(!validated || source.empty()) return;

    const auto report_path=app_root()/L"diagnostics"/L"commercial-frontier.txt";
    auto runner=jojo::Ps1CommercialEvidenceRunner::open(fs::path(source),open_options);
    if(!runner){
        status=L"Análise comercial falhou ao abrir a fonte: "+wide(runner.detail);
        add_log(L"Falha antes da execução; a imagem original permaneceu intacta.");
        InvalidateRect(win,nullptr,FALSE);
        return;
    }

    jojo::Ps1CommercialEvidenceOptions options{};
    options.boot.instruction_budget=250000u;
    options.boot.trace_capacity=64u;
    options.boot.mmio_event_capacity=64u;
    options.boot.bios_event_capacity=64u;
    options.boot.stagnation_instruction_limit=50000u;

    const auto report=runner.value.run(options);
    if(report.first_frame){
        if(show_game_frame(runner.value.display_frame())){
            add_log(L"Primeiro frame PS1 apresentado na janela de jogo.");
        }else{
            add_log(L"Aviso: frame PS1 detectado, mas a apresentação D3D11 falhou.");
        }
    }
    const auto saved=jojo::save_ps1_commercial_evidence_report_atomic(report_path,report);
    const auto frontier_name=std::string(jojo::ps1_commercial_frontier_class_name(report.frontier));
    if(!saved){
        status=L"Frontier identificado, mas o relatório não pôde ser salvo: "+wide(saved.detail);
        add_log(L"Frontier: "+wide(frontier_name));
    }else{
        status=L"Frontier comercial identificado: "+wide(frontier_name)+L". Relatório: "+report_path.wstring();
        add_log(L"Frontier: "+wide(frontier_name));
        add_log(L"Instruções aposentadas: "+std::to_wstring(report.total_instructions_retired));
    }
    InvalidateRect(win,nullptr,FALSE);
}

void make_fonts(){
    title_font=CreateFontW(-42,0,0,0,FW_HEAVY,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI Black");
    body_font=CreateFontW(-19,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    small_font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    button_font=body_font;
}

void create_controls(HWND parent){
    const wchar_t* initial=source.empty()?L"Nenhuma imagem selecionada":source.c_str();
    source_box=CreateWindowExW(0,L"EDIT",initial,WS_CHILD|WS_VISIBLE|ES_READONLY|ES_AUTOHSCROLL,82,249,616,42,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SOURCE_PATH)),GetModuleHandleW(nullptr),nullptr);
    source_btn=CreateWindowExW(0,L"BUTTON",L"SELECIONAR IMAGEM",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,712,249,208,42,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SELECT_SOURCE)),GetModuleHandleW(nullptr),nullptr);
    checkpoint_btn=CreateWindowExW(0,L"BUTTON",L"EXECUTAR CHECKPOINT",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,300,690,300,50,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RUN_CHECKPOINT)),GetModuleHandleW(nullptr),nullptr);
    validate_btn=CreateWindowExW(0,L"BUTTON",L"VALIDAR JOGO",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,620,690,300,50,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_VALIDATE_SOURCE)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(source_box,WM_SETFONT,reinterpret_cast<WPARAM>(body_font),TRUE);
    DragAcceptFiles(parent,TRUE);
    refresh_actions();
}

void draw_button(DRAWITEMSTRUCT* d){
    const bool off=(d->itemState&ODS_DISABLED)!=0, press=(d->itemState&ODS_SELECTED)!=0;
    COLORREF c=d->CtlID==ID_VALIDATE_SOURCE?MAGENTA:PURPLE;
    if(press)c=RGB(GetRValue(c)*3/4,GetGValue(c)*3/4,GetBValue(c)*3/4);
    if(off)c=RGB(68,54,76);
    fill_round(d->hDC,d->rcItem,c);
    wchar_t t[96]{};GetWindowTextW(d->hwndItem,t,96);draw_text(d->hDC,t,d->rcItem,button_font,off?MUTED:TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE:
        win=h;
        create_controls(h);
        if(!source.empty()){
            status=L"Imagem detectada automaticamente. Validando diretamente da fonte original...";
            validate_source();
        }
        return 0;
    case WM_COMMAND:
        if(LOWORD(w)==ID_SELECT_SOURCE){
            auto p=choose_image();
            if(!p.empty()&&usable_image(fs::path(p)))select_image(fs::path(p));
            return 0;
        }
        if(LOWORD(w)==ID_VALIDATE_SOURCE){validate_source();return 0;}
        if(LOWORD(w)==ID_RUN_CHECKPOINT){run_checkpoint();return 0;}
        break;
    case WM_DROPFILES:{
        const auto drop=reinterpret_cast<HDROP>(w); const UINT count=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);
        if(count==1){
            const UINT length=DragQueryFileW(drop,0,nullptr,0); std::wstring path(static_cast<size_t>(length)+1,L'\0');
            if(DragQueryFileW(drop,0,path.data(),length+1)){path.resize(length);const fs::path image(path);if(usable_image(image))select_image(image);}
        }
        DragFinish(drop);return 0;
    }
    case WM_DRAWITEM:draw_button(reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:{HDC dc=reinterpret_cast<HDC>(w);SetTextColor(dc,TEXT);SetBkColor(dc,PANEL);return reinterpret_cast<INT_PTR>(edit_brush);}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT c{};GetClientRect(h,&c);paint(dc,c);EndPaint(h,&ps);return 0;}
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
} // namespace

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR,int show){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;

    const auto root=app_root();
    executable_root=executable_dir();
    settings_path=root/L"settings.ini";
    binding_path=root/L"config"/L"game-source.ini";
    const auto loaded=jojo::load_settings(settings_path);
    if(loaded) app_settings=loaded.value;
    if(app_settings.source_binding_path.empty()) app_settings.source_binding_path=utf8(binding_path.wstring());
    else binding_path=fs::path(wide(app_settings.source_binding_path));

    const auto startup=jojo::win32::resolve_startup_source(executable_root,app_settings,open_options);
    if(startup && startup.value) source=startup.value->wstring();
    else if(!startup) status=L"Autodetecção de Data/ROM falhou: "+wide(startup.detail);

    make_fonts();edit_brush=CreateSolidBrush(PANEL);

    WNDCLASSEXW game_class{};
    game_class.cbSize=sizeof(game_class);
    game_class.lpfnWndProc=game_proc;
    game_class.hInstance=inst;
    game_class.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    game_class.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    game_class.lpszClassName=L"JOJORecompiledGameWindow";
    if(!RegisterClassExW(&game_class)){CoUninitialize();return 3;}

    WNDCLASSEXW c{};c.cbSize=sizeof(c);c.lpfnWndProc=proc;c.hInstance=inst;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hIcon=LoadIconW(nullptr,IDI_APPLICATION);c.lpszClassName=L"JOJORecompiledWindow";
    if(!RegisterClassExW(&c)){CoUninitialize();return 3;}
    win=CreateWindowExW(0,c.lpszClassName,L"JOJO Recompiled",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,1018,880,nullptr,nullptr,inst,nullptr);
    if(!win){CoUninitialize();return 4;}
    ShowWindow(win,show);UpdateWindow(win);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(game_window && IsWindow(game_window)) DestroyWindow(game_window);
    game_presenter.reset();
    game_frame={};
    if(title_font)DeleteObject(title_font);if(body_font)DeleteObject(body_font);if(small_font)DeleteObject(small_font);if(edit_brush)DeleteObject(edit_brush);CoUninitialize();return static_cast<int>(msg.wParam);
}
#endif