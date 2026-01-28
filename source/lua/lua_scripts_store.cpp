#include "main.h"
#include "lua_scripts_store.h"
#include "lua_script_manager.h"
#include "../gui.h"
#include <wx/msgdlg.h>
#include <wx/statline.h>
#include <wx/dcbuffer.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <future>
#include <fstream>

// Defines for store URL - Replace with actual URL
#define STORE_INDEX_URL "https://raw.githubusercontent.com/otacademy/rme-scripts/main/repo.json"

// Remove using json = nlohmann::json; to avoid conflict with potential other json definitions or just namespace issues


LuaScriptsStore* LuaScriptsStore::instance = nullptr;

void LuaScriptsStore::ShowStore(wxWindow* parent) {
    if (instance) {
        instance->Raise();
        instance->Show();
        return;
    }

    // We parent it to the frames so it stays on top but doesn't block
    instance = new LuaScriptsStore(parent);
    instance->Show();
}

enum {
    ID_STORE_CLOSE = 1002,
    ID_STORE_SEARCH,
    ID_STORE_RETURN,
    ID_STORE_INSTALL_DETAIL
};

BEGIN_EVENT_TABLE(LuaScriptsStore, wxDialog)
    EVT_CLOSE(LuaScriptsStore::OnWindowClose)
    EVT_BUTTON(ID_STORE_CLOSE, LuaScriptsStore::OnClose)
    EVT_BUTTON(ID_STORE_RETURN, LuaScriptsStore::OnReturn)
    EVT_BUTTON(ID_STORE_INSTALL_DETAIL, LuaScriptsStore::OnInstallBtnDetail)
END_EVENT_TABLE()

