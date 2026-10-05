// cspell:ignore dxgi dxguid dxcapi IDxc IDXGI DXGI hlsl wvp HLSL GGO glyphs Meiryo Gothic
#pragma once
#include "Renderer.h"

// ============================================================================
// TextRenderer：画面に文字を出す機能
//
// ■ なぜ必要か
//   画面に文字を出す手段がないと、
//   完成品として配ったときに画面に文字が1つも出ない。
//   タイトルも、残り時間も、「クリア」の一言すら出せない。ゲームとして成立しないので、
//   外部ライブラリに頼らない自前の文字描画を用意する。
//
// ■ どうやって出しているか
//   1. Windowsが持っているフォントに「この文字の形を教えて」と頼む（GetGlyphOutlineW）。
//      返ってくるのは、文字の濃さ（0〜64）が並んだ小さな灰色画像
//   2. その画像を1枚の大きなテクスチャ（グリフアトラス）の空いている場所へ貼り付け、
//      「その文字はアトラスのどこにあるか」を控える
//   3. 描くときは、控えた位置から切り出した四角形を画面に並べる
//
//   一度使った文字はアトラスに残るので、2回目以降はただの四角形描画になる。
//   逆に、使っていない文字のためにアトラスを用意することもない。
//   日本語は文字種が多いため「あらかじめ全部作っておく」方式では現実的でなく、
//   実際に出した文字だけを後から足していくこの方式にしてある
//
// ■ 大きさについて
//   形の取り出しは決まった大きさ（kGlyphPixelHeight）で1回だけ行い、表示のときに拡大縮小する。
//   大きさごとに作り直すとアトラスがすぐ埋まるうえ、同じ文字を何度も切り出すことになるため
// ============================================================================

// 文字1つぶんの頂点。位置は画面のピクセル座標そのまま（左上が原点）
struct TextVertex {
    Vector2 position;
    Vector2 texcoord;
    Vector4 color;
};

// 書体の種類。
//
// 1種類しか無いと、大きさを変えるだけで作り分けることになり、どの画面も同じ顔になる。
// 見出しは太く、説明は細く、数字は数字らしく——それぞれ別の書体を当てると、
// 文字を読む前に「これは見出し」「これは数字」と分かるようになる。
//
// Numeric/Labelは欧文の書体なので、日本語を渡しても字が無い。
// 無い字はBodyで代わりに切り出すようにしてあるので、混ざっても崩れはしないが、
// そこだけ書体が変わるので、日本語を出す所ではDisplay/Bodyを使うこと
enum class TextFace {
    Display, // 見出し。太くて大きい（題・大きな見出し・番号）
    Body,    // 本文。読ませる文と、日本語を含むものはこれ
    Numeric, // 数字。斜めに寝た太字で、数を読ませたい所だけに使う
    Label,   // 小さい注記。等幅で、キーの案内や補足に使う
};
constexpr int kTextFaceCount = 4;

// 1文字ぶんの、アトラス上での場所と字送りの情報
struct GlyphInfo {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f; // アトラス上の範囲（0〜1）
    float width = 0.0f, height = 0.0f;                // 図形の大きさ[px]
    float bearingX = 0.0f, bearingY = 0.0f;           // 文字の基準点から図形の左上までのずれ[px]
    float advance = 0.0f;                             // 次の文字までどれだけ進むか[px]
    bool valid = false;                               // 図形を持つか（空白はfalse＝進むだけで何も描かない）
};

class TextRenderer {
public:
    // グリフを切り出す大きさ[px]。大きいほど拡大したときに綺麗だが、アトラスの消費も増える
    static constexpr int kGlyphPixelHeight = 64;
    // グリフアトラスの一辺[px]。R8（1ピクセル1バイト）なので2048でも4MBしか使わない
    static constexpr int kAtlasSize = 2048;
    // 1フレームに描ける文字数の上限。1文字＝四角形1つ＝頂点6つ
    static constexpr int kMaxGlyphsPerFrame = 4096;
    // 隣の文字の端が滲んで混ざらないよう、貼り付ける図形どうしの間に空ける余白[px]
    static constexpr int kGlyphPadding = 2;

