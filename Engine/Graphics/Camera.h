#pragma once
#include <Windows.h>
#include "../Core/MathUtility.h"
#include "../Core/InputEngine.h"

// カメラの映し方。
//   Perspective  … 遠くの物が小さくなる。3Dのゲーム向け
//   Orthographic … 遠近が付かない。2Dのゲーム向け（縦に何メートル映すかで決める）
enum class CameraProjection { Perspective, Orthographic };

// ================================================================
// 3Dのカメラ。位置と向きはゲームのコードが決める。
//
//   Camera& cam = engine.GetCamera();
//   cam.SetPosition({ 0, 5, -10 });
//   cam.LookAt({ 0, 0, 0 });
//
// 向き（rotate）はオイラー角[rad]。x＝上下（正で下を向く）、y＝左右（正で右を向く）。
//
// デバッグ用に、キーとマウスで飛び回る「自由移動」を付けられる（既定はOFF）。
//   cam.SetFreeMove(true);
//   右ドラッグ/矢印: 視点  WASD: 移動  E,Space: 上  Q,Ctrl: 下  Shift: 速く
// ================================================================
class Camera {
public:
    // Engineが呼ぶ
    void Initialize(float aspectRatio, InputEngine* input);
    // Engineが毎フレーム呼ぶ。自由移動がONなら入力を反映し、行列を作り直す
    void Update(float deltaTime);

    // ----- 位置と向き -----
    Transform& GetTransform() { return transform_; }
    const Transform& GetTransform() const { return transform_; }
    void SetPosition(const Vector3& position) { transform_.translate = position; UpdateMatrices(); }
    void SetRotation(const Vector3& rotationEuler) { transform_.rotate = rotationEuler; UpdateMatrices(); }
    const Vector3& GetPosition() const { return transform_.translate; }
    const Vector3& GetRotation() const { return transform_.rotate; }

    // targetの方を向く（位置はそのまま）
    void LookAt(const Vector3& target);

    // カメラの前・右・上の向き（ワールド座標、長さ1）
    Vector3 Forward() const;
    Vector3 Right() const;
    Vector3 Up() const;

    // transformを直接書き換えたあと、同じフレームの描画に間に合わせたいときに呼ぶ
    //（Engineは毎フレーム、ゲームの更新の後に自動で作り直すので、普通は要らない）
    void SyncMatrices() { UpdateMatrices(); }

    // ----- 映し方 -----
    float GetFovY() const { return fovY_; }                     // 画角[rad]（縦）
    void SetFovY(float fovY) { fovY_ = fovY; UpdateMatrices(); }
    void SetNearFar(float nearZ, float farZ) { nearZ_ = nearZ; farZ_ = farZ; UpdateMatrices(); }
    float GetNearZ() const { return nearZ_; }
    float GetFarZ() const { return farZ_; }

    CameraProjection GetProjection() const { return projection_; }
    void SetProjection(CameraProjection projection) { projection_ = projection; UpdateMatrices(); }
    float GetOrthoHeight() const { return orthoHeight_; }       // 平行投影で縦に映す長さ[m]
    void SetOrthoHeight(float height) {
        orthoHeight_ = (height < 0.01f) ? 0.01f : height;
        UpdateMatrices();
    }
    void SetAspectRatio(float aspectRatio) { aspectRatio_ = aspectRatio; UpdateMatrices(); }

    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
    Matrix4x4 GetViewProjectionMatrix() const { return Multiply(viewMatrix_, projectionMatrix_); }

    // ----- デバッグ用の自由移動 -----
    // onlyWhileRightButton=true にすると、右ボタンを押している間だけWASDで動く（UnityのSceneビューと同じ）。
    // 押していない間のキーは、他の操作（W/E/Rでギズモの切り替えなど）に使える
    void SetFreeMove(bool enabled, bool onlyWhileRightButton = false) {
        freeMove_ = enabled;
        moveOnlyWhileRightButton_ = onlyWhileRightButton;
    }
    bool IsFreeMove() const { return freeMove_; }
    void SetFreeMoveSpeed(float unitsPerSecond) { moveSpeed_ = unitsPerSecond; }

private:
    void UpdateRotate();
    void UpdateMove(float deltaTime);
    void ClampPitch();
    void UpdateMatrices();

    InputEngine* input_ = nullptr;
    bool freeMove_ = false;
    bool moveOnlyWhileRightButton_ = false;

    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f} };

    float moveSpeed_ = 5.0f;      // 1秒あたりの移動量
    float rotateSpeed_ = 0.005f;  // マウス1pxあたりの回転量[rad]
    float keyRotateSpeed_ = 1.5f; // 矢印キーでの回転速度[rad/s]

    CameraProjection projection_ = CameraProjection::Perspective;
    float orthoHeight_ = 10.0f;
    float fovY_ = 0.45f;
    float aspectRatio_ = 16.0f / 9.0f;
    float nearZ_ = 0.1f;
    float farZ_ = 100.0f;

    Matrix4x4 viewMatrix_ = MakeIdentity4x4();
    Matrix4x4 projectionMatrix_ = MakeIdentity4x4();

    bool isDragging_ = false; // 右ボタンを押した次のフレームからtrue（押した瞬間に視点が飛ばないように）
};
