#include "InputEngine.h"
#include <cassert>
#include <cmath>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "Xinput.lib")

void InputEngine::Initialize(HWND hwnd) {
    hwnd_ = hwnd;

    // DirectInputオブジェクトの生成
    HRESULT hr = DirectInput8Create(
        GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8,
        reinterpret_cast<void**>(directInput_.GetAddressOf()), nullptr);
    assert(SUCCEEDED(hr));

    // キーボードデバイスの生成
    hr = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, nullptr);
    assert(SUCCEEDED(hr));

    // 入力データ形式のセット（キーボード共通のフォーマットを使用）
    hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));

    // 排他制御レベルのセット（フォアグラウンド・非排他）
    //
    // DISCL_NOWINKEY は付けない。付けるとWindowsキーが丸ごと殺され、
    // スタートメニューも Win+Shift+S のスクリーンショットも効かなくなる。
    // 「ゲーム中に誤ってデスクトップへ戻らないように」という意図の指定だが、
    // このゲームは枠なしの窓なので、そもそも戻れて困るものではない。
    // 撮ることも、他の窓へ移ることもできない方がずっと不便
    hr = keyboard_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));

    keyboard_->Acquire();

    GetCursorPos(&mousePosition_);
    ScreenToClient(hwnd_, &mousePosition_);
    previousMousePosition_ = mousePosition_;
}

void InputEngine::Finalize() {
    if (keyboard_) { keyboard_->Unacquire(); }
    keyboard_.Reset();
    directInput_.Reset();
}

void InputEngine::Update() {
    // 差分を取るために、今の状態を「前フレームの状態」へ移してから読み直す
    memcpy(preKey_, key_, sizeof(key_));
    memcpy(preMouse_, mouse_, sizeof(mouse_));
    previousMousePosition_ = mousePosition_;

    windowActive_ = (hwnd_ != nullptr) && (GetForegroundWindow() == hwnd_);

    // --- キーボード ---
    memset(key_, 0, sizeof(key_));
    if (keyboard_) {
        // ウィンドウ切り替え等でデバイスを見失うことがあるので、毎フレーム再取得を試みておく
        keyboard_->Acquire();
        keyboard_->GetDeviceState(sizeof(key_), key_);
    }

    // --- マウス ---
    // GetAsyncKeyStateは他のアプリを触っている間の操作まで拾ってしまうので、
    // 自分のウィンドウが手前にあるときだけ有効な入力として扱う。
    // ここで一度だけ判断しておけば、呼ぶ側がその都度確かめる必要がなくなる
    if (windowActive_) {
        // マウスの左右は「主ボタン/副ボタン」の設定で入れ替えられることがある。
        // VK_LBUTTONは物理的な左ではなく主ボタンを指すので、左利き設定でも自然に動く
        mouse_[(int)MouseButton::Left] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        mouse_[(int)MouseButton::Right] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        mouse_[(int)MouseButton::Middle] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    } else {
        for (bool& button : mouse_) { button = false; }
    }

    if (hwnd_) {
        GetCursorPos(&mousePosition_);
        ScreenToClient(hwnd_, &mousePosition_);
    }
    mouseDelta_.x = mousePosition_.x - previousMousePosition_.x;
    mouseDelta_.y = mousePosition_.y - previousMousePosition_.y;

    UpdateGamepad();
}

// ゲームパッド（XInput）。ここもキーボードと同じく「前フレームの状態」をこのクラスが持ち、
// 押した瞬間／離した瞬間を呼ぶ側が組み立てなくて済むようにする
void InputEngine::UpdateGamepad() {
    prePadButtons_ = padButtons_;
    padButtons_ = 0;
    padLeftX_ = padLeftY_ = 0.0f;
    padRightX_ = padRightY_ = 0.0f;
    padLeftTrigger_ = padRightTrigger_ = 0.0f;
    padHasInput_ = false;

    // 繋がっていないパッドを毎フレーム探すと、そのたびに待たされることがある。
    // 見失っている間は間隔を空け、遊んでいる途中で繋いだ場合だけ拾えるようにする
    if (!padConnected_) {
        if (--padSearchCountdown_ > 0) { return; }
        padSearchCountdown_ = kPadSearchInterval;
    }

    XINPUT_STATE state{};
    if (XInputGetState(0, &state) != ERROR_SUCCESS) {
        padConnected_ = false;
        return;
    }
    padConnected_ = true;

    // 非アクティブなときは、キーボードやマウスと同じく「何も押されていない」にする。
    // XInputはウィンドウが手前かどうかを見ないので、ここで揃えないと
    // 裏に回している間も操作が届いてしまう
    if (!windowActive_) { return; }

    padButtons_ = state.Gamepad.wButtons;

    // スティックの遊び。中心付近の微小な傾きを切り捨てたうえで、
    // 残りを0〜1へ引き伸ばす（切り捨てただけだと、遊びの縁で値が飛ぶ）
    auto ApplyDeadZone = [](SHORT value, SHORT deadZone) -> float {
        if (value > deadZone) { return float(value - deadZone) / float(32767 - deadZone); }
        if (value < -deadZone) { return float(value + deadZone) / float(32768 - deadZone); }
        return 0.0f;
    };
    padLeftX_ = ApplyDeadZone(state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    padLeftY_ = ApplyDeadZone(state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    padRightX_ = ApplyDeadZone(state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    padRightY_ = ApplyDeadZone(state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    padLeftTrigger_ = float(state.Gamepad.bLeftTrigger) / 255.0f;
    padRightTrigger_ = float(state.Gamepad.bRightTrigger) / 255.0f;

    // 今このフレームでパッドが触られたか。画面の案内をキーとボタンで切り替えるのに使う
    padHasInput_ = (padButtons_ != 0) ||
                   (fabsf(padLeftX_) > 0.2f) || (fabsf(padLeftY_) > 0.2f) ||
                   (fabsf(padRightX_) > 0.2f) || (fabsf(padRightY_) > 0.2f) ||
                   (padLeftTrigger_ > 0.2f) || (padRightTrigger_ > 0.2f);
}