    void Initialize(GraphicsContext& gfx, Object3DPipeline& sharedCompiler) {
        device_ = gfx.device.Get();
        // グリフをアトラスへ送る一時バッファは、GPUの転送が終わるまで解放できない。
        // 既存の「フレーム終わり（GPU待機のあと）にまとめて捨てる」仕組みへ相乗りさせる。
        // 他の初期化より先に繋いでおくこと（預け先が無いまま1文字でも作ると、
        // 転送前にバッファが消えて絵が壊れる）
        pendingUploads_ = &gfx.pendingIntermediateResources;
        CreateFontResources();
        CreateAtlasTexture(gfx);
        CreatePipeline(gfx.device.Get(), sharedCompiler);
        CreateVertexBuffer(gfx.device.Get());
    }

    void Finalize() {
        if (hdc_) {
            if (previousFont_) { SelectObject(hdc_, previousFont_); previousFont_ = nullptr; }
            DeleteDC(hdc_);
            hdc_ = nullptr;
        }
        for (FontFace& face : faces_) {
            if (face.font) { DeleteObject(face.font); face.font = nullptr; }
        }
    }

    ~TextRenderer() { Finalize(); }

    // ===== 描画の予約 =====
    // その場では描かず、頂点を溜めておく。1フレーム分をFlush()でまとめて1回の描画命令にする
    // （文字ごとに描画命令を出すと、たった数十文字でも描画装置とのやり取りが増えて重くなる）
    //
    // x, y は文字列の左上の画面座標[px]。sizeは文字の高さ[px]
    void DrawText(ID3D12GraphicsCommandList* commandList, float x, float y,
                  const std::string& utf8Text, float size, const Vector4& color,
                  TextFace face = TextFace::Body) {
        if (utf8Text.empty()) { return; }
        std::wstring text = ConvertString(utf8Text);
        float scale = size / (float)kGlyphPixelHeight;
        float penX = x;
        float penY = y;

        for (wchar_t ch : text) {
            if (ch == L'\n') { penX = x; penY += size * kLineSpacing; continue; }
            if (ch == L'\r') { continue; }

            const GlyphInfo& glyph = GetOrCreateGlyph(commandList, ch, face);
            if (glyph.valid && vertexCount_ + 6 <= kMaxGlyphsPerFrame * 6) {
                float left = penX + glyph.bearingX * scale;
                float top = penY + glyph.bearingY * scale;
                float right = left + glyph.width * scale;
                float bottom = top + glyph.height * scale;
                PushQuad(left, top, right, bottom, glyph.u0, glyph.v0, glyph.u1, glyph.v1, color);
            }
            penX += glyph.advance * scale;
        }
    }

    // 文字列に含まれる文字をアトラスへ用意しておく。
    // 幅を測るには全文字の字送りが分かっている必要があり、まだ切り出していない文字は
    // 推定値になってしまう。中央寄せの前にこれを呼ぶことで、
    // 「その文字列が初めて出たフレームだけ位置がずれる」のを防ぐ
    void EnsureGlyphs(ID3D12GraphicsCommandList* commandList, const std::string& utf8Text,
                      TextFace face = TextFace::Body) {
        for (wchar_t ch : ConvertString(utf8Text)) {
            if (ch == L'\n' || ch == L'\r') { continue; }
            GetOrCreateGlyph(commandList, ch, face);
        }
    }

    // 描く前に文字列の幅[px]を知りたいとき（中央寄せ・右寄せに使う）。
    // まだアトラスに無い文字は「だいたい正方形」とみなすので、
    // 正確に測りたい場合は先にEnsureGlyphs()を呼ぶこと
    float MeasureWidth(const std::string& utf8Text, float size, TextFace face = TextFace::Body) const {
        if (utf8Text.empty()) { return 0.0f; }
        std::wstring text = ConvertString(utf8Text);
        float scale = size / (float)kGlyphPixelHeight;
        float widest = 0.0f;
        float current = 0.0f;
        for (wchar_t ch : text) {
            if (ch == L'\n') { widest = (std::max)(widest, current); current = 0.0f; continue; }
            if (ch == L'\r') { continue; }
            current += AdvanceOf(ch, face, scale, size);
        }
        return (std::max)(widest, current);
    }

