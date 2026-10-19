// cspell:ignore dxgi dxguid dxcapi IDxc IDXGI DXGI hlsl wvp HLSL
#pragma once
#include "Object3D.h"

// ============================================================================
// ウィンドウ生成・DirectX12デバイス・パイプラインステート・描画コマンドなど、
// 「画面に出すための土台」をまとめたファイル。ゲームの内容には一切関与しない。
// ============================================================================

#ifdef _DEBUG
// デバッグレイヤーの有効化
inline void EnableDebugLayer() {
    ComPtr<ID3D12Debug1> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        debugController->SetEnableGPUBasedValidation(TRUE);
    }
}
#endif

// DirectX12の基盤（デバイス・コマンド関連・スワップチェーン・各種ディスクリプタヒープ・深度バッファ等）一式
// 「この時したい動作」ではなく「常に必要な土台」なので、まとめてここに持たせる
struct GraphicsContext {
    ComPtr<IDXGIFactory7> dxgiFactory;
    ComPtr<IDXGIAdapter4> useAdapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12Resource> swapChainResources[2];
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2]{};
    ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;

    ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
    UINT descriptorSizeSRV = 0;
    // SRVヒープに置ける枚数。テクスチャ付きモデルを読むたびに1枚ずつ使うので、
    // 上限を超えていないかを使う側（GetOrLoadCachedModelKeys など）が必ず確かめること。
    // 超えたまま書き込むとヒープの外を触ることになり、原因の分かりにくい描画崩れ・強制終了になる
    static constexpr UINT kSrvDescriptorCount = 128;
    UINT nextSrvIndex = 3; // 0:ImGuiのフォント（Debugのみ）, 1:uvChecker, 2:白で使用済み。次に空くのは3番目から

    ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;
    ComPtr<ID3D12Resource> depthStencilResource;

    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    uint64_t fenceValue = 0;

    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // 画面のRTVの形式（作り直すときに使う）
    int32_t clientWidth = 0;
    int32_t clientHeight = 0;
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};

    // objファイル読み込みでアップロードした中間リソース（GPU転送完了まで解放できないため、
    // コマンド実行＆フェンス待機の後にまとめて解放する）
    std::vector<ComPtr<ID3D12Resource>> pendingIntermediateResources;

    // コマンドキューに積んだ処理をGPUが完了するまでCPUを待たせる
    void WaitForGPU() {
        fenceValue++;
        commandQueue->Signal(fence.Get(), fenceValue);
        if (fence->GetCompletedValue() < fenceValue) {
            fence->SetEventOnCompletion(fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }
};

// 画面へ書き出すときの色の扱い。
//
//   AsIs : 計算した値をそのまま置く。このエンジンが今まで出していた見え方
//   Srgb : 置くときにsRGBへ直す。同じ数字でも明るく見える
//
// 本来はSrgbが筋（絵はsRGBとして読み込み、計算はリニアで行っているので、
// 出すときに戻すのが対）。ただしAsIsで見た目を決め切ったゲームがすでにあるため、
// 既定はSrgb（直す）。各PSOは「sRGBで描く」と名乗っているので、
// 直さないと描き先の形式と食い違い、D3D12のデバッグレイヤーが
// 毎フレーム「The render target format in slot 0 does not match」と怒る。
//
// AsIsを選べるようにしてあるのは、sRGBへ直さないエンジンで作った物を
// 載せ替えて、そのときと同じ色で出したいときのため。
// 切り替えると絵の色が全部変わる
enum class SurfaceColor {
    AsIs,
    Srgb,
};

// GraphicsContext一式を構築する
inline GraphicsContext CreateGraphicsContext(HWND hwnd, int32_t clientWidth, int32_t clientHeight,
                                             SurfaceColor surfaceColor = SurfaceColor::AsIs) {
    GraphicsContext gfx;
    gfx.clientWidth = clientWidth;
    gfx.clientHeight = clientHeight;

    // DXGIファクトリーとアダプターの生成
    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&gfx.dxgiFactory));
    assert(SUCCEEDED(hr));

    hr = gfx.dxgiFactory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&gfx.useAdapter));
    assert(SUCCEEDED(hr));

    // D3D12デバイスの生成
    hr = D3D12CreateDevice(gfx.useAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&gfx.device));
    assert(SUCCEEDED(hr));

    // コマンド関連リソースの生成
    D3D12_COMMAND_QUEUE_DESC cqDesc{};
    hr = gfx.device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&gfx.commandQueue));
    assert(SUCCEEDED(hr));

    hr = gfx.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gfx.commandAllocator));
    assert(SUCCEEDED(hr));

    hr = gfx.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gfx.commandAllocator.Get(), nullptr, IID_PPV_ARGS(&gfx.commandList));
    assert(SUCCEEDED(hr));

    // スワップチェーンの生成
    gfx.swapChainDesc = {
        (UINT)clientWidth, (UINT)clientHeight, DXGI_FORMAT_R8G8B8A8_UNORM, FALSE, {1,0},
        DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, DXGI_SCALING_STRETCH, DXGI_SWAP_EFFECT_FLIP_DISCARD,
        DXGI_ALPHA_MODE_UNSPECIFIED, 0
    };
    // swapChainはComPtr<IDXGISwapChain4>だが、CreateSwapChainForHwndが要求するのはIDXGISwapChain1**なので、
    // GetAddressOf()で取得したComPtr管理下のポインタへのアドレスをreinterpret_castして渡す
    hr = gfx.dxgiFactory->CreateSwapChainForHwnd(gfx.commandQueue.Get(), hwnd, &gfx.swapChainDesc, nullptr, nullptr,
        reinterpret_cast<IDXGISwapChain1**>(gfx.swapChain.GetAddressOf()));
    assert(SUCCEEDED(hr));
    // Alt+EnterでDXGIが勝手に排他的フルスクリーンへ切り替えるのを止める。
    // 全画面の出し入れはエンジンが行う（ボーダーレス。Engine::SetFullscreen）
    gfx.dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    // ディスクリプターヒープの生成
    gfx.rtvDescriptorHeap = CreateDescriptorHeap(gfx.device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
    gfx.srvDescriptorHeap = CreateDescriptorHeap(gfx.device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, GraphicsContext::kSrvDescriptorCount, true);
    gfx.descriptorSizeSRV = gfx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // レンダーターゲットビュー（RTV）の作成
    const UINT descriptorSizeRTV = gfx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = gfx.rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    gfx.rtvHandles[0] = rtvStartHandle;
    // 画面そのものはUNORMで作る（フリップ方式のスワップチェーンはsRGBの形式を受け付けない）。
    // sRGBで出したいときは、同じ物を見る「窓」の側をsRGBにする。
    // 直すのはハードウェアなので、絵を描く側は何も変えなくてよい
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = (surfaceColor == SurfaceColor::Srgb) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                          : DXGI_FORMAT_R8G8B8A8_UNORM;
    gfx.rtvFormat = rtvDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    gfx.swapChain->GetBuffer(0, IID_PPV_ARGS(&gfx.swapChainResources[0]));
    gfx.device->CreateRenderTargetView(gfx.swapChainResources[0].Get(), &rtvDesc, gfx.rtvHandles[0]);
    gfx.rtvHandles[1].ptr = gfx.rtvHandles[0].ptr + descriptorSizeRTV;
    gfx.swapChain->GetBuffer(1, IID_PPV_ARGS(&gfx.swapChainResources[1]));
    gfx.device->CreateRenderTargetView(gfx.swapChainResources[1].Get(), &rtvDesc, gfx.rtvHandles[1]);

    // フェンスの作成
    hr = gfx.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gfx.fence));
    assert(SUCCEEDED(hr));
    gfx.fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // 深度ステンシルリソースとDSVの作成
    gfx.depthStencilResource = CreateDepthStencilTextureResource(gfx.device.Get(), clientWidth, clientHeight);
    gfx.dsvDescriptorHeap = CreateDescriptorHeap(gfx.device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    gfx.device->CreateDepthStencilView(gfx.depthStencilResource.Get(), &dsvDesc, gfx.dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    gfx.viewport = { 0, 0, (float)clientWidth, (float)clientHeight, 0.0f, 1.0f };
    gfx.scissorRect = { 0, 0, clientWidth, clientHeight };

    return gfx;
}

// 画面の大きさが変わったとき（全画面の出し入れ）に、大きさに依存するものを作り直す：
// スワップチェーンのバッファ・RTV・深度バッファ・ビューポート。
// GPUが使い終わってからでないとバッファを手放せないので、先に待つ
inline void ResizeGraphicsContext(GraphicsContext& gfx, int32_t clientWidth, int32_t clientHeight) {
    if (clientWidth <= 0 || clientHeight <= 0) { return; } // 最小化中など
    gfx.WaitForGPU();

    gfx.swapChainResources[0].Reset();
    gfx.swapChainResources[1].Reset();
    HRESULT hr = gfx.swapChain->ResizeBuffers(0, (UINT)clientWidth, (UINT)clientHeight, DXGI_FORMAT_UNKNOWN, 0);
    assert(SUCCEEDED(hr));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = gfx.rtvFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (UINT i = 0; i < 2; ++i) {
        gfx.swapChain->GetBuffer(i, IID_PPV_ARGS(&gfx.swapChainResources[i]));
        gfx.device->CreateRenderTargetView(gfx.swapChainResources[i].Get(), &rtvDesc, gfx.rtvHandles[i]);
    }

    gfx.depthStencilResource = CreateDepthStencilTextureResource(gfx.device.Get(), clientWidth, clientHeight);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    gfx.device->CreateDepthStencilView(gfx.depthStencilResource.Get(), &dsvDesc, gfx.dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    gfx.clientWidth = clientWidth;
    gfx.clientHeight = clientHeight;
    gfx.viewport = { 0, 0, (float)clientWidth, (float)clientHeight, 0.0f, 1.0f };
    gfx.scissorRect = { 0, 0, clientWidth, clientHeight };
}

// Object3D描画用のシェーダー・ルートシグネチャ・PSO一式
struct Object3DPipeline {
    ComPtr<IDxcUtils> dxcUtils;
    ComPtr<IDxcCompiler3> dxcCompiler;
    ComPtr<IDxcIncludeHandler> includeHandler;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pso; // 不透明用（psos[kNone]と同じ物）
    ComPtr<ID3D12PipelineState> psos[(int)BlendMode::kCount]; // ブレンドモードごとのPSO
    ComPtr<ID3D12PipelineState> depthOnlyPso; // 奥行きだけを書く（半透明の下準備）
};

// ブレンドモードに対応するブレンド設定を作る
inline D3D12_RENDER_TARGET_BLEND_DESC MakeBlendDesc(BlendMode mode) {
    D3D12_RENDER_TARGET_BLEND_DESC blend{};
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (mode == BlendMode::kNone) { return blend; }

    blend.BlendEnable = TRUE;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    switch (mode) {
    case BlendMode::kNormal:
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        break;
    case BlendMode::kAdd:
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_ONE;
        break;
    case BlendMode::kSubtract:
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_ONE;
        blend.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT; // Dest - Src
        break;
    case BlendMode::kMultiply:
        blend.SrcBlend = D3D12_BLEND_ZERO;
        blend.DestBlend = D3D12_BLEND_SRC_COLOR;
        break;
    case BlendMode::kScreen:
        blend.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
        blend.DestBlend = D3D12_BLEND_ONE;
        break;
    default: break;
    }
    // αチャンネルは色の合成方法に関わらず、Srcをそのまま通す
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    return blend;
}

// Object3DPipeline一式を構築する（ルートシグネチャ定義・シェーダーコンパイル・PSO作成）
inline Object3DPipeline CreateObject3DPipeline(ID3D12Device* device) {
    Object3DPipeline pipeline;

    // DXCコンパイラの初期化
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pipeline.dxcUtils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pipeline.dxcCompiler));
    pipeline.dxcUtils->CreateDefaultIncludeHandler(&pipeline.includeHandler);

    // ルートシグネチャの構築
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Descriptor.ShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

    // 平行光源用のCBufferをbindする（PixelShaderで利用、レジスタ番号1）
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Descriptor.ShaderRegister = 1;

    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    // 静的サンプラーの設定
    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    // ルートシグネチャのシリアライズと作成
    ComPtr<ID3DBlob> sigBlob;
    ComPtr<ID3DBlob> errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));

    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&pipeline.rootSignature));
    assert(SUCCEEDED(hr));

    // インプットレイアウトの設定
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,      0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,   0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    // シェーダーのコンパイル
    ComPtr<IDxcBlob> vsBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", pipeline.dxcUtils.Get(), pipeline.dxcCompiler.Get(), pipeline.includeHandler.Get());
    ComPtr<IDxcBlob> psBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", pipeline.dxcUtils.Get(), pipeline.dxcCompiler.Get(), pipeline.includeHandler.Get());

    // パイプラインステートオブジェクト（PSO）の設定
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = pipeline.rootSignature.Get();
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    // 深度ステンシルステートの設定
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // ブレンドモードごとにPSOを作る。半透明の物は奥行きを書き込まない
    // （書き込むと、後ろにある物が描かれずに消えてしまう）
    for (int i = 0; i < (int)BlendMode::kCount; ++i) {
        BlendMode mode = (BlendMode)i;
        psoDesc.BlendState.RenderTarget[0] = MakeBlendDesc(mode);
        psoDesc.DepthStencilState.DepthWriteMask =
            (mode == BlendMode::kNone) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline.psos[i]));
        assert(SUCCEEDED(hr));
    }
    pipeline.pso = pipeline.psos[(int)BlendMode::kNone];

    // 奥行きだけを書くPSO（色は書かない）。半透明の物を描く直前に同じ物を1回描いておくと、
    // 1つの物の中で「奥の面が手前の面を上書きする」のを防げる（一番手前の面だけが合成される）
    psoDesc.BlendState.RenderTarget[0] = MakeBlendDesc(BlendMode::kNone);
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline.depthOnlyPso));
    assert(SUCCEEDED(hr));

    return pipeline;
}


