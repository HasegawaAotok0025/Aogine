#pragma once
#include "../Graphics/Object3D.h"
#include <map>

// ============================================================================
// デバッグビューで変えた内容の保存と読み込み。
//
// 置く物はコード（SetupScene）で作るので、画面で動かした結果は、
// 保存しておかないと次の起動で消えてしまう。
// ここでは「コードで作った値から変わった物」だけを、名前をキーにして
// テキストファイルへ書き、起動時にコードで作ったあとへ重ねる。
//
// 変えていない物は書かない。全部書いてしまうと、物理で動いただけの位置まで
// 「置き場所」として残ってしまう
// ============================================================================

// 画面から変えられる項目一式
struct ObjectEdit {
    bool visible = true;
    Vector3 translate{}, rotate{}, scale{};
    Vector4 color{};
    int blendMode = 0;
    int lightingMode = 0;
    bool untextured = true;
    Vector3 uvScale{}, uvRotate{}, uvTranslate{};

    static ObjectEdit From(const Object3D& obj) {
        ObjectEdit e;
        e.visible = obj.visible;
        e.translate = obj.transform.translate;
        e.rotate = obj.transform.rotate;
        e.scale = obj.transform.scale;
        e.color = obj.materialData->color;
        e.blendMode = (int)obj.blendMode;
        e.lightingMode = obj.lightingMode;
        e.untextured = obj.untextured;
        e.uvScale = obj.uvTransform.scale;
        e.uvRotate = obj.uvTransform.rotate;
        e.uvTranslate = obj.uvTransform.translate;
        return e;
    }

    void ApplyTo(Object3D& obj) const {
        obj.visible = visible;
        obj.transform.translate = translate;
        obj.transform.rotate = rotate;
        obj.transform.scale = scale;
        obj.materialData->color = color;
        if (blendMode >= 0 && blendMode < (int)BlendMode::kCount) { obj.blendMode = (BlendMode)blendMode; }
        obj.lightingMode = lightingMode;
        obj.untextured = untextured;
        obj.uvTransform.scale = uvScale;
        obj.uvTransform.rotate = uvRotate;
        obj.uvTransform.translate = uvTranslate;
    }

    bool operator==(const ObjectEdit& o) const {
        auto same3 = [](const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
        return visible == o.visible && same3(translate, o.translate) && same3(rotate, o.rotate) && same3(scale, o.scale)
            && color.x == o.color.x && color.y == o.color.y && color.z == o.color.z && color.w == o.color.w
            && blendMode == o.blendMode && lightingMode == o.lightingMode && untextured == o.untextured
            && same3(uvScale, o.uvScale) && same3(uvRotate, o.uvRotate) && same3(uvTranslate, o.uvTranslate);
    }
    bool operator!=(const ObjectEdit& o) const { return !(*this == o); }
};

// 保存の宛先（実行時のフォルダ＝プロジェクトのフォルダ）
inline constexpr const char* kObjectEditsFile = "saved_edits.txt";

// 物の識別。名前が同じ物が並ぶことがあるので、「同じ名前の何番目か」を添える
inline std::vector<std::string> MakeObjectEditKeys(const std::vector<Object3D>& objects) {
    std::vector<std::string> keys;
    keys.reserve(objects.size());
    std::unordered_map<std::string, int> count;
    for (const Object3D& obj : objects) {
        int n = count[obj.name]++;
        keys.push_back(obj.name + "#" + std::to_string(n));
    }
    return keys;
}

// 1行＝1つの物。タブ区切り。先頭がキー
inline void SaveObjectEdits(const std::map<std::string, ObjectEdit>& edits, const char* path = kObjectEditsFile) {
    std::ofstream file(path, std::ios::trunc);
    if (!file) { return; }
    file << "# CG3 saved edits (key, visible, translate, rotate, scale, color, blend, lighting, untextured, uvScale, uvRotate, uvTranslate)\n";
    file.precision(9);
    for (const auto& [key, e] : edits) {
        file << key << '\t' << (e.visible ? 1 : 0) << '\t'
             << e.translate.x << ' ' << e.translate.y << ' ' << e.translate.z << '\t'
             << e.rotate.x << ' ' << e.rotate.y << ' ' << e.rotate.z << '\t'
             << e.scale.x << ' ' << e.scale.y << ' ' << e.scale.z << '\t'
             << e.color.x << ' ' << e.color.y << ' ' << e.color.z << ' ' << e.color.w << '\t'
             << e.blendMode << '\t' << e.lightingMode << '\t' << (e.untextured ? 1 : 0) << '\t'
             << e.uvScale.x << ' ' << e.uvScale.y << ' ' << e.uvScale.z << '\t'
             << e.uvRotate.x << ' ' << e.uvRotate.y << ' ' << e.uvRotate.z << '\t'
             << e.uvTranslate.x << ' ' << e.uvTranslate.y << ' ' << e.uvTranslate.z << '\n';
    }
}

inline std::map<std::string, ObjectEdit> LoadObjectEdits(const char* path = kObjectEditsFile) {
    std::map<std::string, ObjectEdit> edits;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') { continue; }
        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string col;
        while (std::getline(ss, col, '\t')) { cols.push_back(col); }
        if (cols.size() < 12) { continue; } // 壊れた行は読み飛ばす

        ObjectEdit e;
        int flag = 0;
        std::stringstream(cols[1]) >> flag; e.visible = flag != 0;
        std::stringstream(cols[2]) >> e.translate.x >> e.translate.y >> e.translate.z;
        std::stringstream(cols[3]) >> e.rotate.x >> e.rotate.y >> e.rotate.z;
        std::stringstream(cols[4]) >> e.scale.x >> e.scale.y >> e.scale.z;
        std::stringstream(cols[5]) >> e.color.x >> e.color.y >> e.color.z >> e.color.w;
        std::stringstream(cols[6]) >> e.blendMode;
        std::stringstream(cols[7]) >> e.lightingMode;
        std::stringstream(cols[8]) >> flag; e.untextured = flag != 0;
        std::stringstream(cols[9]) >> e.uvScale.x >> e.uvScale.y >> e.uvScale.z;
        std::stringstream(cols[10]) >> e.uvRotate.x >> e.uvRotate.y >> e.uvRotate.z;
        std::stringstream(cols[11]) >> e.uvTranslate.x >> e.uvTranslate.y >> e.uvTranslate.z;
        edits[cols[0]] = e;
    }
    return edits;
}
