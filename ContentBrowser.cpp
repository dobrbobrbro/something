#include "stdafx.h"
#include "ContentBrowser.h"
#include "../Editors/xrEProps/Tree/Choose/UIChooseForm.h"
#include "../Editors/xrECore/Editor/Library.h"
#include "../Editors/LevelEditor/UI/Tools/UIObjectTool.h"
#include "../xrCore/FS.h"
#include <algorithm>
#include <windows.h>
#include <commdlg.h>


UIContentBrowser::UIContentBrowser()
{
    m_CurrentPath.clear();
    m_RefreshInProgress = false;
    m_Selection = false;
    m_SelectedItem.clear();
    m_SearchQuery.clear();
    m_PendingObject.clear();
    m_AddButtonClicked = false;
    m_ObjectList = xr_new<UIItemListForm>();
    m_TextureNull.create("\\ed\\ed_nodata");
    m_TextureNull->Load();
    if (FS.exist("$game_textures$", "ed\\bar\\rollic\\contentbrowser\\folder.dds"))
        m_tFolder.create("ed\\bar\\rollic\\contentbrowser\\folder.dds");
    else
        m_tFolder.create("$game_textures$\\ed\\folder");
    m_tFolder->Load();

    if (FS.exist("$game_textures$", "ed\\bar\\rollic\\contentbrowser\\up.dds"))
        m_tUp.create("ed\\bar\\rollic\\contentbrowser\\up");
    else
        m_tUp.create("$game_textures$\\ed\\ed_nodata");
    m_tUp->Load();

    RefreshList();
}

UIContentBrowser::~UIContentBrowser()
{
    for (auto& pair : m_PreviewCache)
    {
        if (pair.second && pair.second != m_TextureNull->surface_get())
        {
            // Replace with engine-specific texture release if available
        }
    }
    m_PreviewCache.clear();
    m_TextureNull.destroy();
    m_tFolder.destroy();
    m_tUp.destroy();
    xr_delete(m_ObjectList);
}