// ===== コライダー（ボックス）のワイヤーフレーム描画用パイプライン =====
// 位置だけを持つシンプルな頂点で、色は固定（緑）のPixelShaderを使う軽量なライン専用パイプライン
struct LineVertex {
    Vector3 position;
};

struct LinePipeline {
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pso;
};

inline LinePipeline CreateLinePipeline(ID3D12Device* device, Object3DPipeline& sharedCompiler) {
    LinePipeline pipeline;

    // ルートパラメータ0：WVP用のCBV（VertexShaderのみ）
    // ルートパラメータ1：ボックスの色（PixelShaderのみ）。衝突状態に応じて緑/赤を切り替えるためRoot Constantで渡す
    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Constants.ShaderRegister = 1;
    rootParameters[1].Constants.Num32BitValues = 4; // float4 1個分

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    ComPtr<ID3DBlob> sigBlob;
    ComPtr<ID3DBlob> errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&pipeline.rootSignature));
    assert(SUCCEEDED(hr));

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // DXCコンパイラはObject3DPipeline作成時に用意したものを使い回す
    ComPtr<IDxcBlob> vsBlob = CompileShader(L"Line.VS.hlsl", L"vs_6_0", sharedCompiler.dxcUtils.Get(), sharedCompiler.dxcCompiler.Get(), sharedCompiler.includeHandler.Get());
    ComPtr<IDxcBlob> psBlob = CompileShader(L"Line.PS.hlsl", L"ps_6_0", sharedCompiler.dxcUtils.Get(), sharedCompiler.dxcCompiler.Get(), sharedCompiler.includeHandler.Get());

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = pipeline.rootSignature.Get();
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    // Unityのシーンギズモのように、他のオブジェクトに隠れず常に手前に見えるよう深度テストは行わない
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = false;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline.pso));
    assert(SUCCEEDED(hr));

    return pipeline;
}

