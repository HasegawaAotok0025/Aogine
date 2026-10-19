// cspell:ignore dxgi dxguid dxcapi IDxc IDXGI DXGI hlsl wvp HLSL
#pragma once
#pragma warning(disable: 4819) // Unicode文字に関する警告を無効化
#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <charconv> // objの番号を、例外を投げずに数値へ変換するのに使う
#include <cmath>
#include <utility>
#include <chrono>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <cassert>
#include <wrl.h> // ComPtr（COMオブジェクトの解放忘れ・二重解放を防ぐスマートポインタ）

#include <dxcapi.h>
#include <DirectXTex.h>
#include "externals/DirectXTex/d3dx12.h"
#include <vector>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <array>
#include <functional>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
#endif

#include "../Core/MathUtility.h"
#include "Camera.h"
#include "../Core/AudioEngine.h"
#include "../Core/InputEngine.h"
#include "../Physics/BoxCollider.h"
#include "../Physics/MeshCollider.h"

#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr; // 「IDXGI」「ID3D12」等のCOMオブジェクトはすべてComPtrで扱う（生ポインタで持たない）

inline std::ofstream logStream;

// 文字列変換（UTF-8 から WideString）
inline std::wstring ConvertString(const std::string& str) {
    if (str.empty()) return std::wstring();
    auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0);
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &result[0], sizeNeeded);
    return result;
}

// ログを出す。logs/latest.log と Visual Studio の「出力」ウィンドウの両方に流れる
inline void Log(const std::string& message) {
    if (logStream.is_open()) logStream << message << std::endl;
    OutputDebugStringA((message + "\n").c_str());
}

struct VertexData {
    Vector4 position;
    Vector2 texcoord;
    Vector3 normal;
};

// objが参照するmtlファイルから読み込む情報（今回はTextureのパスのみ）
struct MaterialData {
    std::string textureFilePath;
};

// objの中で o（オブジェクト）/ g（グループ）に区切られた、ひとまとまりの部品。
// verticesの[firstVertex, firstVertex + vertexCount)の範囲を指すだけで、頂点は複製しない
struct ModelPart {
    std::string name;
    size_t firstVertex = 0;
    size_t vertexCount = 0;
};

// objファイル1つ分のデータ（頂点データ＋マテリアル）
// 将来的にゲームエンジンとして使い回すことを見据え、モデル1つ＝この構造体1つで完結するようにしてある
struct ModelData {
    std::vector<VertexData> vertices; // 全部品を順に並べたもの
    // o / g ごとの区切り。1つも宣言が無いファイルでも、全体を指す部品が1つだけ入る。
    // 区切りを持たせているのは、1つのobjに複数の物体が入っていたときに
    // 別々のオブジェクトとして置けるようにするため（まとめてしまうと個別に動かせない）
    std::vector<ModelPart> parts;
    MaterialData material;
};

// マテリアル（色 + Lighting方式 + UVTransform）
struct Material {
    Vector4 color;
    int32_t lightingMode; // 0:Lightingなし 1:Lambert 2:HalfLambert（旧enableLightingを3値に拡張）
    float padding[3];     // 16バイト境界に合わせるためのパディング（HLSL側のfloat3 paddingと対応）
    Matrix4x4 uvTransform; // UVTransform（Translate/Rotate/Scaleを合成した行列）
};

// Lighting方式
enum class LightingMode : int32_t {
    kNone = 0,
    kLambert = 1,
    kHalfLambert = 2,
};

// ブレンドモード。画面にすでにある色(Dest)と、これから描く色(Src)の合成方法。
// ハードウェアの固定機能なので、モードごとにPSOを1つずつ作って切り替える
//   None     : 合成なし（不透明）
//   Normal   : 通常のαブレンド。Src * SrcA + Dest * (1 - SrcA)
//   Add      : 加算。Src * SrcA + Dest * 1
//   Subtract : 減算。Dest * 1 - Src * SrcA
//   Multiply : 乗算。Src * 0 + Dest * Src
//   Screen   : スクリーン。Src * (1 - Dest) + Dest * 1
enum class BlendMode : int32_t {
    kNone,
    kNormal,
    kAdd,
    kSubtract,
    kMultiply,
    kScreen,
    kCount, // 個数を数えるためのもの。オブジェクトに設定してはいけない
};

// 座標変換用行列（WVP + WorldMatrix。法線変換にWorldMatrixを利用する）
struct TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
};

// 平行光源
struct DirectionalLight {
    Vector4 color;     // ライトの色
    Vector3 direction; // ライトの向き（必ず正規化しておくこと）
    float intensity;   // 輝度
};

// リソースリークチェッカー
// C++は「デストラクタはコンストラクタと逆順で呼ばれる」ため、これをmainの一番最初に実体化しておけば、
// デストラクタはmainの一番最後（＝他のすべてのComPtrが解放された後）に呼ばれる。
// そのタイミングでReportLiveObjectsを行えば、解放漏れを正しく検出できる
struct D3DResourceLeakChecker {
    ~D3DResourceLeakChecker() {
        ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        }
    }
};

// バッファリソース（Upload Heap）の作成
inline ComPtr<ID3D12Resource> CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
    ComPtr<ID3D12Resource> resource;
    D3D12_HEAP_PROPERTIES uploadHeapProperties{};
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HRESULT hr = device->CreateCommittedResource(
        &uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
        &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}

// ディスクリプターヒープの作成
inline ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = heapType;
    descriptorHeapDesc.NumDescriptors = numDescriptors;
    descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
    assert(SUCCEEDED(hr));
    return descriptorHeap;
}

