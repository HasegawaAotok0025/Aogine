#pragma once
#include "../Graphics/Object3D.h"

// ============================================================================
// オブジェクトの一覧と、選んだ物の設定を画面から変えるパネル（Debugビルドのみ）。
//
// 置いてある物すべてに共通する項目（位置・回転・拡大・色・α・ブレンドモード・ライティング・UV）を
// 出す。ゲームのコードを書き換えずに、どの物でも調整できる。
// 項目を足したいときはここへ足す。すると全部のゲームで使えるようになる
// ============================================================================

#ifdef USE_IMGUI

// 一覧で選ぶ。selectedIdは選んでいるオブジェクトのid（0は未選択）
inline void DrawObjectInspector(std::vector<Object3D>& objects, uint32_t& selectedId) {
    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Inspector")) { ImGui::End(); return; }

    // ----- 一覧 -----
    ImGui::TextUnformatted("Objects");
    if (ImGui::BeginChild("##objects", ImVec2(0.0f, 130.0f), true)) {
        for (Object3D& obj : objects) {
            std::string label = (obj.name.empty() ? std::string("(no name)") : obj.name) + "##" + std::to_string(obj.id);
            if (ImGui::Selectable(label.c_str(), obj.id == selectedId)) { selectedId = obj.id; }
        }
    }
    ImGui::EndChild();

    Object3D* obj = nullptr;
    for (Object3D& o : objects) { if (o.id == selectedId) { obj = &o; break; } }
    if (!obj) {
        ImGui::TextDisabled("Select an object.");
        ImGui::End();
        return;
    }

    ImGui::Separator();
    ImGui::Text("%s%s", obj->name.c_str(), obj->tag.empty() ? "" : ("  [" + obj->tag + "]").c_str());
    ImGui::Checkbox("visible", &obj->visible);

    // ----- Transform -----
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("translate", &obj->transform.translate.x, 0.05f);
        ImGui::DragFloat3("rotate", &obj->transform.rotate.x, 0.01f);
        ImGui::DragFloat3("scale", &obj->transform.scale.x, 0.02f);
    }

    // ----- Material -----
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Vector4は x,y,z,w が並んだfloat 4つなので、そのままRGBAとして渡せる。wがα
        ImGui::ColorEdit4("color", &obj->materialData->color.x);

        static const char* kBlendNames[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
        int blend = (int)obj->blendMode;
        if (ImGui::Combo("blendMode", &blend, kBlendNames, (int)BlendMode::kCount)) { obj->blendMode = (BlendMode)blend; }
        if (obj->blendMode == BlendMode::kNone && obj->materialData->color.w < 1.0f) {
            ImGui::TextDisabled("blendMode is None: alpha has no effect.");
        }

        static const char* kLightingNames[] = { "None", "Lambert", "HalfLambert" };
        ImGui::Combo("lighting", &obj->lightingMode, kLightingNames, IM_ARRAYSIZE(kLightingNames));
        ImGui::Checkbox("untextured", &obj->untextured);
    }

    // ----- UV -----
    if (ImGui::CollapsingHeader("UV Transform")) {
        ImGui::DragFloat2("uv translate", &obj->uvTransform.translate.x, 0.01f);
        ImGui::DragFloat2("uv scale", &obj->uvTransform.scale.x, 0.01f);
        ImGui::DragFloat("uv rotate", &obj->uvTransform.rotate.z, 0.01f);
    }

    ImGui::End();
}

#endif // USE_IMGUI