// コライダー可視化用：一辺1の立方体のワイヤーフレーム（頂点8つ・辺12本＝24インデックスのLineList）を1回だけ作る
// 各オブジェクトのOBBは、このユニットボックスをMakeOBBWorldMatrix()で変形するだけで表現できる
// （回転軸×サイズで各辺を作り、最小コーナーへ平行移動する）
struct LineBoxMesh {
    ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    ComPtr<ID3D12Resource> indexResource;
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT indexCount = 0;
};

inline LineBoxMesh CreateLineBoxMesh(ID3D12Device* device) {
    LineBoxMesh mesh;

    std::vector<LineVertex> vertices = {
        { {0,0,0} }, { {1,0,0} }, { {0,1,0} }, { {1,1,0} },
        { {0,0,1} }, { {1,0,1} }, { {0,1,1} }, { {1,1,1} },
    };
    std::vector<uint32_t> indices = {
        0,1, 1,3, 3,2, 2,0, // 底面
        4,5, 5,7, 7,6, 6,4, // 上面
        0,4, 1,5, 2,6, 3,7, // 側面（縦の辺）
    };

    size_t vertexSizeInBytes = sizeof(LineVertex) * vertices.size();
    mesh.vertexResource = CreateBufferResource(device, vertexSizeInBytes);
    LineVertex* vertexData = nullptr;
    mesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::copy(vertices.begin(), vertices.end(), vertexData);
    mesh.vertexResource->Unmap(0, nullptr);
    mesh.vbv.BufferLocation = mesh.vertexResource->GetGPUVirtualAddress();
    mesh.vbv.SizeInBytes = (UINT)vertexSizeInBytes;
    mesh.vbv.StrideInBytes = sizeof(LineVertex);

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

// ===== 影（地面に落とす丸い染み）用のパイプライン =====
// 影が無いと、横から見た画面では「今どのくらいの高さにいるのか」「どこへ落ちるのか」が分からない。
// 見栄えの問題ではなく、狙って落とす遊びが成立するかどうかに直結する。
//
// 本格的な影（シャドウマップ）は、光源から見た深度を描いてから本描画で引き比べる二段構えで、
// 描画の仕組み全体に手を入れることになる。ここでは「真下に丸い染みを置く」だけの方式にしてある。
// 平らな足場の上に物が乗っているという今の作りには、これで十分に用が足りる
struct ShadowPipeline {
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pso;
};

inline ShadowPipeline CreateShadowPipeline(ID3D12Device* device, Object3DPipeline& sharedCompiler) {
    ShadowPipeline pipeline;

    // ルートパラメータ0：WVP（頂点シェーダー）／1：影の色と濃さ（ピクセルシェーダー）
    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Constants.ShaderRegister = 1;
    rootParameters[1].Constants.Num32BitValues = 4;

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    ComPtr<ID3DBlob> sigBlob;
    ComPtr<ID3DBlob> errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&pipeline.rootSignature));
    assert(SUCCEEDED(hr));

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    ComPtr<IDxcBlob> vsBlob = CompileShader(L"Shadow.VS.hlsl", L"vs_6_0", sharedCompiler.dxcUtils.Get(), sharedCompiler.dxcCompiler.Get(), sharedCompiler.includeHandler.Get());
    ComPtr<IDxcBlob> psBlob = CompileShader(L"Shadow.PS.hlsl", L"ps_6_0", sharedCompiler.dxcUtils.Get(), sharedCompiler.dxcCompiler.Get(), sharedCompiler.includeHandler.Get());

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = pipeline.rootSignature.Get();
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // 影は地面に薄く重ねるものなので半透明合成を有効にする
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

    // 手前にある物には隠れてほしいので奥行きの判定は行うが、
    // 影自身は奥行きを書き込まない（半透明なので、書き込むと後ろの物が消えてしまう）
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline.pso));
    assert(SUCCEEDED(hr));

    return pipeline;
}

