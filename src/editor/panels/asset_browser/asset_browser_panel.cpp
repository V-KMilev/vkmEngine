#include "panels/asset_browser/asset_browser_panel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "panels/asset_browser/asset_kinds.h"
#include "system/render/editor_render_hooks.h"
#include "core/frame_context.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/resource_manager.h"
#include "input/editor_keybinds.h"
#include "ui/asset/asset_drag.h"
#include "actions/asset_drop.h"
#include "ui/style/editor_icons.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"
#include "actions/create_entity.h"
#include "actions/asset_actions.h"
#include "preview/preview_assets.h"
#include "asset_editors/asset_editors.h"
#include "session/asset/file_import.h"
#include "preview/preview_cache.h"
#include "system/audio/audio_system.h"
#include "system/loading/loading_system.h"
#include "import/import_formats.h"
#include "io/asset/asset_library.h"
#include "io/scene/asset_refs.h"
#include "io/scene/scene_assets.h"
#include "io/project/roots.h"
#include "ui/audition_transport.h"
#include "ui/editor_dialogs.h"
#include "ui/asset/asset_labels.h"

namespace Vkm::Engine {

namespace {

// Making an undrawn texture's GPU mirror is a full upload; capped so 4K maps fill in over
// several frames.
constexpr int TEXTURE_UPLOADS_PER_FRAME = 3;

/**
 * @brief The rows of @p kind matching @p filter, loaded or only in @p library, sorted by name.
 *
 * @param kind      Lists what is loaded, and finds a library name already loaded.
 * @param resources What is loaded.
 * @param library   The library's names of the kind; a loaded one is listed once.
 * @param filter    The search; empty matches every row.
 * @param out       Cleared, then filled.
 */
void listRows(
    const AssetKind& kind,
    const ResourceManager& resources,
    const std::vector<std::string>& library,
    const char* filter,
    std::vector<AssetRow>& out
) {
    out.clear();
    kind.rows(resources, [&](const AssetRow& row) {
        if (matchesFilter(row.name.c_str(), filter)) out.push_back(row);
    });
    for (const std::string& name : library) {
        if (!matchesFilter(name.c_str(), filter) || kind.find(resources, name)) continue;
        out.push_back(AssetRow{{}, name, 0, false});
    }
    std::sort(out.begin(), out.end(), [](const AssetRow& a, const AssetRow& b) { return a.name < b.name; });
}

/// The one word a tile says under its name: what keeps the asset from working, or null.
const char* tileProblem(const ResourceManager& resources, AssetType type, const AssetRow& row) {
    if (!row.held) return "not loaded";
    if (type == AssetType::AnimationClip) {
        const AnimationClipAsset& clip = resources.get(AnimationClipHandle{row.key});
        if (!resources.findByName<SkeletonAsset>(clip.skeleton)) return "no rig";
    }
    return nullptr;
}

/**
 * @brief Cut @p caption onto two lines of @p width, at the last of `_ : / .` that fits.
 *
 * @param caption The tile's caption.
 * @param width   Pixels a line may take.
 * @param second  The rest, elided in its middle; empty when the caption fits on one line.
 * @return The first line.
 */
std::string splitCaption(const std::string& caption, float width, std::string& second) {
    second.clear();
    if (ImGui::CalcTextSize(caption.c_str()).x <= width) return caption;
    const auto fits = [&](size_t chars) {
        return ImGui::CalcTextSize(caption.c_str(), caption.c_str() + chars).x <= width;
    };
    size_t cut = 0;
    for (size_t i = 0; i < caption.size(); ++i) {
        const char c = caption[i];
        const bool breaks = c == '_' || c == ':' || c == '/' || c == '.';
        if (breaks && fits(i + 1)) cut = i + 1;
    }
    // No break that fits: as much as fits, then the rest.
    if (cut == 0) {
        while (cut < caption.size() && fits(cut + 1)) ++cut;
    }
    second = elidedLine(caption.c_str() + cut, width);
    return caption.substr(0, cut);
}

/**
 * @brief What a tile calls an asset: its file's stem with what follows it, or a name naming no file whole.
 *
 * "ArmChair_1k:mat0" of "assets/models/ArmChair/ArmChair_1k.gltf:mat0": a model's assets share its
 * folders, which an elided full name would show alone.
 *
 * @param name The asset's whole name, as the tooltip shows it.
 * @return Its caption.
 */
std::string tileCaption(const std::string& name) {
    const std::string stem = assetFileStem(name);
    if (stem.empty()) return name;
    const size_t colon = name.find(':');
    return colon == std::string::npos ? stem : stem + name.substr(colon);
}

/**
 * @brief Load a library row's asset; when it cannot be, a toast says so.
 *
 * @param ec   Its resources take the asset; its toasts take the failure.
 * @param kind Its kind.
 * @param name Its name in the library.
 * @return Its key, or a null one.
 */
StorageIndex loadRow(EditorContext& ec, const AssetKind& kind, const std::string& name) {
    if (!SceneAssets::loadNamed(kind.type, name, ec.frame.resources)) {
        ec.state.toasts.push(ToastKind::Error, "Could not load " + name);
        return {};
    }
    return kind.find(ec.frame.resources, name);
}

/**
 * @brief The square face of an asset tile: its picture, or its glyph on a same-size square.
 *
 * The face stays the last item and allows overlap, so later controls over it are reachable.
 *
 * @param picture 0 shows @p glyph instead.
 * @param face Edge length, pixels.
 * @param glyph Drawn centered on a face with no picture.
 * @param glyphColor A faint one says a picture is still coming.
 * @param[out] faceMin Top-left corner of the face.
 * @param[out] faceMax Bottom-right corner of the face.
 * @return Whether the face was clicked.
 */
bool tileFace(
    ImTextureID picture,
    float face,
    EditorIcon glyph,
    const ImVec4& glyphColor,
    ImVec2& faceMin,
    ImVec2& faceMax
) {
    faceMin = ImGui::GetCursorScreenPos();

    const ImVec4 inert = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    ImGui::SetNextItemAllowOverlap();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, inert);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, inert);
    const bool clicked = picture
        ? ImGui::ImageButton("##face", picture, ImVec2(face, face), ImVec2(0, 1), ImVec2(1, 0))
        : ImGui::Button("##face", ImVec2(face, face));
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    faceMax = ImGui::GetItemRectMax();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) {
        const ImU32 edge = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        dl->AddRect(faceMin, faceMax, edge, 0.0f, 0, EditorStyle::px(2.0f));
    }
    if (!picture) {
        drawEditorIcon(
            dl,
            glyph,
            ImVec2((faceMin.x + faceMax.x) * 0.5f, (faceMin.y + faceMax.y) * 0.5f),
            face * 0.22f,
            ImGui::GetColorU32(glyphColor)
        );
    }
    return clicked;
}

