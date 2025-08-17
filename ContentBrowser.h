#pragma once
#include "..//xrCore/xrstring.h"
#include "..//xrCore/FS.h"

#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include "..//Editors/xrEUI/imgui.h"
#include "..//xrCore/vector.h"

#include "../xrEProps/UIItemListForm.h"
#include "../xrECore/Engine/texture.h"

struct Item {
    xr_string name;
    bool isFolder;
};

class UIContentBrowser {
public:
    UIContentBrowser();
    ~UIContentBrowser();
    void Draw();
    void RefreshList();
    void OnItemClicked(const xr_string& item, bool isFolder);
    void LoadThumbnail(const xr_string& name);
    void AddObjectToScene(const xr_string& itemName, const Fvector& pos);
    bool GetMouseScenePosition(Fvector& pos);

private:
    xr_string m_CurrentPath;
    bool m_RefreshInProgress;
    bool m_Selection;
    xr_string m_SelectedItem;
    xr_string m_SearchQuery;
    xr_string m_PendingObject; // Store selected object path for placement
    bool m_AddButtonClicked; // Flag to track "Add" button click
    UIItemListForm* m_ObjectList;
    ref_texture m_TextureNull;
    ref_texture m_tFolder;
    ref_texture m_tUp;
    xr_vector<Item> m_Items;
    xr_map<xr_string, ImTextureID> m_PreviewCache;
    xr_vector<xr_string> m_CacheOrder;
};