// DXCを使用したシェーダーのコンパイル
inline ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile, IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler) {
    ComPtr<IDxcBlobEncoding> shaderSource;
    dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    DxcBuffer shaderSourceBuffer{ shaderSource->GetBufferPointer(), shaderSource->GetBufferSize(), DXC_CP_UTF8 };
    LPCWSTR arguments[] = { filePath.c_str(), L"-E", L"main", L"-T", profile, L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr" };
    ComPtr<IDxcResult> shaderResult;
    dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler, IID_PPV_ARGS(&shaderResult));
    ComPtr<IDxcBlobUtf8> shaderError;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError && shaderError->GetStringLength() != 0) { Log(shaderError->GetStringPointer()); assert(false); }
    ComPtr<IDxcBlob> shaderBlob;
    shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    return shaderBlob;
}

// テクスチャが読めなかったときの代役を、その場で作る（ファイルに一切依存しない）。
// マゼンタ×黒の市松模様は「テクスチャが見つからなかった」ことを表す昔からの目印で、
// 真っ白や真っ黒と違って「本当にそういう見た目なのか、読み込みに失敗したのか」を取り違えない。
//
// 代役を用意しておく理由：読み込み失敗をそのまま下流へ流すと、幅も高さも0の画像で
// GPUリソースを作ろうとして別の場所で落ちる。ここで必ず「使える画像」に変えてしまえば、
// 呼び出し側は失敗を気にせず書ける
inline DirectX::ScratchImage CreateMissingTextureImage() {
    constexpr size_t kSize = 64;   // 1辺の画素数
    constexpr size_t kCheck = 8;   // 市松模様1マスの画素数

    DirectX::ScratchImage image{};
    if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kSize, kSize, 1, 1))) {
        return image; // ここまで失敗する状況では打つ手がない（空のまま返す）
    }

    const DirectX::Image* dst = image.GetImage(0, 0, 0);
    for (size_t y = 0; y < kSize; ++y) {
        uint8_t* row = dst->pixels + y * dst->rowPitch;
        for (size_t x = 0; x < kSize; ++x) {
            bool magenta = ((x / kCheck) + (y / kCheck)) % 2 == 0;
            row[x * 4 + 0] = magenta ? 255 : 0;
            row[x * 4 + 1] = 0;
            row[x * 4 + 2] = magenta ? 255 : 0;
            row[x * 4 + 3] = 255;
        }
    }
    return image;
}

// WICファイルからテクスチャを読み込み、ミップマップを生成する。
// 読み込めなかった場合は止めずに「テクスチャが無い」ことが見て分かる市松模様で代用する
// （以前はassertで止めていたが、mtlの指すpngをコピーし忘れただけでモデルが一切置けなくなっていた）
inline DirectX::ScratchImage LoadTexture(const std::string& filePath) {
    Log("[LoadTexture] path = \"" + filePath + "\""); // どのパスを読もうとしたかログに残す（失敗時の切り分け用）
    DirectX::ScratchImage image{};
    std::wstring filePathW = ConvertString(filePath);
    HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    if (FAILED(hr)) {
        Log(std::format("[LoadTexture] 読み込みに失敗しました hr=0x{:08X} path=\"{}\" → マゼンタの市松模様で代用します",
                        static_cast<uint32_t>(hr), filePath));
        return CreateMissingTextureImage(); // 代役は64x64の1枚絵なのでミップは作らない
    }

    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
    if (FAILED(hr)) {
        // ミップが作れないだけなら絵そのものは使える（遠くで少しちらつくが描画は続く）
        Log(std::format("[LoadTexture] ミップマップを作れませんでした hr=0x{:08X} path=\"{}\" → 元の画像のまま使います",
                        static_cast<uint32_t>(hr), filePath));
        return image;
    }

    return mipImages;
}

// テクスチャリソース（Default Heap）の作成
inline ComPtr<ID3D12Resource> CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = UINT(metadata.width);
    resourceDesc.Height = UINT(metadata.height);
    resourceDesc.MipLevels = UINT16(metadata.mipLevels);
    resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
    resourceDesc.Format = metadata.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Resource> resource;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&resource)
    );
    assert(SUCCEEDED(hr));

    return resource;
}

// テクスチャデータをVRAM（Default Heap）へ転送
// 戻り値のアップロード用中間リソースは、GPUでのコピー完了（フェンス待機）まで解放してはいけない
[[nodiscard]]
inline ComPtr<ID3D12Resource> UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages,
    ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel) {
        const DirectX::Image* img = mipImages.GetImage(mipLevel, 0, 0);
        D3D12_SUBRESOURCE_DATA subresource{};
        subresource.pData = img->pixels;
        subresource.RowPitch = static_cast<LONG_PTR>(img->rowPitch);
        subresource.SlicePitch = static_cast<LONG_PTR>(img->slicePitch);
        subresources.push_back(subresource);
    }

    uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));
    ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(device, intermediateSize);
    UpdateSubresources(commandList, texture, intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);

    return intermediateResource;
}