// 影1つぶんの板（XZ平面に置いた1辺1の四角形。中心が原点なので、拡大縮小がそのまま影の大きさになる）
inline LineBoxMesh CreateShadowQuadMesh(ID3D12Device* device) {
    LineBoxMesh mesh;

    std::vector<LineVertex> vertices = {
        { { -0.5f, 0.0f, -0.5f } }, { { 0.5f, 0.0f, -0.5f } },
        { { -0.5f, 0.0f,  0.5f } }, { { 0.5f, 0.0f,  0.5f } },
    };
    std::vector<uint32_t> indices = { 0, 1, 2, 2, 1, 3 };

    size_t vertexSizeInBytes = sizeof(LineVertex) * vertices.size();
    mesh.vertexResource = CreateBufferResource(device, vertexSizeInBytes);
    LineVertex* vertexData = nullptr;
    mesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::copy(vertices.begin(), vertices.end(), vertexData);
    mesh.vertexResource->Unmap(0, nullptr);
    mesh.vbv.BufferLocation = mesh.vertexResource->GetGPUVirtualAddress();
    mesh.vbv.SizeInBytes = (UINT)vertexSizeInBytes;
    mesh.vbv.StrideInBytes = sizeof(LineVertex);

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

// コライダーのボックス1個を描画する際に必要なWVP専用の定数バッファ（1個分）
struct LineTransformBuffer {
    ComPtr<ID3D12Resource> resource;
    Matrix4x4* data = nullptr;
};

inline LineTransformBuffer CreateLineTransformBuffer(ID3D12Device* device) {
    LineTransformBuffer buffer;
    buffer.resource = CreateBufferResource(device, sizeof(Matrix4x4));
    buffer.resource->Map(0, nullptr, reinterpret_cast<void**>(&buffer.data));
    *buffer.data = MakeIdentity4x4();
    return buffer;
}

// ===== 標準で使えるテクスチャ（uvChecker/白）をSRVスロット1,2に読み込んでおく =====
struct DefaultTextures {
    ComPtr<ID3D12Resource> uvCheckerResource;
    D3D12_GPU_DESCRIPTOR_HANDLE uvCheckerSrv{};
    // 真っ白な1枚。これを貼ると、材質の色がそのまま出る
    ComPtr<ID3D12Resource> whiteResource;
    D3D12_GPU_DESCRIPTOR_HANDLE whiteSrv{};
};

// 真っ白なテクスチャを、ファイルを持たずにその場で作る。
//
// シェーダは必ずテクスチャを1枚読むので、「貼らない」という選択肢が作れない。
// 代わりに全部が白い1枚を貼れば、色の掛け算で材質の色だけが残る。
// ファイルにしないのは、配布する一式から1枚落ちただけで
// 画面じゅうが真っ黒になる、という壊れ方をさせないため
inline DirectX::ScratchImage CreateWhiteImage() {
    DirectX::ScratchImage image;
    image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1);
    uint8_t* pixels = image.GetPixels();
    pixels[0] = pixels[1] = pixels[2] = pixels[3] = 255;
    return image;
}