void UIContentBrowser::Draw()
{
    static bool showWindow = true;
    ImGui::Begin("Content Browser", &showWindow, ImGuiWindowFlags_None);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::Columns(2, nullptr, true);
    ImGui::SetColumnWidth(0, 230.0f);

    // ==== Левая колонка: список ====
    {
        ImGui::BeginChild("Object List", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginGroup();

        float search_width = 208.0f;
        ImGui::SetNextItemWidth(search_width);
        char buffer[256];
        xr_strcpy(buffer, m_SearchQuery.c_str());
        if (ImGui::InputTextWithHint("##Search", "Search", buffer, sizeof(buffer))) {
            m_SearchQuery = buffer;
            RefreshList();
        }

        ImGui::EndGroup();
        ImGui::Separator();

        ImGui::BeginChild("ScrollableList", ImVec2(0, 0), false);
        m_ObjectList->Draw();
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::EndGroup();
        ImGui::EndChild();
        // Синхронизация: из левого списка правый
        RStringVec selectedItems;
        if (m_ObjectList->GetSelected(selectedItems) && !selectedItems.empty()) {
            xr_string sel = selectedItems[0].c_str();
            if (m_SelectedItem != sel) {
                m_SelectedItem = sel;
                // Update UIObjectTool with the selected item
                ESceneObjectTool* objTool = dynamic_cast<ESceneObjectTool*>(Scene->GetTool(OBJCLASS_SCENEOBJECT));
                if (objTool && objTool->pForm) {
                    UIObjectTool* uiObjTool = dynamic_cast<UIObjectTool*>(objTool->pForm);
                    if (uiObjTool) {
                        // ИСПРАВЛЕНО: Формируем полный путь, НЕ удаляя расширение
                        xr_string fullPath = m_CurrentPath;
                        if (!fullPath.empty()) fullPath += "\\";
                        fullPath += m_SelectedItem;
                        uiObjTool->SetCurrent(fullPath.c_str());
                    }
                }
            }
        }
    }

    // ==== Правая колонка: превью ====
    ImGui::NextColumn();
    ImGui::Text("Path: %s", m_CurrentPath.c_str());

    if (ImGui::ImageButton(m_tUp->surface_get(), ImVec2(20, 20)) && !m_CurrentPath.empty())
    {
        size_t pos = m_CurrentPath.find_last_of('\\', m_CurrentPath.length() - 2);
        m_CurrentPath = pos != xr_string::npos ? m_CurrentPath.substr(0, pos) : "";
        m_PreviewCache.clear();
        m_CacheOrder.clear();
        m_SelectedItem.clear();
        m_PendingObject.clear();
        RefreshList();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(80, 20)))
    {
        m_PreviewCache.clear();
        m_CacheOrder.clear();
        m_SelectedItem.clear();
        m_PendingObject.clear();
        RefreshList();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add", ImVec2(80, 20)) && !m_SelectedItem.empty() && !m_Selection)
    {
        m_Selection = true;
        m_AddButtonClicked = true;
        // ИСПРАВЛЕНО: Формируем полный путь, НЕ удаляя расширение для ChooseForm
        xr_string fullPath = m_CurrentPath;
        if (!fullPath.empty()) fullPath += "\\";
        fullPath += m_SelectedItem;
        UIChooseForm::SelectItem(smObject, 1, fullPath.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Import", ImVec2(80, 20)))
    {
        OPENFILENAME ofn;
        char szFile[260 * 16] = { 0 };
        ZeroMemory(&ofn, sizeof(OPENFILENAME));
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "All Files\0*.*\0Object Files\0*.ogf;*.object\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

        if (GetOpenFileName(&ofn))
        {
            xr_string destDir = m_CurrentPath;
            if (!destDir.empty()) destDir += "\\";

            CreateDirectoryA((FS.get_path("$fs_root$")->m_Path + destDir).c_str(), NULL);

            char* fileName = szFile;
            xr_string sourcePath = fileName;
            fileName += sourcePath.length() + 1;

            if (*fileName == '\0') // Single file
            {
                size_t lastSlash = sourcePath.find_last_of('\\');
                xr_string fileOnly = (lastSlash != xr_string::npos) ? sourcePath.substr(lastSlash + 1) : sourcePath;
                xr_string destPath = destDir + fileOnly;
                if (CopyFileA(sourcePath.c_str(), (FS.get_path("$fs_root$")->m_Path + destPath).c_str(), FALSE))
                {
                    RefreshList();
                }
                else
                {
                    ELog.DlgMsg(mtError, "Failed to copy file: %s, Error: %d", fileOnly.c_str(), GetLastError());
                }
            }
            else // Multiple files
            {
                while (*fileName != '\0')
                {
                    xr_string fileOnly = fileName;
                    xr_string fullSourcePath = sourcePath + "\\" + fileOnly;
                    xr_string destPath = destDir + fileOnly;
                    if (CopyFileA(fullSourcePath.c_str(), (FS.get_path("$fs_root$")->m_Path + destPath).c_str(), FALSE))
                    {
                        RefreshList();
                    }
                    else
                    {
                        ELog.DlgMsg(mtError, "Failed to copy file: %s, Error: %d", fileOnly.c_str(), GetLastError());
                    }
                    fileName += fileOnly.length() + 1;
                }
            }
        }
    }
    ImGui::BeginChild("Content", ImVec2(0, 0), true);
    const float tileSize = 100.0f;
    int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (tileSize + 10)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
    if (ImGui::BeginTable("ContentTable", columns, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX))
    {
        for (auto& item : m_Items)
        {
            ImGui::TableNextColumn();
            ImGui::BeginGroup();
            ImGui::PushID(item.name.c_str());

            // --- размеры общей выделяемой области (thumbnail + подпись) ---
            const float pad = 5.0f;
            ImVec2 selectableSize(tileSize + 2 * pad, tileSize + ImGui::GetTextLineHeightWithSpacing() + 2 * pad);
            bool isSelected = (m_SelectedItem == item.name);
            // 1) Большая выделяемая зона: СЕРАЯ ПОДСВЕТКА на всю плитку
            if (ImGui::Selectable("##selectable",
                isSelected,
                ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowItemOverlap,
                selectableSize))
            {

                m_SelectedItem = item.name;

                // --- НЕ УДАЛЯТЬ: синхронизация с левым списком ---
                m_ObjectList->SelectItem(item.name.c_str());
                // --- НЕ УДАЛЯТЬ: обновление UIObjectTool выбранного элемента ---
                if (ESceneObjectTool* objTool = dynamic_cast<ESceneObjectTool*>(Scene->GetTool(OBJCLASS_SCENEOBJECT)))
                    if (objTool->pForm)
                        if (UIObjectTool* uiObjTool = dynamic_cast<UIObjectTool*>(objTool->pForm))

                        {
                            // ИСПРАВЛЕНО: Формируем полный путь, НЕ удаляя расширение
                            xr_string fullPath = m_CurrentPath;
                            if (!fullPath.empty()) fullPath += "\\";
                            fullPath += m_SelectedItem;
                            uiObjTool->SetCurrent(fullPath.c_str());
                        }

                if (ImGui::IsMouseDoubleClicked(0))
                    OnItemClicked(item.name, item.isFolder);
            }

            // 2) Рисуем содержимое ВНУТРИ выделенной зоны
            ImVec2 rectMin = ImGui::GetItemRectMin();
            ImVec2 cur = rectMin;         // верхний-левый угол выделения
            cur.x += pad;
            cur.y += pad;

            // --- картинка (thumbnail) ---
            ImGui::SetCursorScreenPos(cur);
            ImTextureID iconID = item.isFolder ? m_tFolder->surface_get() : m_TextureNull->surface_get();
            if (!item.isFolder)
            {
                auto it = m_PreviewCache.find(item.name);
                if (it != m_PreviewCache.end())
                    iconID = it->second;
            }

            if (ImGui::ImageButton(iconID, ImVec2(tileSize, tileSize)))
            {
                m_SelectedItem = item.name;
                // --- НЕ УДАЛЯТЬ: синхронизация с левым списком ---
                m_ObjectList->SelectItem(item.name.c_str());
                // --- НЕ УДАЛЯТЬ: обновление UIObjectTool выбранного элемента ---
                if (ESceneObjectTool* objTool = dynamic_cast<ESceneObjectTool*>(Scene->GetTool(OBJCLASS_SCENEOBJECT)))
                    if (objTool->pForm)
                        if (UIObjectTool* uiObjTool = dynamic_cast<UIObjectTool*>(objTool->pForm))

                        {
                            // ИСПРАВЛЕНО: Формируем полный путь, НЕ удаляя расширение
                            xr_string fullPath = m_CurrentPath;
                            if (!fullPath.empty()) fullPath += "\\";
                            fullPath += m_SelectedItem;
                            uiObjTool->SetCurrent(fullPath.c_str());
                        }

                if (!item.isFolder && !m_Selection)
                {
                    m_Selection = true;
                    // ИСПРАВЛЕНО: Формируем полный путь, НЕ удаляя расширение для ChooseForm
                    xr_string fullPath = m_CurrentPath;
                    if (!fullPath.empty()) fullPath += "\\";
                    fullPath += item.name;
                    UIChooseForm::SelectItem(smObject, 1, fullPath.c_str());
                }
                else if (item.isFolder)
                {
                    OnItemClicked(item.name, item.isFolder);
                }
            }

            // --- подпись под картинкой ---
            ImGui::SetCursorScreenPos(ImVec2(cur.x, cur.y + tileSize + 2.0f));
            ImGui::TextWrapped("%s", item.name.c_str());

            ImGui::PopID();
            ImGui::EndGroup();

        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::EndChild();
    ImGui::Columns(1);
    ImGui::PopStyleVar();

    // ==== Обработка результата выбора ====
    if (m_Selection)
    {
        bool change = false;
        xr_string result; // Сюда приходит "чистое" имя, например, "bb"
        if (UIChooseForm::GetResult(change, result))
        {
            if (change)
            {
                // ИСПРАВЛЕНО: Восстанавливаем полное имя файла, если расширение отсутствует
                xr_string finalPath = result;
                
                // Проверяем, есть ли в имени точка (признак расширения)
                if (strrchr(finalPath.c_str(), '.') == nullptr)
                {
                    // Расширения нет. Пробуем добавить стандартные.
                    xr_string path_with_object_ext = finalPath + ".object";
                    if (FS.exist("$fs_root$", path_with_object_ext.c_str()) || FS.exist("$game_data$", path_with_object_ext.c_str()))
                    {
                        finalPath = path_with_object_ext;
                    }
                    else
                    {
                        xr_string path_with_ogf_ext = finalPath + ".ogf";
                        if (FS.exist("$fs_root$", path_with_ogf_ext.c_str()) || FS.exist("$game_data$", path_with_ogf_ext.c_str()))
                        {
                            finalPath = path_with_ogf_ext;
                        }
                    }
                }

                // Теперь используем finalPath, который должен содержать расширение
                xr_string normalizedResult = result;
                while (!normalizedResult.empty() && (normalizedResult[0] == '\\' || normalizedResult[0] == '/'))
                    normalizedResult.erase(0, 1);
                std::replace(normalizedResult.begin(), normalizedResult.end(), '/', '\\');

                if (FS.exist("$fs_root$", normalizedResult.c_str()) || FS.exist("$game_data$", normalizedResult.c_str()))
                {
                    m_PendingObject = normalizedResult;
                    // Update UIObjectTool with the selected item
                    ESceneObjectTool* objTool = dynamic_cast<ESceneObjectTool*>(Scene->GetTool(OBJCLASS_SCENEOBJECT));
                    if (objTool && objTool->pForm) {
                        UIObjectTool* uiObjTool = dynamic_cast<UIObjectTool*>(objTool->pForm);
                        if (uiObjTool) {
                            uiObjTool->SetCurrent(m_PendingObject.c_str());
                        }
                    }
                    // Add object to scene immediately after selection
                    Fvector pos = { 0.f, 0.f, 0.f };
                    if (GetMouseScenePosition(pos))
                    {
                        AddObjectToScene(m_PendingObject, pos);
                        m_PendingObject.clear();
                    }
                }
                else
                {
                    // В сообщении об ошибке выводим исходное имя, чтобы было понятно, что не нашлось
                    ELog.DlgMsg(mtError, "Selected object does not exist: %s", normalizedResult.c_str());
                }
            }
            m_Selection = false;
        }
        UIChooseForm::Update();
    }

    // ==== Размещение объекта только при явном клике мыши, если не была нажата кнопка Add ====
    if (!m_PendingObject.empty() && Tools->GetAction() == etaAdd && ImGui::IsMouseClicked(0) && !m_AddButtonClicked)
    {
        // Validate m_PendingObject before adding
        xr_string normalizedPath = m_PendingObject;
        while (!normalizedPath.empty() && (normalizedPath[0] == '\\' || normalizedPath[0] == '/'))
            normalizedPath.erase(0, 1);
        std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');
        if (FS.exist("$fs_root$", normalizedPath.c_str()) || FS.exist("$game_data$", normalizedPath.c_str()))
        {
            Fvector pos = { 0.f, 0.f, 0.f };
            if (GetMouseScenePosition(pos))
            {
                AddObjectToScene(m_PendingObject, pos);
                m_PendingObject.clear();
            }
        }
        else
        {
            ELog.DlgMsg(mtError, "Cannot place object, file does not exist: %s", normalizedPath.c_str());
            m_PendingObject.clear();
        }
    }

    m_AddButtonClicked = false;
    ImGui::End();
}

void UIContentBrowser::RefreshList()
{
    if (m_RefreshInProgress)
        return;

    m_RefreshInProgress = true;
    m_Items.clear();
    ListItemsVec items;
    FS_FileSet lst;
    if (Lib.GetObjects(lst))
    {
        for (const auto& file : lst)
        {
            xr_string relativeName = file.name;
            if (!m_CurrentPath.empty())
            {
                if (relativeName.find(m_CurrentPath + "\\") != 0)
                    continue;
                relativeName = relativeName.substr(m_CurrentPath.length() + 1);
            }

            size_t pos = relativeName.find('\\');
            Item item;
            item.isFolder = pos != xr_string::npos;
            item.name = item.isFolder ? relativeName.substr(0, pos) : relativeName;
            if (!m_SearchQuery.empty() && relativeName.find(m_SearchQuery) == xr_string::npos)
                continue;
            auto it = std::find_if(m_Items.begin(), m_Items.end(),
                [&item](const Item& i) { return i.name == item.name && i.isFolder == item.isFolder; });
            if (it == m_Items.end())
                m_Items.push_back(item);
            if (!item.isFolder)
                LHelper().CreateItem(items, relativeName.c_str(), 0, ListItem::flDrawThumbnail, 0);
        }
    }

    std::sort(m_Items.begin(), m_Items.end(),
        [](const Item& a, const Item& b)
        {
            if (a.isFolder == b.isFolder)
                return a.name < b.name;
            return a.isFolder > b.isFolder;
        });
    for (auto& item : m_Items)
        if (!item.isFolder)
            LoadThumbnail(item.name);
    m_ObjectList->AssignItems(items);
    m_RefreshInProgress = false;
}

void UIContentBrowser::OnItemClicked(const xr_string& item, bool isFolder)
{
    if (isFolder && !item.empty())
    {
        xr_string newPath = m_CurrentPath;
        if (!newPath.empty())
            newPath += "\\";
        newPath += item;
        FS_FileSet lst;
        if (Lib.GetObjects(lst))
        {
            bool pathExists = false;
            for (const auto& file : lst)
            {
                if (file.name.find(newPath + "\\") == 0 || file.name == newPath)
                {
                    pathExists = true;
                    break;
                }
            }
            if (pathExists)
            {
                m_PreviewCache.clear();
                m_CacheOrder.clear();
                m_CurrentPath = newPath;
                m_SelectedItem.clear();
                m_PendingObject.clear();
                RefreshList();
            }
        }
    }
    else if (!isFolder)
    {
        m_SelectedItem = item;
        ESceneObjectTool* objTool = dynamic_cast<ESceneObjectTool*>(Scene->GetTool(OBJCLASS_SCENEOBJECT));
        if (objTool && objTool->pForm) {
            UIObjectTool* uiObjTool = dynamic_cast<UIObjectTool*>(objTool->pForm);
            if (uiObjTool) {
                // ИСПРАВЛЕНО: Формируем полный путь, НЕ удаляя расширение
                xr_string fullPath = m_CurrentPath;
                if (!fullPath.empty()) fullPath += "\\";
                fullPath += m_SelectedItem;
                  uiObjTool->SetCurrent(m_SelectedItem.c_str());
            }
        }
    }
}

void UIContentBrowser::LoadThumbnail(const xr_string& name)
{
    if (m_PreviewCache.find(name) != m_PreviewCache.end())
        return;
    ImTextureID imgID = nullptr;
    SChooseEvents* e = UIChooseForm::GetEvents(smObject);
    if (e && !e->on_get_texture.empty())
    {
        xr_string fullPath = m_CurrentPath;
        if (!fullPath.empty())
            fullPath += "\\";
        fullPath += name;
        e->on_get_texture(fullPath.c_str(), imgID);
    }

    if (!imgID)
    {
        ref_texture tex;
        xr_string textureName = name;
        if (textureName.size() >= 4 && textureName.substr(textureName.size() - 4) == ".thm")
            textureName = textureName.substr(0, textureName.size() - 4);
        xr_string basePath = "rawdata\\objects\\";
        if (!m_CurrentPath.empty())
            basePath += m_CurrentPath + "\\";
        basePath += textureName;

        xr_string thmPath = basePath + ".thm";
        xr_string ddsPath = basePath + ".dds";
        xr_string tgaPath = basePath + ".tga";

        if (FS.exist("$fs_root$", thmPath.c_str()))
        {
            EImageThumbnail* thm = ImageLib.CreateThumbnail(basePath.c_str(), EImageThumbnail::ETObject);
            if (thm)
            {
                thm->Update(imgID);
                xr_delete(thm);
            }
        }
        else if (FS.exist("$game_textures$", ddsPath.c_str()))
        {
            tex.create(ddsPath.c_str());
            imgID = tex->surface_get();
        }
        else if (FS.exist("$game_textures$", tgaPath.c_str()))
        {
            tex.create(tgaPath.c_str());
            imgID = tex->surface_get();
        }
    }

    if (imgID)
    {
        m_PreviewCache[name] = imgID;
        m_CacheOrder.push_back(name);
        if (m_CacheOrder.size() > 50)
        {
            auto oldest = m_CacheOrder.front();
            auto it = m_PreviewCache.find(oldest);
            if (it != m_PreviewCache.end() && it->second && it->second != m_TextureNull->surface_get())
            {
                // Replace with engine-specific texture release if available
            }
            m_PreviewCache.erase(oldest);
            m_CacheOrder.erase(m_CacheOrder.begin());
        }
    }
}

void UIContentBrowser::AddObjectToScene(const xr_string& itemName, const Fvector& pos)
{
    if (itemName.empty())
        return;
    // Normalize path for engine compatibility
    xr_string normalizedPath = itemName;
    while (!normalizedPath.empty() && (normalizedPath[0] == '\\' || normalizedPath[0] == '/'))
        normalizedPath.erase(0, 1);
    std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');

    // Check if the file exists in the engine's file system
    if (!FS.exist("$fs_root$", normalizedPath.c_str()) && !FS.exist("$game_data$", normalizedPath.c_str()))
    {
        ELog.DlgMsg(mtError, "Object file does not exist: %s", normalizedPath.c_str());
        return;
    }

    string256 namebuffer;
    Scene->GenObjectName(OBJCLASS_SCENEOBJECT, namebuffer, normalizedPath.c_str());
    CSceneObject* obj = xr_new<CSceneObject>((LPVOID)0, namebuffer);
    CEditableObject* ref = obj->SetReference(normalizedPath.c_str());
    if (!ref)
    {
        ELog.DlgMsg(mtError, "Failed to load object: %s", normalizedPath.c_str());
        xr_delete(obj);
        return;
    }

    Fvector up = { 0.f, 1.f, 0.f };
    obj->MoveTo(pos, up);
    Scene->AppendObject(obj);
    Scene->SelectObjects(false, OBJCLASS_SCENEOBJECT);
    obj->Select(true);
    // Update UIObjectTool with the selected item
    ESceneObjectTool* objTool = dynamic_cast<ESceneObjectTool*>(Scene->GetTool(OBJCLASS_SCENEOBJECT));
    if (objTool && objTool->pForm) {
        UIObjectTool* uiObjTool = dynamic_cast<UIObjectTool*>(objTool->pForm);
        if (uiObjTool) {
            uiObjTool->SetCurrent(normalizedPath.c_str());
        }
    }
}

bool UIContentBrowser::GetMouseScenePosition(Fvector& pos)
{
    Ivector2 mousePos = UI->GetRenderMousePosition();
    Fvector start, dir;
    EDevice->m_Camera.MouseRayFromPoint(start, dir, mousePos);
    float dist = 1000.f;
    Fvector hit;

    if (Scene->RayPick(start, dir, dist, &hit, nullptr))
    {
        pos = hit;
        return true;
    }

    return false;
}