// mtlファイル（マテリアルテンプレートライブラリ）を読み込む。今回必要なのはTextureのパスだけなので"map_Kd"のみ対応する
inline MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
    MaterialData materialData; // 構築するMaterialData
    std::string line;          // ファイルから読んだ1行を格納するもの

    std::string mtlPath = directoryPath + "/" + filename;
    Log("[LoadMaterialTemplateFile] path = \"" + mtlPath + "\"");
    std::ifstream file(mtlPath); // ファイルを開く
    if (!file.is_open()) {
        // mtlが無くても形そのものは描ける（テクスチャの指定が無いだけ）。
        // objだけコピーしてmtlを持ってこなかった・mtlの名前を変えた、といったよくある状況で
        // モデルが一切置けなくなるより、既定のテクスチャで置けた方が作業が進む
        Log("[LoadMaterialTemplateFile] 見つかりません: \"" + mtlPath + "\" → テクスチャ指定なしとして続行します");
        return materialData;
    }

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier; // 先頭の識別子を読む

        if (identifier == "map_Kd") {
            std::string textureFilename;
            s >> textureFilename;
            // map_Kdにはエクスポート元PCの絶対パスが書かれていることがある（例:"C:/Users/.../uvChecker.png"）。
            // 環境が変わると壊れるパスなので、ファイル名部分だけを取り出し、必ずmtlと同じディレクトリから読むようにする
            std::string textureBaseName = std::filesystem::path(textureFilename).filename().string();
            materialData.textureFilePath = directoryPath + "/" + textureBaseName;
            Log("[LoadMaterialTemplateFile] map_Kd -> \"" + materialData.textureFilePath + "\"");
        }
    }

    return materialData;
}

// objファイルの要素番号を、配列の添字へ変換する。
// objの番号は1始まりで、さらに「-1＝最後の要素」という後ろからの数え方も許されている。
// 未指定(0)や範囲外は-1を返し、呼び出し側が「その要素は無い」と判断できるようにする。
//
// この変換を挟まずに素直に「番号-1」で引くと、法線を書き出していないobj（f 1/1 1/1 1/1）で
// 添字-1、つまり配列の外を読んでそのまま落ちる
inline int ResolveObjIndex(int objIndex, size_t elementCount) {
    if (objIndex > 0) {
        int index = objIndex - 1;
        return (index < (int)elementCount) ? index : -1;
    }
    if (objIndex < 0) {
        int index = (int)elementCount + objIndex; // -1が最後の要素、-2がその1つ前…
        return (index >= 0) ? index : -1;
    }
    return -1; // 0は「指定なし」（f 1//1 のようにUVを省いた書き方）
}

// 3頂点から面の法線を求める（法線が書かれていないobjのための代役）。
// 引数は必ず「実際に描画へ登録する順序」で渡すこと。順序が入れ替わると向きが裏返り、
// 陰影が内と外で逆になってしまう
inline Vector3 ComputeTriangleNormal(const Vector4& a, const Vector4& b, const Vector4& c) {
    Vector3 ab{ b.x - a.x, b.y - a.y, b.z - a.z };
    Vector3 ac{ c.x - a.x, c.y - a.y, c.z - a.z };
    Vector3 n{
        ab.y * ac.z - ab.z * ac.y,
        ab.z * ac.x - ab.x * ac.z,
        ab.x * ac.y - ab.y * ac.x,
    };
    float length = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (length < 1e-8f) { return { 0.0f, 1.0f, 0.0f }; } // 潰れた三角形。とりあえず上向きにしておく
    return { n.x / length, n.y / length, n.z / length };
}

