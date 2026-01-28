#ifndef RME_LUA_SCRIPTS_STORE_H
#define RME_LUA_SCRIPTS_STORE_H

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <vector>
#include <string>

struct StoreScript {
	std::string name;
	std::string description;
	std::string author;
	std::string version;
	std::string downloadUrl;
	std::string filename; // Expected filename when saved
	std::string updateDate;
	int rating; // 0-5 or number of votes
	wxBitmap icon; // Helper for UI
};

class LuaScriptsStore : public wxDialog {
public:
	static void ShowStore(wxWindow* parent);
	virtual ~LuaScriptsStore();

private:
	LuaScriptsStore(wxWindow* parent);

	enum ViewState {
		STATE_LIST,
		STATE_DETAIL
	};

	void BuildUI();
	void RefreshStoreList();
	void FetchScriptList();
	void LoadLocalRepo();
	void OnInstallScript(int scriptIndex);
	void OnClose(wxCommandEvent& event);
	void OnWindowClose(wxCloseEvent& event);
	void OnReturn(wxCommandEvent& event);
	void OnInstallBtnDetail(wxCommandEvent& event);
	void AddScriptItem(wxWindow* parent, wxSizer* parentSizer, const StoreScript& script, int index, bool odd);
	void ShowDetailView(int scriptIndex);
	void ShowListView();
	void OnItemClick(wxMouseEvent& event);

	static LuaScriptsStore* instance;
	ViewState current_state;
	int selected_script_index;

	wxPanel* main_content_container;
	wxPanel* list_view_panel;
	wxPanel* detail_view_panel;

	// List view elements
	wxScrolledWindow* scroll_panel;
	wxBoxSizer* list_sizer;

	// Detail view elements
	wxScrolledWindow* detail_scroll;
	wxBoxSizer* detail_sizer;

	// Footer buttons
	wxButton* return_button;
	wxButton* install_button_footer;

	// Dummy data and state
	wxTextCtrl* search_box;
	std::vector<StoreScript> available_scripts;

	DECLARE_EVENT_TABLE()
};

#endif // RME_LUA_SCRIPTS_STORE_H