/// What a tile and its tooltip call a row.
const char* shownName(const AssetRow& row) {
    return row.name.empty() ? "(unnamed)" : row.name.c_str();
}

/// Whether an asset of @p type opens in a window: its own, or, for a terrain, the Terrain window.
bool opensWindow(AssetType type) {
    return AssetEditors::opens(type) || type == AssetType::Terrain;
}

/**
 * @brief Open an asset in its own window; a terrain in the Terrain window, on the entity placing it.
 *
 * @param ec   The frame's editor context.
 * @param type The asset's kind, one opensWindow takes.
 * @param key  The loaded asset.
 */
void openAsset(EditorContext& ec, AssetType type, StorageIndex key) {
    EditorState& state = ec.state;
    Scene& scene = ec.frame.scene;
    if (type != AssetType::Terrain) {
        state.openAssetEditor(type, key);
        return;
    }
    EntityId placing{};
    scene.forEach<Terrain>([&](EntityId id, const Terrain& terrain) {
        if (!placing && terrain.terrain == TerrainHandle{key}) placing = id;
    });
    // None yet: one is placed, as dropping it in the viewport would.
    if (!placing) placing = EditorActions::createFromAsset(scene, ec.frame.resources, state, type, key, {});
    if (!placing) return;
    state.selectEntity(placing);
    state.openTerrain(placing);
}

/**
 * @brief Select the entities naming a loaded asset; when none does, a toast says what holds it.
 *
 * @param ec   The frame's editor context.
 * @param kind The asset's kind.
 * @param row  The asset, loaded.
 */
