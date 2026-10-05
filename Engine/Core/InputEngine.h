#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

// ============================================================================
// InputEngine：キーボードとマウスの入力をまとめて管理するクラス
//
// 毎フレームUpdate()を1回だけ呼ぶこと。そのフレームの入力状態がすべてここに揃う。
// キーの判定にはDIK_W, DIK_SPACEなどのDirectInputキーコード（<dinput.h>）を使う。
//
// ■ 「押した瞬間」を自前で作らないこと
//   IsKeyDownしか無かった頃は、「押した瞬間だけ反応させたい」処理のたびに
//   前フレームの状態を覚えるbool変数を各所で用意していた（jumpKeyWasDown_、
//   playToggleKeyWasDown_、mouseButtonWasDown_ …）。数えると3ファイルに30個以上あり、
//   入力を1つ足すたびにまた1つ増える状態だった。
//   前フレームの状態を持つのは本来この class の仕事なので、ここで一度だけ行う。
//
//     IsKeyDown      押されている間ずっとtrue（移動など、押しっぱなしで続く操作）
//     IsKeyTriggered 押した瞬間の1フレームだけtrue（ジャンプ・決定など、1回だけの操作）
//     IsKeyReleased  離した瞬間の1フレームだけtrue（引っ張って離す操作など）
//
// ■ マウスもここで扱う
//   以前はGetAsyncKeyState(VK_LBUTTON)が各所に直接書かれていた。
//   GetAsyncKeyStateは他のアプリを触っている間の操作も拾ってしまうため、
//   「ウィンドウが手前にあるか」の判定を毎回書き足す必要があり、書き漏らすと誤動作する。
//   ここで一度だけ確かめ、非アクティブなら全ボタンを「押されていない」として扱う
// ============================================================================

// マウスのボタン
enum class MouseButton { Left, Right, Middle, Count };

class InputEngine {
public:
    void Initialize(HWND hwnd);
    void Finalize();
    void Update(); // 毎フレーム呼び、最新のキー/マウス状態を取得する

    // ===== キーボード =====
    // 押されていればtrue（例：IsKeyDown(DIK_W)）
    bool IsKeyDown(BYTE dik) const { return (key_[dik] & 0x80) != 0; }
    // 押した瞬間の1フレームだけtrue（例：IsKeyTriggered(DIK_SPACE)）
    bool IsKeyTriggered(BYTE dik) const { return (key_[dik] & 0x80) != 0 && (preKey_[dik] & 0x80) == 0; }
    // 離した瞬間の1フレームだけtrue
    bool IsKeyReleased(BYTE dik) const { return (key_[dik] & 0x80) == 0 && (preKey_[dik] & 0x80) != 0; }

    // ===== マウス =====
    bool IsMouseDown(MouseButton button) const { return mouse_[(int)button]; }
    bool IsMouseTriggered(MouseButton button) const { return mouse_[(int)button] && !preMouse_[(int)button]; }
    bool IsMouseReleased(MouseButton button) const { return !mouse_[(int)button] && preMouse_[(int)button]; }

    // ウィンドウ左上を原点としたマウス位置[px]。クライアント領域の外にあることもある
    POINT MousePosition() const { return mousePosition_; }
    // このフレームでのマウスの移動量[px]
    POINT MouseDelta() const { return mouseDelta_; }

    // ===== ゲームパッド（XInput）=====
    // ボタンはXINPUT_GAMEPAD_A / _B / _X / _Y / _START / _DPAD_LEFT … をそのまま渡す。
    // キーボードと同じく「押されている／押した瞬間／離した瞬間」を用意してあるので、
    // 呼ぶ側が前フレームの状態を持つ必要はない
    bool IsPadConnected() const { return padConnected_; }
    bool IsPadDown(WORD button) const { return (padButtons_ & button) != 0; }
    bool IsPadTriggered(WORD button) const { return (padButtons_ & button) != 0 && (prePadButtons_ & button) == 0; }
    bool IsPadReleased(WORD button) const { return (padButtons_ & button) == 0 && (prePadButtons_ & button) != 0; }

    // 左スティックの傾き（-1〜1）。遊び（デッドゾーン）はここで取り除いてある。
    // Yは上が正（画面の上へ倒すと正）
    float PadLeftX() const { return padLeftX_; }
    float PadLeftY() const { return padLeftY_; }
    // 右スティック（カメラ用に用意してある）
    float PadRightX() const { return padRightX_; }
    float PadRightY() const { return padRightY_; }
    // トリガー（0〜1）
    float PadLeftTrigger() const { return padLeftTrigger_; }
    float PadRightTrigger() const { return padRightTrigger_; }

    // このフレームでパッドが触られたか。
    // 「今どちらの機器で遊んでいるか」を見分けて、画面に出す案内をキーとボタンで切り替えるのに使う。
    // 繋がっているかどうかで決めると、繋いだままマウスで遊んでいる人にボタンの絵を見せることになる
    bool PadHasInput() const { return padHasInput_; }

    // ゲームのウィンドウが手前にあるか。非アクティブなら入力はすべて「押されていない」になる
    bool IsWindowActive() const { return windowActive_; }

private:
    ComPtr<IDirectInput8> directInput_;
    ComPtr<IDirectInputDevice8> keyboard_;
    HWND hwnd_ = nullptr;

    BYTE key_[256] = {};
    BYTE preKey_[256] = {}; // 前フレームの状態。「押した瞬間／離した瞬間」はこれとの差から求まる

    bool mouse_[(int)MouseButton::Count] = {};
    bool preMouse_[(int)MouseButton::Count] = {};
    POINT mousePosition_{};
    POINT previousMousePosition_{};
    POINT mouseDelta_{};
    bool windowActive_ = false;

    void UpdateGamepad();

    bool padConnected_ = false;
    WORD padButtons_ = 0;
    WORD prePadButtons_ = 0;
    float padLeftX_ = 0.0f, padLeftY_ = 0.0f;
    float padRightX_ = 0.0f, padRightY_ = 0.0f;
    float padLeftTrigger_ = 0.0f, padRightTrigger_ = 0.0f;
    bool padHasInput_ = false;
    // 繋がっていないパッドを毎フレーム探しに行くと、そのたびに数ミリ秒待たされることがある。
    // 見つからなかったらしばらく間を空けて、遊んでいる途中で繋いだ場合だけ拾えるようにする
    int padSearchCountdown_ = 0;
    static constexpr int kPadSearchInterval = 120; // 約2秒に1回さがす
};