// 多角形の面を三角形へ分割する（耳切り法）。結果は polygon への添字の3つ組で返す。
//
// 単純に「0番目の頂点から扇状に切る」方法だと、凹んだ多角形で図形の外側にはみ出す三角形ができる
// （L字の面を切ると、Lのくぼみを横切る三角形が生まれて、あるはずのない面が描かれる）。
// 耳切り法は「隣り合う3頂点が作る三角形の中に、他の頂点が入っていない」ところだけを切り落とすので、
// 凹んだ形でも図形の内側だけが三角形で埋まる。
//
// 面は平面とは限らないので、まずニューウェル法で面全体の法線を求め、
// それに垂直な2軸へ落として2次元問題として解く
inline void TriangulatePolygon(const std::vector<VertexData>& polygon, std::vector<uint32_t>& outIndices) {
    outIndices.clear();
    const size_t count = polygon.size();
    if (count < 3) { return; }
    if (count == 3) { outIndices = { 0, 1, 2 }; return; }

    // 扇状に切る（凸多角形なら正しい）。耳が見つからなくなったときの逃げ道としても使う
    auto AppendFan = [&outIndices](const std::vector<uint32_t>& ring) {
        for (size_t i = 1; i + 1 < ring.size(); ++i) {
            outIndices.push_back(ring[0]);
            outIndices.push_back(ring[i]);
            outIndices.push_back(ring[i + 1]);
        }
    };

    // ----- 面の法線（ニューウェル法）-----
    // 3頂点の外積と違い、多少ゆがんだ面でも安定して「面全体の向き」が求まる
    Vector3 normal{ 0.0f, 0.0f, 0.0f };
    for (size_t i = 0; i < count; ++i) {
        const Vector4& a = polygon[i].position;
        const Vector4& b = polygon[(i + 1) % count].position;
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    float normalLength = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    std::vector<uint32_t> ring(count);
    for (uint32_t i = 0; i < (uint32_t)count; ++i) { ring[i] = i; }
    if (normalLength < 1e-12f) { AppendFan(ring); return; } // 頂点が一直線上：面積が無いので分けようがない
    normal = { normal.x / normalLength, normal.y / normalLength, normal.z / normalLength };

    // ----- 面の上の2軸を作り、2次元へ落とす -----
    // u×v が normal と一致するように選ぶ。こうすると、落とした多角形は必ず反時計回りになり、
    // 「内側か外側か」の判定を符号だけで書ける
    Vector3 helper = (fabsf(normal.z) < 0.9f) ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
    Vector3 u{
        helper.y * normal.z - helper.z * normal.y,
        helper.z * normal.x - helper.x * normal.z,
        helper.x * normal.y - helper.y * normal.x,
    };
    float uLength = sqrtf(u.x * u.x + u.y * u.y + u.z * u.z);
    if (uLength < 1e-12f) { AppendFan(ring); return; }
    u = { u.x / uLength, u.y / uLength, u.z / uLength };
    Vector3 v{
        normal.y * u.z - normal.z * u.y,
        normal.z * u.x - normal.x * u.z,
        normal.x * u.y - normal.y * u.x,
    };

    std::vector<Vector2> flat(count);
    for (size_t i = 0; i < count; ++i) {
        const Vector4& p = polygon[i].position;
        flat[i] = { p.x * u.x + p.y * u.y + p.z * u.z, p.x * v.x + p.y * v.y + p.z * v.z };
    }

    // 2次元での外積（正なら左に曲がる＝反時計回りの内角）
    auto Cross2D = [](const Vector2& o, const Vector2& a, const Vector2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };

    // ----- 耳を1つずつ切り落とす -----
    size_t attemptsSinceLastClip = 0;
    size_t at = 0;
    while (ring.size() > 3) {
        // 一周まわっても切れる耳が見つからないなら、これ以上は分けられない（自分自身と交差している等）。
        // 残りは扇状に切って、頂点を落とさずに済ませる
        if (attemptsSinceLastClip > ring.size()) { AppendFan(ring); return; }

        size_t prev = (at + ring.size() - 1) % ring.size();
        size_t next = (at + 1) % ring.size();
        const Vector2& a = flat[ring[prev]];
        const Vector2& b = flat[ring[at]];
        const Vector2& c = flat[ring[next]];

        // 内側に折れている頂点（凹み）は耳にならない
        if (Cross2D(a, b, c) <= 0.0f) {
            at = next;
            attemptsSinceLastClip++;
            continue;
        }

        // 三角形abcの中に、他の頂点が入っていないこと。入っていたら切ると相手を分断してしまう
        bool containsOther = false;
        for (size_t i = 0; i < ring.size() && !containsOther; ++i) {
            if (i == prev || i == at || i == next) { continue; }
            const Vector2& p = flat[ring[i]];
            bool inside = Cross2D(a, b, p) >= 0.0f && Cross2D(b, c, p) >= 0.0f && Cross2D(c, a, p) >= 0.0f;
            if (inside) { containsOther = true; }
        }
        if (containsOther) {
            at = next;
            attemptsSinceLastClip++;
            continue;
        }

        outIndices.push_back(ring[prev]);
        outIndices.push_back(ring[at]);
        outIndices.push_back(ring[next]);
        ring.erase(ring.begin() + at);
        if (at >= ring.size()) { at = 0; }
        attemptsSinceLastClip = 0;
    }
    AppendFan(ring); // 残った3頂点
}

// objファイルを読み込み、ModelData（頂点データ＋マテリアル）を構築する
// DirectXは左手系・Textureは左上原点だが、objファイルは右手系・Textureは左下原点で作られているため、
// 読み込み時に変換（Xの反転・回り順の反転・V座標の反転）を行っておく
inline ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
    ModelData modelData;             // 構築するModelData
    std::vector<Vector4> positions;  // 位置
    std::vector<Vector3> normals;    // 法線
    std::vector<Vector2> texcoords;  // テクスチャ座標
    std::string line;                // ファイルから読んだ1行を格納するもの
    int brokenFaceCount = 0;         // 頂点の位置が引けず、読み飛ばした面の数（最後にまとめてログへ出す）
    std::vector<uint32_t> faceIndices; // 1つの面を三角形へ分割した結果（面ごとに使い回す）
    // o / g が現れた位置。「どこから新しい部品が始まるか」だけを控えておき、
    // 区切りの確定は最後にまとめて行う（次の区切りが来るまで、その部品の終わりは決まらないため）
    struct PartMarker { std::string name; size_t firstVertex; };
    std::vector<PartMarker> partMarkers;

    std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
    if (!file.is_open()) {
        // ファイルが無い/開けない場合はここで空のModelDataを返す（呼び出し側でデバッグ用の立方体に差し替える）。
        // 以前はassertで止めていたが、Releaseビルドではassertが消えて素通りし、
        // 直後にpositions/normalsへ範囲外アクセスしてクラッシュしていたため、必ずここで打ち切るようにした
        Log("[LoadObjFile] Failed to open: " + directoryPath + "/" + filename);
        return modelData;
    }

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier; // 先頭の識別子を読む

        if (identifier == "v") {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.x *= -1.0f; // 右手系->左手系変換のためXを反転する
            position.w = 1.0f;
            positions.push_back(position);
        }
        else if (identifier == "vt") {
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            texcoord.y = 1.0f - texcoord.y; // 左下原点->左上原点のTexture座標系に変換する
            texcoords.push_back(texcoord);
        }
        else if (identifier == "vn") {
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            normal.x *= -1.0f; // 右手系->左手系変換のためXを反転する
            normals.push_back(normal);
        }
        else if (identifier == "f") {
            // 面は三角形・四角形（およびそれ以上のn角形）に対応する。ファン分割で三角形に変換する
            // （Blender側で"Triangulate Faces"を有効にして出力していなくても正しく描画できるようにするため）
            std::vector<VertexData> faceVertices;
            std::string vertexDefinition;
            bool faceHasNormals = true; // 1頂点でも法線が欠けていたら、この面の法線は後で計算する
            bool faceIsBroken = false;  // 位置が引けない頂点があった面は、丸ごと捨てる
            while (s >> vertexDefinition) {
                // 頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
                std::istringstream v(vertexDefinition);
                int elementIndices[3] = { 0, 0, 0 };
                for (int32_t element = 0; element < 3; ++element) {
                    std::string index;
                    std::getline(v, index, '/'); // /区切りでインデックスを読んでいく
                    // 数字以外が書かれていても止まらないようにfrom_charsを使う（stoiと違って例外を投げない）。
                    // 変換できなければelementIndicesは0のまま＝「指定なし」として扱われる
                    if (!index.empty()) {
                        std::from_chars(index.data(), index.data() + index.size(), elementIndices[element]);
                    }
                }

                // 1始まり・後ろからの数え方（負の番号）・範囲外を、ここですべて吸収する。
                // 特に法線は、書き出しの設定しだいで丸ごと省かれることがある（f 1/1 1/1 1/1）。
                // 以前はその場合に配列の外を読んでクラッシュしていた
                int positionIndex = ResolveObjIndex(elementIndices[0], positions.size());
                int texcoordIndex = ResolveObjIndex(elementIndices[1], texcoords.size());
                int normalIndex   = ResolveObjIndex(elementIndices[2], normals.size());
                if (positionIndex < 0) { faceIsBroken = true; break; } // 位置だけは代わりを用意しようがない

                Vector4 position = positions[positionIndex];
                Vector2 texcoord = (texcoordIndex >= 0) ? texcoords[texcoordIndex] : Vector2{ 0.0f, 0.0f };
                Vector3 normal{ 0.0f, 0.0f, 0.0f };
                if (normalIndex >= 0) { normal = normals[normalIndex]; } else { faceHasNormals = false; }
                faceVertices.push_back({ position, texcoord, normal });
            }
            if (faceIsBroken) { brokenFaceCount++; continue; }

            // 三角形へ分割する。凹んだ多角形でも図形の外側にはみ出さないよう耳切り法を使う
            TriangulatePolygon(faceVertices, faceIndices);
            // 右手系->左手系変換で回り順が逆になっているため、各三角形は逆順で登録して回り順を戻す
            for (size_t i = 0; i + 2 < faceIndices.size(); i += 3) {
                VertexData triangle[3] = {
                    faceVertices[faceIndices[i + 2]],
                    faceVertices[faceIndices[i + 1]],
                    faceVertices[faceIndices[i]],
                };
                // 法線が書かれていないobjは、描く三角形そのものから法線を作る。
                // 無いまま進めると法線が全部ゼロになり、陰影計算の結果どこも真っ黒になってしまう。
                // 登録するのと同じ並びで計算しているので、左手系へ直した後の向きと必ず一致する
                if (!faceHasNormals) {
                    Vector3 faceNormal = ComputeTriangleNormal(triangle[0].position, triangle[1].position, triangle[2].position);
                    triangle[0].normal = triangle[1].normal = triangle[2].normal = faceNormal;
                }
                modelData.vertices.push_back(triangle[0]);
                modelData.vertices.push_back(triangle[1]);
                modelData.vertices.push_back(triangle[2]);
            }
        }
        else if (identifier == "o" || identifier == "g") {
            // ここから別の部品が始まる。名前には空白が入ることがあるので行末まで読む
            std::string partName;
            std::getline(s, partName);
            size_t nameBegin = partName.find_first_not_of(" \t\r");
            size_t nameEnd = partName.find_last_not_of(" \t\r");
            partName = (nameBegin == std::string::npos) ? "" : partName.substr(nameBegin, nameEnd - nameBegin + 1);
            // 部品の名前はキャッシュの見出し（"フォルダ/ファイル#部品名"）に使うので、区切り文字は潰しておく
            for (char& c : partName) { if (c == '#' || c == '/' || c == '\\') { c = '_'; } }

            // 面を1枚も挟まずにo/gが続いた場合は、新しい部品ではなく名前の付け替えとみなす。
            // 書き出しの設定によってはoの直後にgが並ぶことがあり、そのまま数えると空の部品ができてしまう
            if (!partMarkers.empty() && partMarkers.back().firstVertex == modelData.vertices.size()) {
                partMarkers.back().name = partName;
            } else {
                partMarkers.push_back({ partName, modelData.vertices.size() });
            }
        }
        else if (identifier == "mtllib") {
            // materialTemplateLibraryファイルの名前を取得する
            std::string materialFilename;
            s >> materialFilename;
            // 基本的にobjファイルと同一階層にmtlは存在させるので、ディレクトリ名とファイル名を渡す
            modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
        }
    }

    // ----- 部品の区切りを確定する -----
    // o/gが1つも無いファイル、あるいは最初のo/gより前に面があるファイルのために、
    // 先頭から始まる区切りを必ず1つ用意する（そうしないとその範囲の面がどの部品にも入らない）
    if (partMarkers.empty() || partMarkers.front().firstVertex > 0) {
        partMarkers.insert(partMarkers.begin(), { "", 0 });
    }
    for (size_t i = 0; i < partMarkers.size(); ++i) {
        size_t first = partMarkers[i].firstVertex;
        size_t last = (i + 1 < partMarkers.size()) ? partMarkers[i + 1].firstVertex : modelData.vertices.size();
        if (last <= first) { continue; } // 面を1枚も持たない区切りは部品にしない

        std::string name = partMarkers[i].name;
        if (name.empty()) { name = "part" + std::to_string(modelData.parts.size() + 1); }
        // 同じ名前が複数あるとキャッシュの見出しがぶつかるので、後から来た方に番号を足して分ける
        std::string uniqueName = name;
        for (int suffix = 2; ; ++suffix) {
            bool taken = false;
            for (const ModelPart& existing : modelData.parts) {
                if (existing.name == uniqueName) { taken = true; break; }
            }
            if (!taken) { break; }
            uniqueName = name + "_" + std::to_string(suffix);
        }
        modelData.parts.push_back({ uniqueName, first, last - first });
    }

    if (brokenFaceCount > 0) {
        Log(std::format("[LoadObjFile] \"{}/{}\" の面{}枚は頂点の位置が引けなかったため読み飛ばしました",
                        directoryPath, filename, brokenFaceCount));
    }
    return modelData;
}

