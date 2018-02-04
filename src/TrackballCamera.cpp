#include "TrackballCamera.h"
#include <gtx/norm.hpp>
#include <gtc/constants.hpp>
#include <cmath>

const TrackballCamera::statePtr TrackballCamera::m_states[] = {
  statePtr{new CameraZoom},
  statePtr{new CameraRotate},
  statePtr{new CameraPassive}
};

void TrackballCamera::handleMouseClick(const QMouseEvent &io_action)
{
  auto mousePos = io_action.pos();
  setMousePos(mousePos.x(), mousePos.y());
  updateYawPitch();
  // Did a branchless version for fun using bool math
  auto type = io_action.type();
  auto button = io_action.buttons();
  using ME = QMouseEvent;
  int input = (type == ME::MouseButtonPress)
      * (
        ((button == Qt::LeftButton) * TRACKBALL_ROTATING)
        + ((button == Qt::RightButton) * TRACKBALL_ZOOMING)
        )
      + (type == ME::MouseButtonRelease) * TRACKBALL_PASSIVE;
  m_currentState = static_cast<CAM_STATE>(input);
}

void TrackballCamera::handleKey(const int _glfwKey, const bool _isPress)
{
  m_states[m_currentState]->handleMouseMove(this, _glfwKey, _isPress);
}

/**
 * @brief TrackballCamera::handleMouseMove
 * @param mouseX
 * @param mouseY
 * Pass the mouse move on to the relevent handler
 */
void TrackballCamera::handleMouseMove(const float _mouseX, const float _mouseY)
{
  m_states[m_currentState]->handleMouseMove(this, _mouseX, _mouseY);
}

/**
 * @brief TrackballCamera::handleMouseMove
 * @param mouseX current mouse coordinate X
 * @param mouseY current mouse coordinate Y
 * This is the easiest trackball available. The mouse x difference maps to yaw angle and the y maps to pitch angle.
 */
void TrackballCamera::mouseRotate(float _mouseX, float _mouseY)
{
  // Make sure the yaw is reset when we go past to -pi,pi
  m_yaw = m_lastYaw + (m_lastPos.x - _mouseX) * m_sensitivity;
  m_yaw = glm::mod(m_yaw, glm::pi<float>() * 2.0f);

  // Make sure our pitch is clamped within the range of slightly under half pi
  // We use this to prevent flipping when half pi is exceeded
  // A small epsilon is required to prevent locking at the poles
  static constexpr float epsilon = 0.01f;
  static constexpr float half_pi = glm::half_pi<float>() - epsilon;
  m_pitch = m_lastPitch + (m_lastPos.y - _mouseY)*m_sensitivity;
  m_pitch = glm::clamp(m_pitch, -half_pi, half_pi);
}

/**
 * @brief TrackballCamera::mouseZoom
 * @param mouseX
 * @param mouseY
 */
void TrackballCamera::mouseZoom(float, float _mouseY)
{
  m_zoom += (_mouseY - m_lastPos.y) * 0.1f * m_sensitivity;
  m_lastPos.y = _mouseY;
  m_zoom = glm::clamp(m_zoom, 0.0f, 10.0f);
}

void TrackballCamera::setTarget(const float _x, const float _y, const float _z) noexcept
{
  m_target.x = _x;
  m_target.y = _y;
  m_target.z = _z;
}

void TrackballCamera::setEye(const float _x, const float _y, float _z) noexcept
{
  m_eye.x = _x;
  m_eye.y = _y;
  m_eye.z = _z;
}

void TrackballCamera::setZoom(const float _zoom) noexcept
{
  m_zoom = _zoom;
}

void TrackballCamera::setSensitivity(const float sensitivity) noexcept
{
  m_sensitivity = sensitivity;
}

void TrackballCamera::updateYawPitch()
{
  m_lastYaw = m_yaw;
  m_lastPitch = m_pitch;
}

/**
 * @brief TrackballCamera::update
 * The yaw and pitch are the rotations about the y and x axes respectively, constructed using angle and axis.
 * Note that in glm the matrix is constructed using quaternions - I could leave them in this form for the rotation.
 * In this case, the eye is rotated around the target
 */
void TrackballCamera::update()
{
  // Call base class to update perspective
  Camera::update();

  // Now use lookat function to set the view matrix (assume y is up)
  glm::mat3 r_yaw = glm::mat3_cast(glm::angleAxis(m_yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
  glm::mat3 r_pitch = glm::mat3_cast(glm::angleAxis(m_pitch, glm::vec3(1.0f, 0.0f, 0.0f)));
  glm::vec3 eye = m_target - (r_yaw * r_pitch * m_zoom *  (m_target - m_eye));
  m_viewMatrix = glm::lookAt(eye, m_target, glm::vec3(0.0f,1.0f,0.0f));
}