void selectUsers(EditorContext& ec, const AssetKind& kind, const AssetRow& row) {
    const ResourceManager& resources = ec.frame.resources;
    const Scene& scene = ec.frame.scene;
    const Scene* screen = ec.loadingSystem.builtScreen();

    std::vector<uint8_t>  marks;
    std::vector<EntityId> users;
    const auto names = [&](const Scene& in, EntityId id) {
        SceneAssets::EntityAssetRefs refs;
        SceneAssets::collectAssetRefs(in, id, resources, refs);
        std::fill(marks.begin(), marks.end(), uint8_t{0});
        UsedSlots named{marks};
        kind.named(refs, in, resources, named);
        return named.has(row.key.index);
    };
    scene.forEachEntity([&](EntityId id) {
        if (names(scene, id)) users.push_back(id);
    });
    bool onScreen = false;
    if (users.empty() && screen) {
        screen->forEachEntity([&](EntityId id) { onScreen = onScreen || names(*screen, id); });
    }
    // The selection stands when no entity uses it.
    if (users.empty()) {
        const char* why = onScreen
            ? " is used by the project's loading screen, not by an entity"
            : " is held by another asset, not by an entity";
        ec.state.toasts.push(ToastKind::Info, row.name + why);
        return;
    }
    ec.state.selection.clear();
    for (const EntityId id : users) ec.state.selection.add(id);
}

} // namespace

void AssetBrowserPanel::openRename(AssetType kind, StorageIndex key, const std::string& name) {
    snprintf(m_renameBuf, sizeof(m_renameBuf), "%s", name.c_str());
    m_rename = AssetTarget{kind, key, name, true};
}

const std::vector<std::string>& AssetBrowserPanel::libraryNames(AssetType type) {
    const size_t   at  = static_cast<size_t>(type);
    const uint64_t now = AssetLibrary::get().revision() + 1;
    if (m_libraryAt[at] != now) {
        m_library[at]   = AssetLibrary::get().namesOf(type);
        m_libraryAt[at] = now;
    }
    return m_library[at];
}

void AssetBrowserPanel::ensureAssets(ResourceManager& resources) {
    m_sphere = materialPreviewSphere(resources);

    m_neutral = neutralPreviewMaterial(resources);
}

void AssetBrowserPanel::draw(EditorContext& ec) {
    ensureAssets(ec.frame.resources);
    // A row that would not load is tried again by the next gesture, not each frame of a drag.
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_unloadable.clear();

    drawRail(ec);
    ImGui::SameLine();

    ImGui::BeginGroup();
    drawToolbar(ec);
    ImGui::Separator();
    // Ctrl+wheel sizes the tiles rather than scrolling them.
    const bool sizing = ImGui::GetIO().KeyCtrl;
    const ImGuiWindowFlags still = sizing ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None;
    if (ImGui::BeginChild("##abGrid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, still)) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (sizing && wheel != 0.0f && ImGui::IsWindowHovered()) {
            m_cell = std::clamp(m_cell + wheel * CELL_STEP, MIN_CELL, MAX_CELL);
        }
        drawGrid(ec);
    }
    ImGui::EndChild();
    ImGui::EndGroup();

    // At panel scope: OpenPopup hashes against the calling window, so the child's would not match.
    serviceTextureImport(ec);
    serviceSoundImport(ec);
    drawRenameModal(ec);
    drawDeleteModal(ec);
}

void AssetBrowserPanel::drawRail(EditorContext& ec) {
    const float width = EditorStyle::px(150.0f);
    ImGui::BeginChild("##abRail", ImVec2(width, 0));

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Through the grid's filter, so searching narrows the rail too; counted again only once the
    // assets, the library or the search change.
    const RailKey key{ec.frame.resources.revision(), AssetLibrary::get().revision(), m_filter};
    if (key != m_railKey) {
        std::vector<AssetRow> rows;
        for (const AssetKind& kind : ASSET_KINDS) {
            listRows(kind, ec.frame.resources, libraryNames(kind.type), m_filter, rows);
            m_railCounts[static_cast<size_t>(kind.type)] = rows.size();
        }
        m_railKey = key;
    }
    for (const AssetKind& kind : ASSET_KINDS) {
        const size_t count = m_railCounts[static_cast<size_t>(kind.type)];

        ImGui::PushID(static_cast<int>(kind.type));
        const bool selected = m_kind == kind.type;
        if (listRow("kind", kind.icon, kind.label, selected).clicked) m_kind = kind.type;
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        if (selected) {
            const ImU32 bar = ImGui::GetColorU32(EditorStyle::ACCENT);
            dl->AddRectFilled(mn, ImVec2(mn.x + EditorStyle::px(2.0f), mx.y), bar);
        }

        char buf[16];
        snprintf(buf, sizeof(buf), "%zu", count);
        const float textW = ImGui::CalcTextSize(buf).x;
        const ImVec2 countPos(
            mx.x - textW - EditorStyle::px(6.0f),
            mn.y + (mx.y - mn.y - ImGui::GetTextLineHeight()) * 0.5f
        );
        dl->AddText(countPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), buf);
        ImGui::PopID();
    }

    ImGui::EndChild();
}