// テクスチャを読み込み、指定したディスクリプタヒープのスロット（srvIndex）にSRVを作成する
// アップロード用の中間リソースはこの関数内では解放しない（GPUでのコピー完了前に解放すると壊れるため）。
// 呼び出し側はintermediateResourcesに積まれたリソースを、コマンド実行＆フェンス待機の後に解放すること
// すでに手元にある画像からSRVを作る。ファイルから読む場合との違いは、読み込みの1行だけ。
// 「その場で作った画像」もテクスチャとして使えるように、ここで口を分けてある
inline D3D12_GPU_DESCRIPTOR_HANDLE CreateTextureSRV(
    ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
    ID3D12DescriptorHeap* srvDescriptorHeap, UINT descriptorSizeSRV, UINT srvIndex,
    const DirectX::ScratchImage& mipImages, ComPtr<ID3D12Resource>& outTextureResource,
    std::vector<ComPtr<ID3D12Resource>>& intermediateResources)
{
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device, metadata);
    ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(textureResource.Get(), mipImages, device, commandList);
    intermediateResources.push_back(intermediateResource);

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    srvHandleCPU.ptr += (size_t)descriptorSizeSRV * srvIndex;
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    srvHandleGPU.ptr += (UINT64)descriptorSizeSRV * srvIndex;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, srvHandleCPU);

    outTextureResource = textureResource;
    return srvHandleGPU;
}