// デフォルトテクスチャを読み込む。アップロードはこの関数内でコマンド実行〜GPU待機まで完結させる
// （呼び出し側でpendingIntermediateResourcesを気にしなくていいようにするため）
inline DefaultTextures LoadDefaultTextures(GraphicsContext& gfx) {
    DefaultTextures textures;
    std::vector<ComPtr<ID3D12Resource>> intermediateResources;

    textures.uvCheckerSrv = LoadTextureToSRV(
        gfx.device.Get(), gfx.commandList.Get(), gfx.srvDescriptorHeap.Get(), gfx.descriptorSizeSRV, 1,
        "resources/uvChecker.png", textures.uvCheckerResource, intermediateResources);
    textures.whiteSrv = CreateTextureSRV(
        gfx.device.Get(), gfx.commandList.Get(), gfx.srvDescriptorHeap.Get(), gfx.descriptorSizeSRV, 2,
        CreateWhiteImage(), textures.whiteResource, intermediateResources);

    // このままではアップロード用中間リソースがGPU転送完了前に破棄されうるので、コマンドを実行してGPU完了を待つ
    gfx.commandList->Close();
    ID3D12CommandList* lists[] = { gfx.commandList.Get() };
    gfx.commandQueue->ExecuteCommandLists(1, lists);
    gfx.WaitForGPU();
    gfx.commandAllocator->Reset();
    gfx.commandList->Reset(gfx.commandAllocator.Get(), nullptr);
    // ここでintermediateResourcesがスコープを抜けて自動的に解放される

    return textures;
}