void AssetBrowserPanel::drawToolbar(EditorContext& ec) {
    const AssetKind* found = kindOf(m_kind);
    if (!found) return;
    const AssetKind& kind = *found;

    // The kind's primary action, always first; see docs/reference/editor.md, "Import".
    if (ImGui::Button(kind.verb)) {
        switch (kind.verbAction) {
            case AssetKind::Verb::NewMaterial:
                if (MaterialHandle h = EditorActions::createNewMaterial(ec.frame.resources, ec.state)) {
                    ec.state.openMaterial(h);
                }
                break;
            case AssetKind::Verb::NewTerrain: {
                const EntityId made = EditorActions::createEntity(
                    ec.frame.scene,
                    ec.frame.resources,
                    ec.state,
                    EditorActions::EntityKind::Terrain
                );
                ec.state.selectEntity(made);
                break;
            }
            case AssetKind::Verb::ImportModel:   ec.state.requests.ask(Ask::ImportModel); break;
            case AssetKind::Verb::ImportTexture: openTextureImport(); break;
            case AssetKind::Verb::ImportSound:   openSoundImport();   break;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kind.verbHint);

    ImGui::SameLine();
    const float searchW = EditorStyle::px(200.0f);
    searchField("##assetFilter", m_filter, sizeof(m_filter), searchW, "Search name or folder");

    ImGui::SameLine();
    toggleChip("Unused", &m_unusedOnly, EditorStyle::ACCENT);
    ImGui::SetItemTooltip("Only what nothing in this scene uses. Ctrl+wheel over the tiles sizes them.");

    // Two ways a clip is inaudible though nothing is wrong; a muted mix even looks like it plays.
    if (m_kind == AssetType::AudioClip) {
        Audio::Device& device = ec.audioSystem.device();
        if (!device.isOpen()) {
            ImGui::SameLine();
            ImGui::TextColored(EditorStyle::WARNING, "No audio device.");
        } else if (device.masterVolume() <= 0.0f) {
            ImGui::SameLine();
            ImGui::TextColored(EditorStyle::WARNING, "Listener volume is 0.");
        }
    }
}

void AssetBrowserPanel::drawGrid(EditorContext& ec) {
    const AssetKind* found = kindOf(m_kind);
    if (!found) return;
    const AssetKind& kind = *found;

    // Snapshotted for the clipper: an off-screen tile bakes no thumbnail.
    std::vector<AssetRow> rows;
    listRows(kind, ec.frame.resources, libraryNames(kind.type), m_filter, rows);
    if (rows.empty()) {
        ImGui::TextDisabled("%s", m_filter[0] ? "Nothing matches the search." : kind.empty);
        return;
    }

    collectRefs(ec);
    std::fill(m_used.begin(), m_used.end(), uint8_t{0});
    UsedSlots used{m_used};
    kind.used(m_refs, ec.frame.scene, ec.frame.resources, used);
    // What nothing uses: a loaded asset no reference names, and the library's unloaded ones.
    if (m_unusedOnly) {
        const auto inUse = [&](const AssetRow& row) { return row.held && used.has(row.key.index); };
        rows.erase(std::remove_if(rows.begin(), rows.end(), inUse), rows.end());
        if (rows.empty()) {
            ImGui::TextDisabled("Everything here is in use.");
            return;
        }
    }

    TileContext tc{ec, m_sphere, m_neutral, TEXTURE_UPLOADS_PER_FRAME};
    const float step = EditorStyle::px(m_cell) + ImGui::GetStyle().ItemSpacing.x;
    const int   cols = (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / step));

    // Clipped by grid line, each one tile tall, as the clipper needs.
    const int count = static_cast<int>(rows.size());
    ImGuiListClipper clipper;
    clipper.Begin((count + cols - 1) / cols);
    while (clipper.Step()) {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
            const int last = (std::min)(count, (line + 1) * cols);
            for (int i = line * cols; i < last; ++i) {
                drawTile(tc, kind, rows[static_cast<size_t>(i)]);
                if (i + 1 < last) ImGui::SameLine();
            }
        }
    }
}