// ファイルから読み込んでSRVを作る
inline D3D12_GPU_DESCRIPTOR_HANDLE LoadTextureToSRV(
    ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
    ID3D12DescriptorHeap* srvDescriptorHeap, UINT descriptorSizeSRV, UINT srvIndex,
    const std::string& filePath, ComPtr<ID3D12Resource>& outTextureResource,
    std::vector<ComPtr<ID3D12Resource>>& intermediateResources)
{
    return CreateTextureSRV(device, commandList, srvDescriptorHeap, descriptorSizeSRV, srvIndex,
                            LoadTexture(filePath), outTextureResource, intermediateResources);
}

// 深度ステンシル用テクスチャリソースの作成
inline ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height) {
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    ComPtr<ID3D12Resource> resource;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClearValue,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));

    return resource;
}

// 頂点バッファ＋インデックスバッファをまとめて扱うための構造体
// （VertexResource/IndexResourceそれぞれのView、および描画に使うIndex数を保持する）
struct MeshBuffers {
    ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    ComPtr<ID3D12Resource> indexResource;
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT indexCount = 0;
    AABB localAABB; // このメッシュのローカル空間でのボックスコライダー（頂点全体を包む直方体）

    // メッシュコライダー用：ローカル頂点位置と三角形インデックス（CPU側にも保持しておく）
    std::vector<Vector3> localPositions;
    std::vector<uint32_t> triangleIndices;
};

// 頂点データ配列とインデックス配列から、VertexResource/IndexResourceおよびそれぞれのViewを作成する
inline MeshBuffers CreateMeshBuffers(ID3D12Device* device, const std::vector<VertexData>& vertices, const std::vector<uint32_t>& indices) {
    MeshBuffers mesh{};

    // ----- VertexResource -----
    size_t vertexSizeInBytes = sizeof(VertexData) * vertices.size();
    mesh.vertexResource = CreateBufferResource(device, vertexSizeInBytes);
    VertexData* vertexData = nullptr;
    mesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::copy(vertices.begin(), vertices.end(), vertexData);
    mesh.vertexResource->Unmap(0, nullptr);

    mesh.vbv.BufferLocation = mesh.vertexResource->GetGPUVirtualAddress();
    mesh.vbv.SizeInBytes = (UINT)vertexSizeInBytes;
    mesh.vbv.StrideInBytes = sizeof(VertexData);

    // ボックスコライダー用に、頂点位置だけを抜き出してローカル空間のAABBを求めておく
    std::vector<Vector3> positions;
    positions.reserve(vertices.size());
    for (const auto& v : vertices) { positions.push_back({ v.position.x, v.position.y, v.position.z }); }
    mesh.localAABB = ComputeLocalAABB(positions);
    // メッシュコライダー（球vs静止メッシュ）用にCPU側にも保持しておく
    mesh.localPositions = positions;
    mesh.triangleIndices = indices;

    // ----- IndexResource -----
    size_t indexSizeInBytes = sizeof(uint32_t) * indices.size();
    mesh.indexResource = CreateBufferResource(device, indexSizeInBytes);
    uint32_t* indexData = nullptr;
    mesh.indexResource->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    std::copy(indices.begin(), indices.end(), indexData);
    mesh.indexResource->Unmap(0, nullptr);

    mesh.ibv.BufferLocation = mesh.indexResource->GetGPUVirtualAddress();
    mesh.ibv.SizeInBytes = (UINT)indexSizeInBytes;
    mesh.ibv.Format = DXGI_FORMAT_R32_UINT;

    mesh.indexCount = (UINT)indices.size();

    return mesh;
}