// ===== 平行光源用の定数バッファ =====
struct DirectionalLightBuffer {
    ComPtr<ID3D12Resource> resource;
    DirectionalLight* data = nullptr;
};

inline DirectionalLightBuffer CreateDirectionalLightResource(ID3D12Device* device) {
    DirectionalLightBuffer light;
    light.resource = CreateBufferResource(device, sizeof(DirectionalLight));
    light.resource->Map(0, nullptr, reinterpret_cast<void**>(&light.data));
    light.data->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    light.data->direction = { 0.0f, -1.0f, 0.0f };
    light.data->intensity = 1.0f;
    return light;
}
// ===== 2D（図形と絵）用のメッシュとパイプライン =====

// 2Dの頂点。位置とUVだけ。法線も色も持たない
//（光は当てず、色は描くときにまとめて指定するため）
struct Draw2DVertex {
    Vector3 position;
    Vector2 texcoord;
};

struct Draw2DMesh {
    ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    ComPtr<ID3D12Resource> indexResource;
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT indexCount = 0;
};

inline Draw2DMesh CreateDraw2DMesh(ID3D12Device* device,
                                   const std::vector<Draw2DVertex>& vertices,
                                   const std::vector<uint32_t>& indices) {
    Draw2DMesh mesh;

    size_t vertexSizeInBytes = sizeof(Draw2DVertex) * vertices.size();
    mesh.vertexResource = CreateBufferResource(device, vertexSizeInBytes);
    Draw2DVertex* vertexData = nullptr;
    mesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::copy(vertices.begin(), vertices.end(), vertexData);
    mesh.vertexResource->Unmap(0, nullptr);
    mesh.vbv.BufferLocation = mesh.vertexResource->GetGPUVirtualAddress();
    mesh.vbv.SizeInBytes = (UINT)vertexSizeInBytes;
    mesh.vbv.StrideInBytes = sizeof(Draw2DVertex);

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

// 2Dの四角1枚（-0.5〜0.5。中心が原点なので、拡大縮小がそのまま大きさになる）。
// UVは左上が(0,0)。画像はそう並んでいるので、上下を入れ替えずに貼れる
inline Draw2DMesh CreateDraw2DQuadMesh(ID3D12Device* device) {
    std::vector<Draw2DVertex> vertices = {
        { { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f } }, // 左下
        { {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f } }, // 右下
        { { -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f } }, // 左上
        { {  0.5f,  0.5f, 0.0f }, { 1.0f, 0.0f } }, // 右上
    };
    std::vector<uint32_t> indices = { 0, 1, 2, 2, 1, 3 };
    return CreateDraw2DMesh(device, vertices, indices);
}

// 2Dの円1枚（半径0.5の円盤）。
//
// 板をピクセルシェーダで丸く抜く手もあるが、そのために2D用のシェーダーを
// もう1組増やすことになる。頂点で作れば、四角とまったく同じ道で描ける。
// 分割数32は、半径200pxの円でも辺1本ぶんのふくらみが1px以下に収まる
inline Draw2DMesh CreateDraw2DCircleMesh(ID3D12Device* device, int segments = 32) {
    if (segments < 3) { segments = 3; }

    std::vector<Draw2DVertex> vertices;
    vertices.reserve((size_t)segments + 1);
    vertices.push_back({ { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f } }); // 中心
    for (int i = 0; i < segments; ++i) {
        float angle = kTwoPi * (float)i / (float)segments;
        float x = cosf(angle) * 0.5f;
        float y = sinf(angle) * 0.5f;
        // UVも同じ形に置く。円に絵を貼りたくなったときに、そのまま丸く抜ける
        vertices.push_back({ { x, y, 0.0f }, { x + 0.5f, 0.5f - y } });
    }

    std::vector<uint32_t> indices;
    indices.reserve((size_t)segments * 3);
    for (int i = 0; i < segments; ++i) {
        indices.push_back(0);
        indices.push_back((uint32_t)(1 + i));
        indices.push_back((uint32_t)(1 + (i + 1) % segments));
    }
    return CreateDraw2DMesh(device, vertices, indices);
}

// 2D用のパイプライン。
//
// 図形と絵を同じもので描く。分けると、図形と絵を交互に積んだときに
// 重なり順が保てない（パイプラインを切り替えるたび、まとめて描き直すことになる）。
// 図形のときは白1x1のテクスチャを貼れば、指定した色がそのまま出る。
//
// 奥行きの判定は使わない。2Dの重なりは「描いた順」で決まる
inline LinePipeline CreateDraw2DPipeline(ID3D12Device* device, Object3DPipeline& sharedCompiler) {
    LinePipeline pipeline;

    // 0: WVP（頂点シェーダ） / 1: 掛ける色と切り取る範囲（ピクセルシェーダ） / 2: テクスチャ
    D3D12_DESCRIPTOR_RANGE srvRange[1] = {};
    srvRange[0].BaseShaderRegister = 0;
    srvRange[0].NumDescriptors = 1;
    srvRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[3] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].Constants.ShaderRegister = 1;
    rootParameters[1].Constants.Num32BitValues = 8; // 色(4)＋切り取る範囲(4)

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = srvRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(srvRange);

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
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&pipeline.rootSignature));
    assert(SUCCEEDED(hr));

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    ComPtr<IDxcBlob> vsBlob = CompileShader(L"Draw2D.VS.hlsl", L"vs_6_0", sharedCompiler.dxcUtils.Get(), sharedCompiler.dxcCompiler.Get(), sharedCompiler.includeHandler.Get());
    ComPtr<IDxcBlob> psBlob = CompileShader(L"Draw2D.PS.hlsl", L"ps_6_0", sharedCompiler.dxcUtils.Get(), sharedCompiler.dxcCompiler.Get(), sharedCompiler.includeHandler.Get());

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = pipeline.rootSignature.Get();
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // 半透明を重ねられるようにする。2Dは重ね方がそのまま絵作りになる
    D3D12_RENDER_TARGET_BLEND_DESC& blend = psoDesc.BlendState.RenderTarget[0];
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

    psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    // 奥行きは使わない。2Dの重なりは描いた順で決まる
    psoDesc.DepthStencilState.DepthEnable = false;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline.pso));
    assert(SUCCEEDED(hr));
    return pipeline;
}