    // 1文字ぶんの字送り[px]。
    //
    // その書体に無い字は本文の書体で描かれる（GetOrCreateGlyph）ので、
    // 測る側も同じ所を見に行く。見に行かないと、描いた幅と測った幅が食い違い、
    // 中央寄せがずれたり、キーの案内の後ろに続く文字が重なったりする。
    // 欧文の書体（Label/Numeric）に記号や日本語を混ぜたときに効く
    float AdvanceOf(wchar_t ch, TextFace face, float scale, float size) const {
        const auto& glyphs = faces_[(int)face].glyphs;
        auto it = glyphs.find((uint32_t)ch);
        if (it != glyphs.end()) { return it->second.advance * scale; }
        if (face != TextFace::Body) {
            const auto& body = faces_[(int)TextFace::Body].glyphs;
            auto bodyIt = body.find((uint32_t)ch);
            if (bodyIt != body.end()) { return bodyIt->second.advance * scale; }
        }
        return size; // まだ切り出していない字。だいたい正方形とみなす
    }

    // 行数から高さ[px]を求める（縦方向の中央寄せに使う）
    float MeasureHeight(const std::string& utf8Text, float size) const {
        int lines = 1;
        for (char c : utf8Text) { if (c == '\n') { lines++; } }
        return size + (float)(lines - 1) * size * kLineSpacing;
    }