// ModelData（LoadObjFileの結果）からVertexResource/IndexResourceを作成する
// LoadObjFileは頂点を共有せずポリゴンごとに並べているだけなので、Indexは単純に0,1,2...と振ればよい
inline MeshBuffers CreateMeshBuffersFromModelData(ID3D12Device* device, const ModelData& modelData) {
    std::vector<uint32_t> indices(modelData.vertices.size());
    for (uint32_t i = 0; i < indices.size(); ++i) { indices[i] = i; }
    return CreateMeshBuffers(device, modelData.vertices, indices);
}

// ModelDataのうち、1つの部品（o/gで区切られたひとまとまり）だけからバッファを作る。
// 部品ごとに別のObject3Dとして置くために使う
inline MeshBuffers CreateMeshBuffersFromModelPart(ID3D12Device* device, const ModelData& modelData, const ModelPart& part) {
    size_t first = (std::min)(part.firstVertex, modelData.vertices.size());
    size_t last = (std::min)(first + part.vertexCount, modelData.vertices.size());
    std::vector<VertexData> vertices(modelData.vertices.begin() + first, modelData.vertices.begin() + last);
    std::vector<uint32_t> indices(vertices.size());
    for (uint32_t i = 0; i < indices.size(); ++i) { indices[i] = i; }
    return CreateMeshBuffers(device, vertices, indices);
}

// 球体頂点データの生成（Indexを利用する形に変更。頂点は格子点ごとに1つだけ持ち、共有する）
inline MeshBuffers CreateSphereMeshBuffers(ID3D12Device* device, uint32_t subdivisionLat = 32, uint32_t subdivisionLon = 64) {

    float latStep = kPi / subdivisionLat;
    float lonStep = 2.0f * kPi / subdivisionLon;

    // 頂点は(緯度方向subdivisionLat+1)×(経度方向subdivisionLon+1)の格子点として1つずつだけ生成する
    // （経度・緯度の端は縫い目のためUVが異なる別頂点として重複させる必要がある）
    uint32_t rowCount = subdivisionLat + 1;
    uint32_t colCount = subdivisionLon + 1;
    std::vector<VertexData> vertices;
    vertices.reserve(rowCount * colCount);

    for (uint32_t lat = 0; lat < rowCount; lat++) {
        float theta = kPi * 0.5f - lat * latStep; // 緯度（+PI/2〜-PI/2）
        float cosLat = cosf(theta);
        float sinLat = sinf(theta);
        for (uint32_t lon = 0; lon < colCount; lon++) {
            float phi = lon * lonStep; // 経度（0〜2PI）
            float cosLon = cosf(phi);
            float sinLon = sinf(phi);

            Vector4 pos = { cosLat * cosLon, sinLat, cosLat * sinLon, 1.0f };
            Vector2 uv = { (float)lon / subdivisionLon, (float)lat / subdivisionLat };
            // 単位球なので、位置をそのまま法線として利用できる
            Vector3 normal = { pos.x, pos.y, pos.z };

            vertices.push_back({ pos, uv, normal });
        }
    }

    // インデックスデータの生成（1枚の四角形＝三角形2枚＝6インデックス）
    std::vector<uint32_t> indices;
    indices.reserve(subdivisionLat * subdivisionLon * 6);
    for (uint32_t lat = 0; lat < subdivisionLat; lat++) {
        for (uint32_t lon = 0; lon < subdivisionLon; lon++) {
            uint32_t i0 = lat * colCount + lon;             // 左上
            uint32_t i1 = lat * colCount + (lon + 1);       // 右上
            uint32_t i2 = (lat + 1) * colCount + lon;       // 左下
            uint32_t i3 = (lat + 1) * colCount + (lon + 1); // 右下

            // 三角形1枚目
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            // 三角形2枚目
            indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
        }
    }

    return CreateMeshBuffers(device, vertices, indices);
}

// スプライト（板ポリゴン）頂点データの生成。Indexを利用し、頂点データは4つだけ持つ
inline MeshBuffers CreateSpriteMeshBuffers(ID3D12Device* device, float width = 400.0f, float height = 400.0f) {
    // Spriteは-zの方向を向きとしておく（Lightingしないので実際には使わないが）
    Vector3 spriteNormal = { 0.0f, 0.0f, -1.0f };

    std::vector<VertexData> vertices(4);
    vertices[0].position = { 0.0f,   height, 0.0f, 1.0f }; vertices[0].texcoord = { 0.0f, 1.0f }; vertices[0].normal = spriteNormal; // 0:左下
    vertices[1].position = { 0.0f,   0.0f,   0.0f, 1.0f }; vertices[1].texcoord = { 0.0f, 0.0f }; vertices[1].normal = spriteNormal; // 1:左上
    vertices[2].position = { width,  height, 0.0f, 1.0f }; vertices[2].texcoord = { 1.0f, 1.0f }; vertices[2].normal = spriteNormal; // 2:右下
    vertices[3].position = { width,  0.0f,   0.0f, 1.0f }; vertices[3].texcoord = { 1.0f, 0.0f }; vertices[3].normal = spriteNormal; // 3:右上

    // 「0,1,2」で1つ目の三角形、「1,3,2」で2つ目の三角形を構成する
    std::vector<uint32_t> indices = { 0, 1, 2, 1, 3, 2 };

    return CreateMeshBuffers(device, vertices, indices);
}

