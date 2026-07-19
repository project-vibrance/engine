#include <vibranceUI/core/camera.h>
#include <algorithm>

void Camera::update(float deltaSeconds)
{
    // Movement is applied in local camera space before pitch is clamped
    glm::mat4 cameraRotation = get_rotation_matrix();
    position += glm::vec3(cameraRotation * glm::vec4(velocity * moveSpeed * deltaSeconds, 0.0f));
    pitch = std::clamp(pitch, -1.5f, 1.5f);
}

glm::mat4 Camera::get_view_matrix() const
{
    // View matrix is the inverse of the camera transform
    glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 cameraRotation = get_rotation_matrix();

    return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::get_rotation_matrix() const
{
    glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3 { 1.0f, 0.0f, 0.0f });
    glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3 { 0.0f, -1.0f, 0.0f });

    return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}