#ifdef USE_IMGUI
// ImGuiに日本語フォントを読み込ませる（既定のフォントはASCIIしか持たず、日本語が「?」になる）。
// Meiryo→游ゴシック→MSゴシックの順で、実在するものを1つ使う
inline void LoadImGuiFont(ImGuiIO& io) {
    char windowsDir[MAX_PATH]{};
    if (GetWindowsDirectoryA(windowsDir, MAX_PATH) == 0) { strcpy_s(windowsDir, "C:\\Windows"); }

    const char* candidates[] = { "meiryo.ttc", "YuGothR.ttc", "msgothic.ttc" };
    for (const char* candidate : candidates) {
        std::string path = std::string(windowsDir) + "\\Fonts\\" + candidate;
        // 読めないファイルを渡すとassertで落ちるので、先に存在を確かめる
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) { continue; }
        if (io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese())) { return; }
    }
}

// ImGuiの初期化（DX12バックエンドはSRVヒープのスロット0をフォント用に使う）。
// rtvFormatは描き先の形式。食い違うとデバッグレイヤーが毎フレーム警告する
inline void InitializeImGui(HWND hwnd, GraphicsContext& gfx, DXGI_FORMAT rtvFormat) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    LoadImGuiFont(ImGui::GetIO());

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(gfx.device.Get(), 2, rtvFormat, gfx.srvDescriptorHeap.Get(),
        gfx.srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        gfx.srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
}