LuaScriptsStore::LuaScriptsStore(wxWindow* parent) :
    wxDialog(parent, wxID_ANY, "ScriptsStore", wxDefaultPosition, wxSize(900, 700), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    current_state(STATE_LIST),
    selected_script_index(-1)
{
    SetBackgroundColour(wxColour(245, 247, 250)); // Modern light-gray background
    BuildUI();

    LoadLocalRepo();

    RefreshStoreList();
    ShowListView();
}

LuaScriptsStore::~LuaScriptsStore() {
}

void LuaScriptsStore::BuildUI() {
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // --- Modern Navigation / Filter Bar ---
    wxPanel* navPanel = new wxPanel(this);
    navPanel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* navSizer = new wxBoxSizer(wxHORIZONTAL);

    auto addNavButton = [&](const wxString& text, bool active = false) {
        wxPanel* p = new wxPanel(navPanel);
        if (active) p->SetBackgroundColour(wxColour(49, 130, 206)); // Bootstrap Blue
        else p->SetBackgroundColour(*wxWHITE);

        wxBoxSizer* ps = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* label = new wxStaticText(p, wxID_ANY, text);
        label->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, active ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL));
        label->SetForegroundColour(active ? *wxWHITE : wxColour(74, 85, 104));
        ps->Add(label, 0, wxALL, 10);
        p->SetSizer(ps);
        navSizer->Add(p, 0, wxEXPAND);
    };

    addNavButton("Explore", true);
    addNavButton("Trending");
    addNavButton("Categories");
    addNavButton("Installed");

    navSizer->AddStretchSpacer();

    // Search Box integrated into nav
    search_box = new wxTextCtrl(navPanel, ID_STORE_SEARCH, "", wxDefaultPosition, wxSize(200, -1), wxTE_PROCESS_ENTER);
    navSizer->Add(search_box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    navPanel->SetSizer(navSizer);
    mainSizer->Add(navPanel, 0, wxEXPAND);

    // --- Main Content Switcher ---
    main_content_container = new wxPanel(this);
    wxBoxSizer* containerSizer = new wxBoxSizer(wxVERTICAL);

    // 1. List View Panel
    list_view_panel = new wxPanel(main_content_container);
    wxBoxSizer* listPanelSizer = new wxBoxSizer(wxVERTICAL);

    // Add Banner (Prominent at top of Explore)
    wxImage bannerImg;
    wxString bannerPath = g_gui.getFoundDataDirectory() + "banner.png";
    if (bannerImg.LoadFile(bannerPath, wxBITMAP_TYPE_PNG)) {
        wxPanel* bannerPanel = new wxPanel(list_view_panel);
        bannerPanel->SetBackgroundColour(*wxWHITE);
        wxBoxSizer* bannerSizer = new wxBoxSizer(wxHORIZONTAL);

        wxStaticBitmap* banner = new wxStaticBitmap(bannerPanel, wxID_ANY, wxBitmap(bannerImg));
        bannerSizer->Add(banner, 0, wxALL | wxALIGN_CENTER, 0);

        bannerPanel->SetSizer(bannerSizer);
        listPanelSizer->Add(bannerPanel, 0, wxEXPAND | wxBOTTOM, 0);
    }

    wxPanel* subHeaderPanel = new wxPanel(list_view_panel);
    subHeaderPanel->SetBackgroundColour(wxColour(247, 250, 252));
    wxBoxSizer* subHeaderSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* subHeader = new wxStaticText(subHeaderPanel, wxID_ANY, "Trending Scripts");
    subHeader->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    subHeader->SetForegroundColour(wxColour(45, 55, 72));
    subHeaderSizer->Add(subHeader, 0, wxALL, 15);
    subHeaderPanel->SetSizer(subHeaderSizer);
    listPanelSizer->Add(subHeaderPanel, 0, wxEXPAND);

    scroll_panel = new wxScrolledWindow(list_view_panel, wxID_ANY);
    scroll_panel->SetScrollRate(0, 20);
    scroll_panel->SetBackgroundColour(wxColour(245, 247, 250));
    list_sizer = new wxBoxSizer(wxVERTICAL);
    scroll_panel->SetSizer(list_sizer);
    listPanelSizer->Add(scroll_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

    list_view_panel->SetSizer(listPanelSizer);
    containerSizer->Add(list_view_panel, 1, wxEXPAND);

    // 2. Detail View Panel
    detail_view_panel = new wxPanel(main_content_container);
    wxBoxSizer* detailPanelSizer = new wxBoxSizer(wxVERTICAL);

    detail_scroll = new wxScrolledWindow(detail_view_panel, wxID_ANY);
    detail_scroll->SetScrollRate(0, 20);
    detail_scroll->SetBackgroundColour(*wxWHITE);
    detail_sizer = new wxBoxSizer(wxVERTICAL);
    detail_scroll->SetSizer(detail_sizer);
    detailPanelSizer->Add(detail_scroll, 1, wxEXPAND);

    detail_view_panel->SetSizer(detailPanelSizer);
    containerSizer->Add(detail_view_panel, 1, wxEXPAND);
    detail_view_panel->Hide();

    main_content_container->SetSizer(containerSizer);
    mainSizer->Add(main_content_container, 1, wxEXPAND);

    // --- Footer Section ---
    wxPanel* footerPanel = new wxPanel(this);
    footerPanel->SetBackgroundColour(wxColour(240, 240, 240));
    wxBoxSizer* footerSizer = new wxBoxSizer(wxHORIZONTAL);

    return_button = new wxButton(footerPanel, ID_STORE_RETURN, "Return", wxDefaultPosition, wxSize(100, 35));
    return_button->Hide();
    footerSizer->Add(return_button, 0, wxALL, 5);

    footerSizer->AddStretchSpacer();

    install_button_footer = new wxButton(footerPanel, ID_STORE_INSTALL_DETAIL, "Install", wxDefaultPosition, wxSize(120, 35));
    install_button_footer->SetBackgroundColour(wxColour(50, 150, 200));
    install_button_footer->SetForegroundColour(*wxWHITE);
    install_button_footer->Hide();
    footerSizer->Add(install_button_footer, 0, wxALL, 5);

    footerSizer->Add(new wxButton(footerPanel, wxID_ANY, "<<", wxDefaultPosition, wxSize(30,30)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    footerSizer->Add(new wxButton(footerPanel, wxID_ANY, "<", wxDefaultPosition, wxSize(30,30)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    footerSizer->Add(new wxStaticText(footerPanel, wxID_ANY, "1 / 1"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);
    footerSizer->Add(new wxButton(footerPanel, wxID_ANY, ">", wxDefaultPosition, wxSize(30,30)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    footerSizer->Add(new wxButton(footerPanel, wxID_ANY, ">>", wxDefaultPosition, wxSize(30,30)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    footerPanel->SetSizer(footerSizer);
    mainSizer->Add(footerPanel, 0, wxEXPAND);

    SetSizer(mainSizer);
    Layout();
    CenterOnParent();
}

void LuaScriptsStore::FetchScriptList() {
    // Keeping existing logic but disabled for now in favor of local json
    // to match user request "ver UI" first.
}

void LuaScriptsStore::LoadLocalRepo() {
    available_scripts.clear();
    wxString dataPath = g_gui.getFoundDataDirectory() + "store_repo.json";

    std::ifstream f(dataPath.ToStdString());
    if (!f.is_open()) {
        wxLogError("Failed to open store_repo.json in %s", dataPath);
        return;
    }

    try {
        nlohmann::json j;
        f >> j;

        for (const auto& item : j) {
            StoreScript s;
            s.name = item.value("name", "Unknown Script");
            s.description = item.value("description", "No description provided.");
            s.author = item.value("author", "Unknown");
            s.version = item.value("version", "1.0.0");
            s.downloadUrl = item.value("download_url", "");
            s.filename = item.value("filename", "script.lua");
            s.updateDate = item.value("update_date", "Recently");
            s.rating = item.value("rating", 0);

            available_scripts.push_back(s);
        }
    } catch (const std::exception& e) {
        wxLogError("Failed to parse store_repo.json: %s", e.what());
    }
}

void LuaScriptsStore::RefreshStoreList() {
    list_sizer->Clear(true); // Delete old items

    int idx = 0;
    for (const auto& script : available_scripts) {
        AddScriptItem(scroll_panel, list_sizer, script, idx, idx % 2 != 0);
        idx++;
    }

    scroll_panel->Layout();
}

void LuaScriptsStore::AddScriptItem(wxWindow* parent, wxSizer* parentSizer, const StoreScript& script, int index, bool odd) {
    // Outer shadow-like border panel
    wxPanel* outerPanel = new wxPanel(parent);
    outerPanel->SetBackgroundColour(wxColour(226, 232, 240));
    wxBoxSizer* outerSizer = new wxBoxSizer(wxVERTICAL);

    wxPanel* itemPanel = new wxPanel(outerPanel);
    itemPanel->SetBackgroundColour(*wxWHITE);

    wxBoxSizer* itemSizer = new wxBoxSizer(wxHORIZONTAL);

    // 1. Better Icons
    wxBitmap iconBmp(56, 56);
    {
        wxMemoryDC dc(iconBmp);
        size_t hash = std::hash<std::string>{}(script.name);
        wxColour accent(49, 130, 206);
        dc.SetBrush(wxBrush(accent));
        dc.Clear();
        dc.SetTextForeground(*wxWHITE);
        dc.SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.DrawText(script.name.substr(0,1), 18, 12);
    }
    wxStaticBitmap* icon = new wxStaticBitmap(itemPanel, wxID_ANY, iconBmp);
    itemSizer->Add(icon, 0, wxALL | wxALIGN_CENTER_VERTICAL, 15);

    wxBoxSizer* infoSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* nameLabel = new wxStaticText(itemPanel, wxID_ANY, script.name);
    nameLabel->SetFont(wxFont(13, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    nameLabel->SetForegroundColour(wxColour(45, 55, 72));
    infoSizer->Add(nameLabel, 0, wxBOTTOM, 2);

    wxString authorStr = wxString::Format("By %s ", script.author) + wxString::FromUTF8("•") + wxString::Format(" %s", script.updateDate);
    wxStaticText* authorLabel = new wxStaticText(itemPanel, wxID_ANY, authorStr);
    authorLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    authorLabel->SetForegroundColour(wxColour(113, 128, 150));
    infoSizer->Add(authorLabel, 0);

    itemSizer->Add(infoSizer, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);

    // Status Badge (Mocking 'Trending' etc)
    if (script.rating > 1000) {
        wxPanel* badge = new wxPanel(itemPanel);
        badge->SetBackgroundColour(wxColour(254, 240, 138));
        wxBoxSizer* bs = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* bt = new wxStaticText(badge, wxID_ANY, "TRENDING");
        bt->SetFont(wxFont(7, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        bt->SetForegroundColour(wxColour(133, 77, 14));
        bs->Add(bt, 0, wxALL, 4);
        badge->SetSizer(bs);
        itemSizer->Add(badge, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);
    }

    wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* vL = new wxStaticText(itemPanel, wxID_ANY, "v" + script.version);
    vL->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    vL->SetForegroundColour(wxColour(45, 55, 72));
    rightSizer->Add(vL, 0, wxALIGN_RIGHT | wxBOTTOM, 2);

    wxStaticText* rL = new wxStaticText(itemPanel, wxID_ANY, wxString::Format(wxString::FromUTF8("%d ★"), (int)script.rating));
    rL->SetForegroundColour(wxColour(214, 158, 46));
    rightSizer->Add(rL, 0, wxALIGN_RIGHT);

    itemSizer->Add(rightSizer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 20);
    itemPanel->SetSizer(itemSizer);

    auto bindClick = [&](wxWindow* w) {
        w->Bind(wxEVT_LEFT_DOWN, &LuaScriptsStore::OnItemClick, this);
    };
    bindClick(itemPanel);
    for(auto child : itemPanel->GetChildren()) bindClick(child);

    outerSizer->Add(itemPanel, 1, wxEXPAND);
    outerSizer->SetMinSize(-1, 80);
    outerPanel->SetSizer(outerSizer);

    parentSizer->Add(outerPanel, 0, wxEXPAND | wxBOTTOM, 8);
}

void LuaScriptsStore::ShowListView() {
    current_state = STATE_LIST;
    detail_view_panel->Hide();
    list_view_panel->Show();
    return_button->Hide();
    install_button_footer->Hide();

    // We don't have a headerTitle anymore, maybe just set dialog title
    SetTitle("ScriptsStore");

    Layout();
}

void LuaScriptsStore::ShowDetailView(int index) {
    if (index < 0 || index >= (int)available_scripts.size()) return;

    current_state = STATE_DETAIL;
    selected_script_index = index;
    const auto& script = available_scripts[index];

    list_view_panel->Hide();
    detail_view_panel->Show();
    return_button->Show();
    install_button_footer->Show();

    // Header Labels removed

    detail_sizer->Clear(true);

    // Create Detail Layout
    wxPanel* content = new wxPanel(detail_scroll);
    content->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* mainBox = new wxBoxSizer(wxHORIZONTAL);

    // Left Sidebar
    wxPanel* leftSidebar = new wxPanel(content);
    leftSidebar->SetBackgroundColour(wxColour(220, 240, 255));
    leftSidebar->SetMinSize(wxSize(150, -1));
    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* dateLabel = new wxStaticText(leftSidebar, wxID_ANY, script.updateDate);
    dateLabel->SetForegroundColour(*wxWHITE);
    wxPanel* dateHeader = new wxPanel(leftSidebar);
    dateHeader->SetBackgroundColour(wxColour(50, 110, 160));
    wxBoxSizer* dhSizer = new wxBoxSizer(wxHORIZONTAL);
    dhSizer->Add(dateLabel, 0, wxALL, 5);
    dateHeader->SetSizer(dhSizer);
    leftSizer->Add(dateHeader, 0, wxEXPAND);

    wxStaticText* authLabel = new wxStaticText(leftSidebar, wxID_ANY, script.author);
    authLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    leftSizer->Add(authLabel, 0, wxALL, 10);

    leftSizer->Add(new wxStaticText(leftSidebar, wxID_ANY, wxString::Format("%d points", (int)script.rating)), 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
    leftSizer->Add(new wxStaticText(leftSidebar, wxID_ANY, "8 Versions"), 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    leftSidebar->SetSizer(leftSizer);
    mainBox->Add(leftSidebar, 0, wxEXPAND);

    // Right Content
    wxBoxSizer* rightBox = new wxBoxSizer(wxVERTICAL);

    wxStaticText* nameBig = new wxStaticText(content, wxID_ANY, script.name + " - v" + script.version);
    nameBig->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    rightBox->Add(nameBig, 0, wxALL, 15);

    auto addSection = [&](const wxString& title, const wxString& text, bool isGray = false) {
        rightBox->Add(new wxStaticText(content, wxID_ANY, title), 0, wxLEFT | wxRIGHT | wxTOP, 15);
        wxPanel* p = new wxPanel(content);
        if (isGray) p->SetBackgroundColour(wxColour(230, 230, 230));
        wxBoxSizer* ps = new wxBoxSizer(wxVERTICAL);
        wxStaticText* st = new wxStaticText(p, wxID_ANY, text);
        ps->Add(st, 0, wxALL, 10);
        if (title == "Description:") {
            wxStaticText* link = new wxStaticText(p, wxID_ANY, "README");
            link->SetForegroundColour(wxColour(0, 102, 204));
            ps->Add(link, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        }
        p->SetSizer(ps);
        rightBox->Add(p, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 15);
    };

    addSection("Description:", script.description, true);
    rightBox->Add(new wxStaticText(content, wxID_ANY, "Author(s): " + script.author), 0, wxLEFT | wxRIGHT, 15);
    rightBox->Add(new wxStaticText(content, wxID_ANY, "Categories: Map, Bordering, Nature"), 0, wxLEFT | wxRIGHT | wxTOP, 5);

    addSection("Technical Info", "Language: Lua\nSystems: Windows, Linux, MacOS", true);

    rightBox->Add(new wxStaticText(content, wxID_ANY, "Screenshots:"), 0, wxLEFT | wxRIGHT | wxTOP, 15);
    // Placeholder Image
    wxBitmap shotBmp(400, 200);
    {
        wxMemoryDC dc(shotBmp);
        dc.SetBackground(wxBrush(wxColour(50, 50, 50)));
        dc.Clear();
        dc.SetTextForeground(*wxWHITE);
        dc.DrawText("Screenshot Placeholder", 130, 90);
    }
    rightBox->Add(new wxStaticBitmap(content, wxID_ANY, shotBmp), 0, wxALL, 15);

    mainBox->Add(rightBox, 1, wxEXPAND);
    content->SetSizer(mainBox);
    detail_sizer->Add(content, 1, wxEXPAND);

    detail_view_panel->Layout();
    detail_scroll->Layout();
    Layout();
}

void LuaScriptsStore::OnItemClick(wxMouseEvent& event) {
    wxWindow* win = dynamic_cast<wxWindow*>(event.GetEventObject());
    while (win && win->GetParent() != scroll_panel) {
        win = win->GetParent();
    }

    if (win) {
        // Find index. We stored it in the Refresh loop but we can search or just store in ClientData
        // For now, let's assume we can find it by looking through children of scroll_panel
        int idx = 0;
        for (auto child : scroll_panel->GetChildren()) {
            if (child == win) {
                // Every item has a separator after it, so actual index is idx / 2
                ShowDetailView(idx / 2);
                return;
            }
            idx++;
        }
    }
}

void LuaScriptsStore::OnInstallScript(int scriptIndex) {
    wxMessageBox("Installing " + available_scripts[scriptIndex].name + "...", "Install");
}

void LuaScriptsStore::OnClose(wxCommandEvent& event) {
    Close();
}

void LuaScriptsStore::OnWindowClose(wxCloseEvent& event) {
    instance = nullptr;
    Destroy();
}

void LuaScriptsStore::OnReturn(wxCommandEvent& event) {
    ShowListView();
}

void LuaScriptsStore::OnInstallBtnDetail(wxCommandEvent& event) {
    if (selected_script_index >= 0) {
        OnInstallScript(selected_script_index);
    }
}