void AssetBrowserPanel::collectRefs(EditorContext& ec) {
    // The project's loading screen uses what it shows, though nothing in the scene does: deleted
    // while it is built, its handle would die under it. Walked every frame in play, which edits
    // outside the history.
    const ResourceManager& resources = ec.frame.resources;
    const Scene& scene = ec.frame.scene;
    const Scene* screen = ec.loadingSystem.builtScreen();
    const std::array<uint64_t, 4> stamp = {
        scene.epoch(),
        resources.revision(),
        ec.state.commands.revision(),
        reinterpret_cast<uintptr_t>(screen)
    };
    if (m_refsKey == stamp && !ec.input.playing) return;

    m_refs = SceneAssets::EntityAssetRefs{};
    scene.forEachEntity([&](EntityId id) {
        SceneAssets::collectAssetRefs(scene, id, resources, m_refs);
    });
    if (screen) {
        screen->forEachEntity([&](EntityId id) {
            SceneAssets::collectAssetRefs(*screen, id, resources, m_refs);
        });
    }
    m_refsKey = stamp;
}

StorageIndex AssetBrowserPanel::takeRow(EditorContext& ec, const AssetKind& kind, const AssetRow& row) {
    if (row.held) return row.key;
    if (row.name == m_unloadable) return {};
    const StorageIndex key = loadRow(ec, kind, row.name);
    if (!key) m_unloadable = row.name;
    return key;
}

void AssetBrowserPanel::drawTile(TileContext& tc, const AssetKind& kind, const AssetRow& row) {
    EditorContext& ec = tc.ec;
    const float face = EditorStyle::px(m_cell);
    const GpuTextureId tex = row.held && kind.thumb ? kind.thumb(tc, row.key, row.version) : 0u;
    const bool orphan = row.held && !UsedSlots{m_used}.has(row.key.index);
    const char* name = shownName(row);

    // By name, so a row keeps its id as it is loaded mid-drag.
    if (row.name.empty()) ImGui::PushID(static_cast<int>(row.key.index));
    else                   ImGui::PushID(row.name.c_str());
    ImGui::BeginGroup();

    // A faint glyph means a bake is coming, unlike a kind with no thumbnail; or, for an asset not
    // loaded, that there is nothing to show until it is.
    ImVec4 glyph = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (kind.thumb || !row.held) glyph.w = 0.30f;
    ImVec2 faceMin, faceMax;
    tileFace(imTexture(tex), face, kind.icon, glyph, faceMin, faceMax);
    // The face's own hover, which a window lying over it takes; not its rectangle's.
    const bool faceHovered = ImGui::IsItemHovered();
    // Into the viewport, onto an asset slot, or onto an entity that takes it.
    if (ImGui::BeginDragDropSource()) {
        if (const StorageIndex key = takeRow(ec, kind, row)) {
            const AssetDrag drag{kind.type, key};
            ImGui::SetDragDropPayload(ASSET_DRAG, &drag, sizeof(drag));
        }
        ImGui::TextUnformatted(name);
        ImGui::EndDragDropSource();
    }
    if (ImGui::IsItemHovered()) {
        if (row.held) {
            char full[256];
            kind.detail(ec.frame.resources, row.key, true, full, sizeof(full));
            ImGui::SetTooltip("%s\n%s%s", name, full, orphan ? "\nNothing in this scene uses it" : "");
        } else {
            ImGui::SetTooltip("%s\nIn the library, not loaded: using it loads it", name);
        }
        // Not Delete: it destroys the selected entity first, so both fire.
        if (row.held && kind.rename && ImGui::IsKeyPressed(RENAME_KEY)) {
            openRename(kind.type, row.key, row.name);
        }
    }

    if (ImGui::BeginPopupContextItem("##tilectx")) {
        drawTileMenu(ec, kind, row, orphan);
        ImGui::EndPopup();
    }

    // On the face, so the tile keeps the common height.
    const bool mine = kind.type == AssetType::AudioClip && m_previewClip == AudioClipHandle{row.key};
    if (kind.type == AssetType::AudioClip && row.held) {
        const float ih    = ImGui::GetFrameHeight();
        const float pad   = EditorStyle::px(5.0f);
        const ImVec2 back = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(faceMin.x + pad, faceMax.y - ih - pad));
        const AudioClipAsset& clip = ec.frame.resources.get(AudioClipHandle{row.key});
        if (auditionTransport("abSnd", ec.audioSystem.device(), m_previewVoice, mine, &clip, ih))
            m_previewClip = AudioClipHandle{row.key};
        ImGui::SetCursorScreenPos(back);
    }

    // A double-click opens an asset in its own window, a material too; one not loaded is loaded by it.
    const bool opens   = opensWindow(kind.type);
    const bool doubled = faceHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (doubled && (opens || !row.held)) {
        const StorageIndex key = takeRow(ec, kind, row);
        if (key && opens) openAsset(ec, kind.type, key);
    }

    drawTileCaption(ec, kind, row, mine);
    ImGui::EndGroup();

    ImGui::PopID();
}