inline void ShutdownImGui() {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
#endif

// Object3D1つ分のWVP/World/UVTransformを計算し、マップ済みの定数バッファへ書き込む
// Spriteは平行投影（画面座標系そのまま）、それ以外（Sphere/objモデル）はカメラの透視投影を使う
inline void UpdateObjectMatrices(Object3D& obj, const Matrix4x4& cameraViewProjection, const Matrix4x4& spriteViewProjection) {
    Matrix4x4 worldMatrix = MakeAffineMatrix(obj.transform.scale, obj.transform.rotate, obj.transform.translate);
    const Matrix4x4& vp = obj.isSprite ? spriteViewProjection : cameraViewProjection;
    obj.transformData->World = worldMatrix;
    obj.transformData->WVP = Multiply(worldMatrix, vp);

    obj.materialData->uvTransform = MakeAffineMatrix(obj.uvTransform.scale, obj.uvTransform.rotate, obj.uvTransform.translate);
    obj.materialData->lightingMode = obj.lightingMode;
}

// Object3D1つを描画コマンドとして積む（頂点/インデックスバッファ・各種CBV・テクスチャSRVを設定してDrawIndexedInstanced）
inline void DrawObject(ID3D12GraphicsCommandList* commandList, const Object3D& obj, const DefaultTextures& textures) {
    commandList->IASetVertexBuffers(0, 1, &obj.vbv);
    commandList->IASetIndexBuffer(&obj.ibv);
    commandList->SetGraphicsRootConstantBufferView(0, obj.materialResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, obj.transformResource->GetGPUVirtualAddress());
    // 貼るものが無い物は白い1枚。uvChecker.pngはUVの並びを確かめるための格子模様なので、
    // ゲームに出す物に貼ったままだと、画面じゅうが虹色の格子と座標の数字で埋まる
    D3D12_GPU_DESCRIPTOR_HANDLE srv = obj.hasOwnTexture ? obj.textureSrvHandleGPU
        : (obj.untextured ? textures.whiteSrv : textures.uvCheckerSrv);
    commandList->SetGraphicsRootDescriptorTable(2, srv);
    commandList->DrawIndexedInstanced(obj.indexCount, 1, 0, 0, 0);
}

// ViewProjectionの逆行列でワールド空間の点へ戻す（マウスピッキングでレイを作るために使う）
// 他の変換と同じ「行ベクトル×行列」の規約（v * M）に合わせている
inline Vector3 UnprojectNDCToWorld(float ndcX, float ndcY, float ndcZ, const Matrix4x4& invViewProjection) {
    float x = ndcX, y = ndcY, z = ndcZ, w = 1.0f;
    float rx = x * invViewProjection.m[0][0] + y * invViewProjection.m[1][0] + z * invViewProjection.m[2][0] + w * invViewProjection.m[3][0];
    float ry = x * invViewProjection.m[0][1] + y * invViewProjection.m[1][1] + z * invViewProjection.m[2][1] + w * invViewProjection.m[3][1];
    float rz = x * invViewProjection.m[0][2] + y * invViewProjection.m[1][2] + z * invViewProjection.m[2][2] + w * invViewProjection.m[3][2];
    float rw = x * invViewProjection.m[0][3] + y * invViewProjection.m[1][3] + z * invViewProjection.m[2][3] + w * invViewProjection.m[3][3];
    if (fabsf(rw) < 1e-8f) { rw = 1e-8f; } // ゼロ割回避
    return { rx / rw, ry / rw, rz / rw };
}
