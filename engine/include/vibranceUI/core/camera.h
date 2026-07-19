#pragma once
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

class Camera
{
    public:
    // Minimal free camera used by hosted 3D examples and polling helpers
    void update(float deltaSeconds);

    glm::mat4 get_view_matrix() const;

    glm::mat4 get_rotation_matrix() const;

    float pitch { 0.0f };
    float yaw { 0.0f };
    float moveSpeed { 2.5f };

    glm::vec3 velocity { 0.0f };
    glm::vec3 position { 0.0f, 0.0f, 3.0f };
};