    // 溜めた文字をまとめて1回で描く。呼んだあとは空になるので、毎フレーム最後に1回呼べばよい。
    // 描画対象・ビューポートはすでに設定済みである前提（RenderFrameの中から呼ぶ）
    void Flush(ID3D12GraphicsCommandList* commandList, ID3D12DescriptorHeap* srvHeap, int screenWidth, int screenHeight) {
        if (vertexCount_ == 0) { return; }

        // アトラスへの貼り付けはDrawText時に済んでいるので、ここで読み取り可能な状態へ切り替える
        TransitionAtlas(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        ID3D12DescriptorHeap* heaps[] = { srvHeap };
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->SetGraphicsRootSignature(rootSignature_.Get());
        commandList->SetPipelineState(pso_.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->IASetVertexBuffers(0, 1, &vbv_);

        float invScreen[2] = {
            (screenWidth > 0) ? 1.0f / (float)screenWidth : 0.0f,
            (screenHeight > 0) ? 1.0f / (float)screenHeight : 0.0f,
        };
        commandList->SetGraphicsRoot32BitConstants(0, 2, invScreen, 0);
        commandList->SetGraphicsRootDescriptorTable(1, atlasSrvGPU_);
        commandList->DrawInstanced((UINT)vertexCount_, 1, 0, 0);

        // 次のフレームでまた貼り付けられるよう、コピー先として使える状態へ戻しておく
        TransitionAtlas(commandList, D3D12_RESOURCE_STATE_COPY_DEST);
        vertexCount_ = 0;
    }

    // このフレームに溜まっている文字数（デバッグ表示用）
    int QueuedGlyphCount() const { return vertexCount_ / 6; }

private:
    static constexpr float kLineSpacing = 1.25f; // 改行1つぶんの行送り（文字の高さに対する倍率）

    ID3D12Device* device_ = nullptr;

    // ===== GDI（Windowsのフォント機能）側 =====
    // 書体ごとにHFONTと切り出し済みの図形を持つ。アトラス（貼り付け先）は1枚を共有していて、
    // 書体が増えても描画命令は1回のままになる（書体ごとにアトラスを分けると、
    // 書体を切り替えるたびに描画命令が増えていく）
    struct FontFace {
        HFONT font = nullptr;
        int ascent = 0; // 文字の基準線から上端までの高さ[px]。図形の縦位置を決めるのに要る
        std::unordered_map<uint32_t, GlyphInfo> glyphs; // 文字コード → アトラス上の場所
    };
    std::array<FontFace, kTextFaceCount> faces_{};
    HDC hdc_ = nullptr;
    HGDIOBJ previousFont_ = nullptr;

    // ===== グリフアトラス =====
    ComPtr<ID3D12Resource> atlasResource_;
    D3D12_GPU_DESCRIPTOR_HANDLE atlasSrvGPU_{};
    D3D12_RESOURCE_STATES atlasState_ = D3D12_RESOURCE_STATE_COPY_DEST;

    // 棚（シェルフ）方式の場所取り：高さの揃った横一列を左から順に埋め、
    // 一杯になったら1段下がって新しい列を始める。文字の高さがほぼ揃っているこの用途では
    // 複雑な詰め込み計算をしなくても隙間がほとんど出ない
    int shelfX_ = 0;
    int shelfY_ = 0;
    int shelfHeight_ = 0;
    bool atlasFull_ = false;

    // アップロード用の一時バッファ。GPUがコピーを終えるまで生かしておく必要があるので、
    // 呼び出し側（Engine）が持つ「フレーム終わりに解放するリスト」へ預ける
    std::vector<ComPtr<ID3D12Resource>>* pendingUploads_ = nullptr;

    // ===== 描画パイプライン =====
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pso_;
    ComPtr<ID3D12Resource> vertexResource_;
    TextVertex* vertexData_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    int vertexCount_ = 0;

    // ---- フォントの用意 ----
    //
    // 書体は「どんな見た目か」ではなく「何に使うか」で分けてある。
    // 書体名を直に指定すると、その名前のフォントが入っていない環境で崩れるので、
    // どれも候補を並べて、頭から見つかったものを使う。
    // 最後の候補は必ずどの環境にもあるものにしておくこと
    void CreateFontResources() {
        hdc_ = CreateCompatibleDC(nullptr);

        struct FaceRequest {
            TextFace face;
            const wchar_t* candidates[5];
            int weight;
            BOOL italic;
        };
        const FaceRequest kRequests[] = {
            // 見出し。太いほど「ここが表題」と分かる
            { TextFace::Display, { L"Yu Gothic UI", L"Meiryo", L"MS Gothic", L"Segoe UI", nullptr }, FW_HEAVY, FALSE },
            // 本文。太いと長い文が読みにくいので普通の太さにする
            { TextFace::Body,    { L"Meiryo", L"Yu Gothic UI", L"MS Gothic", L"Segoe UI", nullptr }, FW_NORMAL, FALSE },
            // 数字。寝かせた太字で、点や倍率が「数字として」目に入るようにする
            { TextFace::Numeric, { L"Impact", L"Arial Black", L"Segoe UI Black", L"Arial", nullptr }, FW_BLACK, TRUE },
            // 小さい注記。等幅にすると、キーの案内が機械的な見た目に落ち着く
            { TextFace::Label,   { L"Consolas", L"Courier New", L"Segoe UI", L"Arial", nullptr }, FW_SEMIBOLD, FALSE },
        };

        for (const FaceRequest& request : kRequests) {
            FontFace& slot = faces_[(int)request.face];
            for (const wchar_t* name : request.candidates) {
                if (!name) { break; }
                slot.font = CreateFontW(
                    -kGlyphPixelHeight, 0, 0, 0, request.weight, request.italic, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
                if (slot.font) { break; }
            }
            if (!slot.font) { continue; }
            HGDIOBJ previous = SelectObject(hdc_, slot.font);
            if (!previousFont_) { previousFont_ = previous; } // 最初に押しのけたものだけ覚えておく
            TEXTMETRICW metrics{};
            GetTextMetricsW(hdc_, &metrics);
            slot.ascent = metrics.tmAscent;
        }
    }

    // その書体がその文字を持っているか。
    //
    // 欧文の書体に日本語を渡すと、GDIは黙って豆腐（.notdef）を返す。
    // 何も確かめずに切り出すと、数字の書体で日本語を書いた瞬間に四角が並ぶ。
    // 持っていない字はBodyへ回すことで、混ざっても読めなくなることはない
    bool FaceHasGlyph(const FontFace& faceSlot, wchar_t ch) const {
        if (!faceSlot.font || !hdc_) { return false; }
        SelectObject(hdc_, faceSlot.font);
        WORD index = 0;
        if (GetGlyphIndicesW(hdc_, &ch, 1, &index, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) { return false; }
        return index != 0xFFFF;
    }

    // ---- アトラス用テクスチャとSRVの用意 ----
    void CreateAtlasTexture(GraphicsContext& gfx) {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kAtlasSize;
        desc.Height = kAtlasSize;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM; // 濃さだけなので1チャンネルで足りる
        desc.SampleDesc.Count = 1;

        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        HRESULT hr = gfx.device->CreateCommittedResource(
            &heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&atlasResource_));
        assert(SUCCEEDED(hr));
        atlasState_ = D3D12_RESOURCE_STATE_COPY_DEST;

        UINT srvIndex = gfx.nextSrvIndex++;
        D3D12_CPU_DESCRIPTOR_HANDLE srvCPU = gfx.srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        srvCPU.ptr += (size_t)gfx.descriptorSizeSRV * srvIndex;
        atlasSrvGPU_ = gfx.srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        atlasSrvGPU_.ptr += (UINT64)gfx.descriptorSizeSRV * srvIndex;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        gfx.device->CreateShaderResourceView(atlasResource_.Get(), &srvDesc, srvCPU);
    }

    // ---- 半透明合成つきの2Dパイプライン ----
    void CreatePipeline(ID3D12Device* device, Object3DPipeline& sharedCompiler) {
        // ルートパラメータ0：画面サイズの逆数（頂点シェーダー）。たった2つの値なので定数バッファを作らず直接渡す
        // ルートパラメータ1：グリフアトラス（ピクセルシェーダー）
        D3D12_ROOT_PARAMETER rootParameters[2] = {};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        rootParameters[0].Constants.ShaderRegister = 0;
        rootParameters[0].Constants.Num32BitValues = 2;

        D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
        descriptorRange[0].BaseShaderRegister = 0;
        descriptorRange[0].NumDescriptors = 1;
        descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRange;
        rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

        // 文字の端が繰り返しでちらつかないよう、はみ出したら端の色を伸ばす（CLAMP）
        D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
        staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
        staticSamplers[0].ShaderRegister = 0;
        staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
        descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        descriptionRootSignature.pParameters = rootParameters;
        descriptionRootSignature.NumParameters = _countof(rootParameters);
        descriptionRootSignature.pStaticSamplers = staticSamplers;
        descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

        ComPtr<ID3DBlob> sigBlob;
        ComPtr<ID3DBlob> errBlob;
        HRESULT hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
        assert(SUCCEEDED(hr));
        hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
        assert(SUCCEEDED(hr));

        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        ComPtr<IDxcBlob> vsBlob = CompileShader(L"Text.VS.hlsl", L"vs_6_0", sharedCompiler.dxcUtils.Get(), sharedCompiler.dxcCompiler.Get(), sharedCompiler.includeHandler.Get());
        ComPtr<IDxcBlob> psBlob = CompileShader(L"Text.PS.hlsl", L"ps_6_0", sharedCompiler.dxcUtils.Get(), sharedCompiler.dxcCompiler.Get(), sharedCompiler.includeHandler.Get());

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSignature_.Get();
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

        // ここがこのパイプラインの肝：半透明合成を有効にする。
        //   出力 = 文字の色 × 文字の濃さ + 背景 × (1 - 文字の濃さ)
        // これまでのパイプラインは合成を一切設定していなかったため、何を描いても不透明で、
        // 文字の輪郭が黒い四角に囲まれてしまう
        D3D12_RENDER_TARGET_BLEND_DESC& blend = psoDesc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

        // 画面に貼り付けるUIなので、3Dの奥行きとは無関係に必ず手前に出す
        D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
        depthStencilDesc.DepthEnable = false;
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

        hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_));
        assert(SUCCEEDED(hr));
    }

    void CreateVertexBuffer(ID3D12Device* device) {
        size_t sizeInBytes = sizeof(TextVertex) * kMaxGlyphsPerFrame * 6;
        vertexResource_ = CreateBufferResource(device, sizeInBytes);
        vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
        vbv_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
        vbv_.SizeInBytes = (UINT)sizeInBytes;
        vbv_.StrideInBytes = sizeof(TextVertex);
    }

    void TransitionAtlas(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES next) {
        if (atlasState_ == next) { return; }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = atlasResource_.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = atlasState_;
        barrier.Transition.StateAfter = next;
        commandList->ResourceBarrier(1, &barrier);
        atlasState_ = next;
    }

    void PushQuad(float left, float top, float right, float bottom,
                  float u0, float v0, float u1, float v1, const Vector4& color) {
        TextVertex* v = vertexData_ + vertexCount_;
        v[0] = { { left,  top    }, { u0, v0 }, color };
        v[1] = { { right, top    }, { u1, v0 }, color };
        v[2] = { { left,  bottom }, { u0, v1 }, color };
        v[3] = { { right, top    }, { u1, v0 }, color };
        v[4] = { { right, bottom }, { u1, v1 }, color };
        v[5] = { { left,  bottom }, { u0, v1 }, color };
        vertexCount_ += 6;
    }

    // 文字の図形をアトラスから引く。まだ無ければWindowsのフォントから切り出して貼り付ける
    const GlyphInfo& GetOrCreateGlyph(ID3D12GraphicsCommandList* commandList, wchar_t ch, TextFace face) {
        // その書体に字が無ければ本文の書体で出す。豆腐が並ぶよりは書体が変わる方がまし
        int index = (int)face;
        if (!faces_[index].font || !FaceHasGlyph(faces_[index], ch)) { index = (int)TextFace::Body; }

        auto& glyphs = faces_[index].glyphs;
        auto it = glyphs.find((uint32_t)ch);
        if (it != glyphs.end()) { return it->second; }

        GlyphInfo glyph = RasterizeGlyph(commandList, ch, index);
        return glyphs.emplace((uint32_t)ch, glyph).first->second;
    }

    // Windowsに文字の形を作ってもらい、アトラスへ貼り付ける
    GlyphInfo RasterizeGlyph(ID3D12GraphicsCommandList* commandList, wchar_t ch, int faceIndex) {
        GlyphInfo glyph{};
        const FontFace& faceSlot = faces_[faceIndex];
        if (!hdc_ || !faceSlot.font) { return glyph; }
        // 切り出す前に、必ずその書体をDCへ入れておくこと。
        // 入れ忘れると、直前に使った書体の形が別の書体の名前で貼られる
        SelectObject(hdc_, faceSlot.font);
        const int ascent = faceSlot.ascent;

        // アトラスが一杯でも、字送りの量だけは必ず求めておく。
        // ここで打ち切ると advance が0のままになり、
        // 以降の文字がすべて同じ位置に重なって描かれてしまう（見た目が完全に壊れる）

        // 変形なし（等倍・回転なし）を表す行列。FIXED型は「整数部, 小数部」なので1.0は{0, 1}
        static const MAT2 kIdentity = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
        GLYPHMETRICS metrics{};
        DWORD size = GetGlyphOutlineW(hdc_, ch, GGO_GRAY8_BITMAP, &metrics, 0, nullptr, &kIdentity);
        if (size == GDI_ERROR) { return glyph; }

        // 字送りの量と、基準点から見た図形の位置。空白のように図形を持たない文字でもこれは意味がある
        glyph.advance = (float)metrics.gmCellIncX;
        glyph.bearingX = (float)metrics.gmptGlyphOrigin.x;
        glyph.bearingY = (float)(ascent - metrics.gmptGlyphOrigin.y); // 上端からの距離に直す
        glyph.width = (float)metrics.gmBlackBoxX;
        glyph.height = (float)metrics.gmBlackBoxY;

        if (size == 0 || metrics.gmBlackBoxX == 0 || metrics.gmBlackBoxY == 0) {
            return glyph; // 空白など、進むだけで何も描かない文字
        }

        std::vector<BYTE> bitmap(size);
        if (GetGlyphOutlineW(hdc_, ch, GGO_GRAY8_BITMAP, &metrics, size, bitmap.data(), &kIdentity) == GDI_ERROR) {
            return glyph;
        }

        if (atlasFull_) { return glyph; } // 場所が無いので図形は貼らない（字送りだけ効く）

        int glyphWidth = (int)metrics.gmBlackBoxX;
        int glyphHeight = (int)metrics.gmBlackBoxY;
        int srcPitch = (glyphWidth + 3) & ~3; // GDIの返す各行は4バイト単位に切り上げられている

        int destX = 0, destY = 0;
        if (!AllocateAtlasSpace(glyphWidth, glyphHeight, destX, destY)) {
            atlasFull_ = true;
            return glyph;
        }

        UploadGlyphToAtlas(commandList, bitmap, srcPitch, glyphWidth, glyphHeight, destX, destY);

        const float inv = 1.0f / (float)kAtlasSize;
        glyph.u0 = (float)destX * inv;
        glyph.v0 = (float)destY * inv;
        glyph.u1 = (float)(destX + glyphWidth) * inv;
        glyph.v1 = (float)(destY + glyphHeight) * inv;
        glyph.valid = true;
        return glyph;
    }

    // アトラス上の空き場所を棚方式で確保する
    bool AllocateAtlasSpace(int width, int height, int& outX, int& outY) {
        if (width > kAtlasSize || height > kAtlasSize) { return false; }

        if (shelfX_ + width + kGlyphPadding > kAtlasSize) {
            // 今の棚の右端まで来た。1段下がって新しい棚を始める
            shelfY_ += shelfHeight_ + kGlyphPadding;
            shelfX_ = 0;
            shelfHeight_ = 0;
        }
        if (shelfY_ + height + kGlyphPadding > kAtlasSize) { return false; } // 下まで使い切った

        outX = shelfX_;
        outY = shelfY_;
        shelfX_ += width + kGlyphPadding;
        shelfHeight_ = (std::max)(shelfHeight_, height);
        return true;
    }

    // 切り出した図形をアトラステクスチャの指定位置へコピーする
    void UploadGlyphToAtlas(ID3D12GraphicsCommandList* commandList, const std::vector<BYTE>& bitmap,
                            int srcPitch, int width, int height, int destX, int destY) {
        // テクスチャへのコピー元は、各行の先頭が256バイト境界に揃っている必要がある（DirectX12の決まり）
        const UINT alignedPitch = ((UINT)width + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        ComPtr<ID3D12Resource> upload = CreateBufferResource(device_, (size_t)alignedPitch * height);

        BYTE* mapped = nullptr;
        upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        for (int y = 0; y < height; ++y) {
            const BYTE* src = bitmap.data() + (size_t)y * srcPitch;
            BYTE* dest = mapped + (size_t)y * alignedPitch;
            for (int x = 0; x < width; ++x) {
                // GGO_GRAY8が返す濃さは0〜64。0〜255へ伸ばす（64段階でも見た目には十分滑らか）
                BYTE level = src[x];
                dest[x] = (level >= 64) ? 255 : (BYTE)((level * 255) / 64);
            }
        }
        upload->Unmap(0, nullptr);

        TransitionAtlas(commandList, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_TEXTURE_COPY_LOCATION destLocation{};
        destLocation.pResource = atlasResource_.Get();
        destLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destLocation.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLocation{};
        srcLocation.pResource = upload.Get();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLocation.PlacedFootprint.Offset = 0;
        srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
        srcLocation.PlacedFootprint.Footprint.Width = (UINT)width;
        srcLocation.PlacedFootprint.Footprint.Height = (UINT)height;
        srcLocation.PlacedFootprint.Footprint.Depth = 1;
        srcLocation.PlacedFootprint.Footprint.RowPitch = alignedPitch;

        commandList->CopyTextureRegion(&destLocation, (UINT)destX, (UINT)destY, 0, &srcLocation, nullptr);

        // GPUがコピーを終えるまでこのバッファは生かしておかなければならない。
        // 呼び出し側がフレーム終わり（GPU待機のあと）にまとめて解放してくれる
        if (pendingUploads_) { pendingUploads_->push_back(upload); }
    }
};