void AssetBrowserPanel::drawTileMenu(
    EditorContext& ec,
    const AssetKind& kind,
    const AssetRow& row,
    bool orphan
) {
    sectionLabel(shownName(row));
    ImGui::Separator();
    if (!row.held && ImGui::MenuItem("Load")) takeRow(ec, kind, row);
    if (opensWindow(kind.type) && ImGui::MenuItem("Open")) {
        if (const StorageIndex key = takeRow(ec, kind, row)) openAsset(ec, kind.type, key);
    }
    if (kind.assignLabel) {
        Scene& scene = ec.frame.scene;
        const EntityId sel = ec.state.selection.active();
        const bool canAssign = sel && scene.isAlive(sel) && assignsTo(kind.type, scene, sel);
        ImGui::BeginDisabled(!canAssign);
        if (ImGui::MenuItem(kind.assignLabel)) {
            if (const StorageIndex key = takeRow(ec, kind, row)) {
                assignAsset(scene, ec.frame.resources, ec.state, sel, kind.type, key);
            }
        }
        ImGui::EndDisabled();
        if (!canAssign) ImGui::TextDisabled("%s", kind.assignHint);
    }
    if (row.held) drawHeldItems(ec, kind, row, orphan);
}

void AssetBrowserPanel::drawHeldItems(
    EditorContext& ec,
    const AssetKind& kind,
    const AssetRow& row,
    bool orphan
) {
    const bool playing = ec.input.playing;
    ImGui::Separator();
    ImGui::BeginDisabled(!kind.rename);
    if (ImGui::MenuItem("Rename...")) openRename(kind.type, row.key, row.name);
    ImGui::EndDisabled();
    if (!kind.rename) ImGui::TextDisabled("%s", kind.noRename);
    // Not during a session: delete discards the undo history, the set-aside one too.
    ImGui::BeginDisabled(!orphan || playing);
    if (ImGui::MenuItem("Delete...")) {
        m_delete = AssetTarget{kind.type, row.key, row.name, true};
    }
    ImGui::EndDisabled();
    if (playing) {
        ImGui::TextDisabled("(stop the session first)");
    } else if (!orphan) {
        ImGui::TextDisabled("(in use - clear the references first)");
    }
    // Who it is in use by: the entities naming it, or, when none does, the loading screen or
    // another asset holding it.
    if (!orphan && ImGui::MenuItem("Select Users")) selectUsers(ec, kind, row);
}

void AssetBrowserPanel::drawTileCaption(
    EditorContext& ec,
    const AssetKind& kind,
    const AssetRow& row,
    bool mine
) {
    // Two lines under the face: the caption, or its first line and what is wrong; while heard,
    // the second is the audition's progress. The rest of what a tile knows is in its tooltip.
    const ResourceManager& resources = ec.frame.resources;
    Audio::Device& device = ec.audioSystem.device();
    const float face = EditorStyle::px(m_cell);

    const std::string caption = tileCaption(row.name);
    const char* problem = tileProblem(resources, kind.type, row);
    const bool heard = mine && device.isVoiceActive(m_previewVoice);
    std::string second;
    const std::string first = problem || heard ? elidedLine(caption.c_str(), face)
                                               : splitCaption(caption, face, second);
    clippedLine(first.c_str(), face, !row.held);
    if (heard) {
        const float length = resources.get(AudioClipHandle{row.key}).duration();
        auditionScrubber("abPos", device, m_previewVoice, length, face);
    } else if (problem) {
        ImGui::TextColored(EditorStyle::WARNING, "%s", problem);
    } else {
        clippedLine(second.empty() ? " " : second.c_str(), face, !row.held);
    }
}