// 立方体（Cube）頂点データの生成。面ごとに法線・UVが必要なので、1面につき4頂点（計24頂点）持つ
inline MeshBuffers CreateCubeMeshBuffers(ID3D12Device* device, float size = 1.0f) {
    float h = size * 0.5f;

    // 面ごとに「左上・右上・左下・右下」の順で4頂点＋法線を定義する（Sprite/Sphereと同じ並び方）
    struct Face { Vector3 normal; Vector3 tl, tr, bl, br; };
    Face faces[6] = {
        { {  1, 0,  0 }, {  h,  h, -h }, {  h,  h,  h }, {  h, -h, -h }, {  h, -h,  h } }, // +X
        { { -1, 0,  0 }, { -h,  h,  h }, { -h,  h, -h }, { -h, -h,  h }, { -h, -h, -h } }, // -X
        { {  0, 1,  0 }, { -h,  h, -h }, {  h,  h, -h }, { -h,  h,  h }, {  h,  h,  h } }, // +Y
        { {  0, -1, 0 }, { -h, -h,  h }, {  h, -h,  h }, { -h, -h, -h }, {  h, -h, -h } }, // -Y
        { {  0, 0,  1 }, { -h,  h,  h }, {  h,  h,  h }, { -h, -h,  h }, {  h, -h,  h } }, // +Z
        { {  0, 0, -1 }, {  h,  h, -h }, { -h,  h, -h }, {  h, -h, -h }, { -h, -h, -h } }, // -Z
    };

    std::vector<VertexData> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(24);
    indices.reserve(36);

    for (const auto& face : faces) {
        uint32_t base = (uint32_t)vertices.size();
        vertices.push_back({ { face.tl.x, face.tl.y, face.tl.z, 1.0f }, { 0.0f, 0.0f }, face.normal });
        vertices.push_back({ { face.tr.x, face.tr.y, face.tr.z, 1.0f }, { 1.0f, 0.0f }, face.normal });
        vertices.push_back({ { face.bl.x, face.bl.y, face.bl.z, 1.0f }, { 0.0f, 1.0f }, face.normal });
        vertices.push_back({ { face.br.x, face.br.y, face.br.z, 1.0f }, { 1.0f, 1.0f }, face.normal });
        // 「0,1,2」で1つ目の三角形、「1,3,2」で2つ目の三角形（Sprite/Sphereと同じ構成）
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 1); indices.push_back(base + 3); indices.push_back(base + 2);
    }

    return CreateMeshBuffers(device, vertices, indices);
}

// メッシュコライダーの可視化用：そのオブジェクトの三角形の辺をすべて集めたワイヤーフレーム（LineList）
// BoxColliderの緑/赤ワイヤーフレームと同じ仕組みで、静止メッシュコライダーの実際の形状に沿って描画する
// ローカル空間の頂点位置をそのまま持つので、毎フレームはワールド行列（WVP）だけ差し替えれば良く、
// メッシュ自体（頂点・インデックス）は一度作ったら使い回せる
struct MeshLineData {
    ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    ComPtr<ID3D12Resource> indexResource;
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT indexCount = 0;
    bool built = false; // 一度でも構築済みかどうか（未構築なら遅延構築する）
};

// メッシュコライダー用のローカル頂点位置＋三角形インデックスから、三角形の全辺（3辺×三角形数）のLineListを作る
// 頂点を共有していないメッシュ（objファイル由来など）でも問題なく動くよう、辺の重複除去は行わない
// （重複した辺は同じ色で重ねて描かれるだけなので、見た目には影響しない）
inline MeshLineData CreateMeshWireframeLineData(ID3D12Device* device, const std::vector<Vector3>& localPositions, const std::vector<uint32_t>& triangleIndices) {
    MeshLineData data;
    if (localPositions.empty() || triangleIndices.size() < 3) { return data; } // メッシュ情報が無ければ何もしない

    std::vector<uint32_t> edgeIndices;
    edgeIndices.reserve(triangleIndices.size() * 2);
    for (size_t i = 0; i + 2 < triangleIndices.size(); i += 3) {
        uint32_t a = triangleIndices[i + 0];
        uint32_t b = triangleIndices[i + 1];
        uint32_t c = triangleIndices[i + 2];
        edgeIndices.push_back(a); edgeIndices.push_back(b);
        edgeIndices.push_back(b); edgeIndices.push_back(c);
        edgeIndices.push_back(c); edgeIndices.push_back(a);
    }

    size_t vertexSizeInBytes = sizeof(Vector3) * localPositions.size();
    data.vertexResource = CreateBufferResource(device, vertexSizeInBytes);
    Vector3* vertexData = nullptr;
    data.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::copy(localPositions.begin(), localPositions.end(), vertexData);
    data.vertexResource->Unmap(0, nullptr);
    data.vbv.BufferLocation = data.vertexResource->GetGPUVirtualAddress();
    data.vbv.SizeInBytes = (UINT)vertexSizeInBytes;
    data.vbv.StrideInBytes = sizeof(Vector3);

    size_t indexSizeInBytes = sizeof(uint32_t) * edgeIndices.size();
    data.indexResource = CreateBufferResource(device, indexSizeInBytes);
    uint32_t* indexData = nullptr;
    data.indexResource->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    std::copy(edgeIndices.begin(), edgeIndices.end(), indexData);
    data.indexResource->Unmap(0, nullptr);
    data.ibv.BufferLocation = data.indexResource->GetGPUVirtualAddress();
    data.ibv.SizeInBytes = (UINT)indexSizeInBytes;
    data.ibv.Format = DXGI_FORMAT_R32_UINT;

    data.indexCount = (UINT)edgeIndices.size();
    data.built = true;
    return data;
}
