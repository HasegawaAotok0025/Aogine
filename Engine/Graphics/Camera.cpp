#include "Camera.h"

namespace {
constexpr float kPitchLimit = 1.5f; // 真上・真下の手前（約85度）で止める
}

void Camera::Initialize(float aspectRatio, InputEngine* input) {
    aspectRatio_ = aspectRatio;
    input_ = input;
    UpdateMatrices();
}

void Camera::Update(float deltaTime) {
    if (freeMove_ && input_ && input_->IsWindowActive()) {
        UpdateRotate();
        if (!moveOnlyWhileRightButton_ || input_->IsMouseDown(MouseButton::Right)) { UpdateMove(deltaTime); }
    } else {
        isDragging_ = false;
    }
    UpdateMatrices();
}

void Camera::LookAt(const Vector3& target) {
    Vector3 d{ target.x - transform_.translate.x, target.y - transform_.translate.y, target.z - transform_.translate.z };
    float horizontal = sqrtf(d.x * d.x + d.z * d.z);
    if (horizontal < 1e-6f && fabsf(d.y) < 1e-6f) { return; } // 同じ場所を指している
    transform_.rotate.y = atan2f(d.x, d.z);
    transform_.rotate.x = atan2f(-d.y, horizontal);
    transform_.rotate.z = 0.0f;
    UpdateMatrices();
}

Vector3 Camera::Forward() const {
    Matrix4x4 r = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, transform_.rotate, { 0.0f, 0.0f, 0.0f });
    return { r.m[2][0], r.m[2][1], r.m[2][2] };
}

Vector3 Camera::Right() const {
    Matrix4x4 r = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, transform_.rotate, { 0.0f, 0.0f, 0.0f });
    return { r.m[0][0], r.m[0][1], r.m[0][2] };
}

Vector3 Camera::Up() const {
    Matrix4x4 r = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, transform_.rotate, { 0.0f, 0.0f, 0.0f });
    return { r.m[1][0], r.m[1][1], r.m[1][2] };
}

void Camera::ClampPitch() {
    if (transform_.rotate.x > kPitchLimit) { transform_.rotate.x = kPitchLimit; }
    if (transform_.rotate.x < -kPitchLimit) { transform_.rotate.x = -kPitchLimit; }
}

// 右ドラッグで視点を回す
void Camera::UpdateRotate() {
    if (input_->IsMouseDown(MouseButton::Right)) {
        // 押した最初のフレームは、それ以前の移動量が乗らないよう回さない
        if (isDragging_) {
            POINT delta = input_->MouseDelta();
            transform_.rotate.y += float(delta.x) * rotateSpeed_;
            transform_.rotate.x += float(delta.y) * rotateSpeed_;
            ClampPitch();
        }
        isDragging_ = true;
    } else {
        isDragging_ = false;
    }
}

// WASD / E,Space / Q,Ctrl で動かす。矢印キーでも視点を回せる
void Camera::UpdateMove(float deltaTime) {
    Vector3 right = Right();
    Vector3 up = Up();
    Vector3 forward = Forward();

    bool shiftDown = input_->IsKeyDown(DIK_LSHIFT) || input_->IsKeyDown(DIK_RSHIFT);
    bool ctrlDown = input_->IsKeyDown(DIK_LCONTROL) || input_->IsKeyDown(DIK_RCONTROL);
    float velocity = moveSpeed_ * (shiftDown ? 3.0f : 1.0f) * deltaTime;

    if (input_->IsKeyDown(DIK_W)) { transform_.translate = AddScaled(transform_.translate, forward, velocity); }
    if (input_->IsKeyDown(DIK_S)) { transform_.translate = AddScaled(transform_.translate, forward, -velocity); }
    if (input_->IsKeyDown(DIK_D)) { transform_.translate = AddScaled(transform_.translate, right, velocity); }
    if (input_->IsKeyDown(DIK_A)) { transform_.translate = AddScaled(transform_.translate, right, -velocity); }
    if (input_->IsKeyDown(DIK_E) || input_->IsKeyDown(DIK_SPACE)) { transform_.translate = AddScaled(transform_.translate, up, velocity); }
    if (input_->IsKeyDown(DIK_Q) || ctrlDown) { transform_.translate = AddScaled(transform_.translate, up, -velocity); }

    float rotateVelocity = keyRotateSpeed_ * deltaTime;
    if (input_->IsKeyDown(DIK_LEFT))  { transform_.rotate.y -= rotateVelocity; }
    if (input_->IsKeyDown(DIK_RIGHT)) { transform_.rotate.y += rotateVelocity; }
    if (input_->IsKeyDown(DIK_UP))    { transform_.rotate.x -= rotateVelocity; }
    if (input_->IsKeyDown(DIK_DOWN))  { transform_.rotate.x += rotateVelocity; }
    ClampPitch();
}

// 位置と向きからView行列（逆行列）を、映し方からProjection行列を作る
void Camera::UpdateMatrices() {
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
    viewMatrix_ = Inverse4x4(worldMatrix);

    if (projection_ == CameraProjection::Orthographic) {
        // 縦を基準に、横は画面の縦横比で決める。カメラの位置が画面の中心に来る（y+が上）
        float halfHeight = orthoHeight_ * 0.5f;
        float halfWidth = halfHeight * aspectRatio_;
        projectionMatrix_ = MakeOrthographicMatrix(-halfWidth, halfHeight, halfWidth, -halfHeight, nearZ_, farZ_);
    } else {
        projectionMatrix_ = MakePerspectiveMatrix(fovY_, aspectRatio_, nearZ_, farZ_);
    }
}