void AssetBrowserPanel::openTextureImport() {
    FilePicker::Options options;
    options.title      = "Import Texture";
    options.root       = Roots::assets();
    options.recursive  = true;
    options.extensions.assign(IMAGE_EXTENSIONS.begin(), IMAGE_EXTENSIONS.end());
    options.hint       = "PNG / JPG / TGA / BMP, read as color (sRGB); Ctrl or Shift picks several";
    options.many       = true;
    m_texturePicker.open(options);
}

void AssetBrowserPanel::serviceTextureImport(EditorContext& ec) {
    std::vector<std::string> picked;
    if (!m_texturePicker.draw(picked)) return;

    for (const std::string& path : picked) FileImport::importTexture(path, ec.frame.resources, ec.state);
}

void AssetBrowserPanel::openSoundImport() {
    FilePicker::Options options;
    options.title      = "Import Sound";
    options.root       = Roots::assets();
    options.recursive  = true;
    options.extensions.assign(AUDIO_EXTENSIONS.begin(), AUDIO_EXTENSIONS.end());
    options.maxResults = 2000;
    options.hint       = "WAV / MP3 / FLAC; Ctrl or Shift picks several";
    options.many       = true;
    m_soundPicker.open(options);
}

void AssetBrowserPanel::serviceSoundImport(EditorContext& ec) {
    std::vector<std::string> picked;
    if (!m_soundPicker.draw(picked)) return;

    for (const std::string& path : picked) FileImport::importSound(path, ec.frame.resources, ec.state);
}

void AssetBrowserPanel::drawRenameModal(EditorContext& ec) {
    const bool renamed = renameDialog("Rename Asset", m_rename.open, m_renameBuf, sizeof(m_renameBuf));
    if (renamed) {
        const AssetKind* kind = kindOf(m_rename.kind);
        if (m_rename.key && kind && kind->rename) {
            kind->rename(ec, m_rename.key, m_rename.name, m_renameBuf);
        }
    }
    // Cleared by any close, so a dismissed dialog leaves no target armed.
    if (!m_rename.open) m_rename.key = {};
}

void AssetBrowserPanel::drawDeleteModal(EditorContext& ec) {
    if (!beginDialog("Delete Asset", m_delete.open)) return;

    ImGui::Text("Delete '%s'?", m_delete.name.c_str());
    // A restored asset would get a new slot, so undo cannot bring it back.
    ImGui::TextDisabled("Undo cannot bring it back.");

    // Orphans only, but the step that cleared the last reference still holds its handle,
    // checked for null not life; so the history goes too.
    const size_t depth = ec.state.commands.undoDepth() + ec.state.commands.redoDepth();
    if (depth > 0) {
        ImGui::TextDisabled(
            "%zu undo step%s will be discarded: a step from before the asset was orphaned still names it.",
            depth,
            depth == 1 ? "" : "s"
        );
    }

    const bool destructive = true;
    const DialogResult r   = dialogButtons(m_delete.open, "Delete", nullptr, true, false, destructive);
    if (r == DialogResult::Confirm && m_delete.key) {
        // The voice holds the samples by shared_ptr: a removed clip would play on, unstoppable.
        if (m_delete.kind == AssetType::AudioClip && m_previewClip == AudioClipHandle{m_delete.key}) {
            ec.audioSystem.device().stopVoice(m_previewVoice);
            m_previewVoice = 0;
            m_previewClip  = {};
        }
        ec.previews.evict(PreviewCache::thumbnailKey(m_delete.kind, m_delete.key.index));
        if (const AssetKind* kind = kindOf(m_delete.kind)) kind->remove(ec.frame.resources, m_delete.key);
        ec.state.forgetWorldState();
        ec.state.markSceneDirty();
    }
    if (r != DialogResult::None) m_delete.key = {};
    endDialog();
}

} // namespace Vkm::Engine
