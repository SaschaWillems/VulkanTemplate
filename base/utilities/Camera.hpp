/*
* Basic camera class
*
* Copyright (C) 2016-2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
private:
	float fov;
	float znear, zfar;


	inline glm::mat4 rotationMatrix() {
		return glm::mat4_cast(rotation);
	}

	inline glm::mat4 translationMatrix() {
		return glm::translate(glm::mat4(1.0f), -position);
	}

	inline glm::mat4 viewMatrix() {
		return rotationMatrix() * translationMatrix();
	}

	void updateViewMatrix()
	{
		glm::mat4 currentMatrix = matrices.view;

		glm::mat4 rotM = glm::mat4(1.0f);
		glm::mat4 transM;

		//rotM = glm::rotate(rotM, glm::radians(rotation.x * (flipY ? -1.0f : 1.0f)), glm::vec3(1.0f, 0.0f, 0.0f));
		//rotM = glm::rotate(rotM, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		//rotM = glm::rotate(rotM, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		//rotM = glm::mat4_cast(glm::inverse(orientation));

		glm::vec3 translation = position;
		transM = glm::translate(glm::mat4(1.0f), translation);

		if (type == CameraType::firstperson)
		{
			matrices.view = rotationMatrix() * translationMatrix();
		}
		else
		{
			matrices.view = transM * rotM;
		}

		viewPos = glm::vec4(position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);
	};
public:
	struct Axis {
		const glm::vec3 positiveX = glm::vec3(1, 0, 0);
		const glm::vec3 negativeX = glm::vec3(-1, 0, 0);
		const glm::vec3 positiveY = glm::vec3(0, 1, 0);
		const glm::vec3 negativeY = glm::vec3(0, -1, 0);
		const glm::vec3 positiveZ = glm::vec3(0, 0, 1);
		const glm::vec3 negativeZ = glm::vec3(0, 0, -1);
	} axis;

	enum CameraType { lookat, firstperson };
	CameraType type = CameraType::lookat;

	//glm::vec3 rotation = glm::vec3();
	glm::vec3 position = glm::vec3();
	glm::vec4 viewPos = glm::vec4();
	glm::quat rotation;
	
	glm::uvec2 viewportSize;

	float rotationSpeed = 1.0f;
	float movementSpeed = 1.0f;
	bool physicsBased = true;

	glm::vec3 acceleration;
	glm::vec3 velocity;
	glm::vec3 torque;
	glm::vec3 angularAcceleration;
	glm::vec3 angularVelocity;

	bool flipY = false;

	struct
	{
		glm::mat4 perspective;
		glm::mat4 view;
	} matrices;

	struct
	{
		bool left = false;
		bool right = false;
		bool forward = false;
		bool backward = false;
		bool up = false;
		bool down = false;
		bool rollLeft = false;
		bool rollRight = false;
	} keys;

	struct Mouse {
		struct Buttons {
			bool left;
		} buttons;
		glm::vec2 cursorPos;
		bool dragging = false;
		glm::vec2 dragCursorPos;
	} mouse{};

	bool moving()
	{
		return keys.left || keys.right || keys.up || keys.down;
	}

	float getNearClip() {
		return znear;
	}

	float getFarClip() {
		return zfar;
	}

	void setPerspective(float fov, float aspect, float znear, float zfar)
	{
		glm::mat4 currentMatrix = matrices.perspective;
		this->fov = fov;
		this->znear = znear;
		this->zfar = zfar;
		matrices.perspective = glm::perspective(glm::radians(fov), aspect, znear, zfar);
		if (flipY) {
			matrices.perspective[1][1] *= -1.0f;
		}
	};

	void updateAspectRatio(float aspect)
	{
		glm::mat4 currentMatrix = matrices.perspective;
		matrices.perspective = glm::perspective(glm::radians(fov), aspect, znear, zfar);
		if (flipY) {
			matrices.perspective[1][1] *= -1.0f;
		}
	}

	void setPosition(glm::vec3 position)
	{
		this->position = position;
		updateViewMatrix();
	}

	void setRotation(glm::vec3 rotation)
	{
		this->rotation = rotation;
		updateViewMatrix();
	}

	void rotate(glm::vec3 delta)
	{
		//this->rotation += delta;
		updateViewMatrix();
	}

	void setTranslation(glm::vec3 translation)
	{
		this->position = translation;
		updateViewMatrix();
	};

	void translate(glm::vec3 delta)
	{
		this->position += delta;
		updateViewMatrix();
	}

	void setRotationSpeed(float rotationSpeed)
	{
		this->rotationSpeed = rotationSpeed;
	}

	void setMovementSpeed(float movementSpeed)
	{
		this->movementSpeed = movementSpeed;
	}

	glm::vec3 getForward() const { return axis.negativeZ * rotation; };
	glm::vec3 getBack()    const { return axis.positiveZ * rotation; };
	glm::vec3 getLeft()    const { return axis.negativeX * rotation; };
	glm::vec3 getRight()   const { return axis.positiveX * rotation; };
	glm::vec3 getDown()    const { return axis.negativeY * rotation; };
	glm::vec3 getUp()      const { return axis.positiveY * rotation; };

	void update(float deltaTime)
	{
		if (type == CameraType::firstperson)
		{
			const glm::vec3 camRight = getRight();
			const glm::vec3 camUp = getUp();
			const glm::vec3 camForward = getForward();

			//glm::vec3 camSide = glm::cross(camFront, camUp);

			float moveSpeed = deltaTime * movementSpeed * 2.5f;

			acceleration = angularAcceleration = glm::vec3(0.0f);

			if (physicsBased) {
				if (keys.forward) {
					acceleration = camForward * moveSpeed;
				}
				if (keys.backward) {
					acceleration = camForward * -moveSpeed;
				}
				if (keys.left) {
					acceleration = camRight * -moveSpeed;
				}
				if (keys.right) {
					acceleration = camRight * moveSpeed;
				}
				if (keys.up) {
					acceleration = camUp * -moveSpeed;
				}
				if (keys.down) {
					acceleration = camUp * moveSpeed;
				}

				float rollSpeed = rotationSpeed * 0.025f;
				if (keys.rollLeft) {
					angularAcceleration.z = -rollSpeed;
				}
				if (keys.rollRight) {
					angularAcceleration.z = rollSpeed;
				}

				//if (mouse.buttons.left) {
				if (mouse.dragging) {
					float rotateSpeed = rotationSpeed * 0.0025f;
					glm::vec2 delta = glm::normalize(mouse.cursorPos - mouse.dragCursorPos);
					if (abs(glm::length(delta)) > 0.1f) {
						angularAcceleration.x = -delta.y * rotateSpeed;
						angularAcceleration.y = delta.x * rotateSpeed;
					}
				}

				// Integrate
				velocity = velocity + acceleration * deltaTime;
				if (glm::length(acceleration) == 0.0f) {
					velocity -= velocity * 0.999f * deltaTime;
				}

				angularVelocity = angularVelocity + angularAcceleration * deltaTime;
				if (glm::length(angularAcceleration) == 0.0f) {
					angularVelocity -= angularVelocity * 0.999f * deltaTime;
				}

				position = position + velocity;
				rotation *= glm::angleAxis(angularVelocity.y, camUp);
				//rotation *= glm::angleAxis(angularVelocity.y, axis.positiveY);
				rotation *= glm::angleAxis(angularVelocity.x, camRight);
				rotation *= glm::angleAxis(angularVelocity.z, camForward);
			}
			else {
				if (keys.forward) {
					position += camForward * moveSpeed;
				}
				if (keys.backward) {
					position += camForward * -moveSpeed;
				}
				if (keys.left) {
					position += camRight * -moveSpeed;
				}
				if (keys.right) {
					position += camRight * moveSpeed;
				}
				if (keys.up) {
					position += camUp * -moveSpeed;
				}
				if (keys.down) {
					position += camUp * moveSpeed;
				}

				float rollSpeed = rotationSpeed * 0.01f;
				if (keys.rollLeft) {
					rotation *= glm::angleAxis(-rollSpeed, camForward);
				}
				if (keys.rollRight) {
					rotation *= glm::angleAxis(rollSpeed, camForward);
				}

				if (mouse.buttons.left) {
					float rotateSpeed = rotationSpeed * 0.01f;
					glm::vec2 delta = glm::normalize(mouse.cursorPos - mouse.dragCursorPos);
					if (abs(glm::length(delta)) > 0.1f) {
						rotation *= glm::angleAxis(delta.x * rotateSpeed, axis.positiveY);
						rotation *= glm::angleAxis(-delta.y * rotateSpeed, camRight);
					}
				}

			}
		}
		updateViewMatrix();
	};

	// Update camera passing separate axis data (gamepad)
	// Returns true if view or position has been changed
	bool updatePad(glm::vec2 axisLeft, glm::vec2 axisRight, float deltaTime)
	{
		bool retVal = false;

		if (type == CameraType::firstperson)
		{
			// Use the common console thumbstick layout		
			// Left = view, right = move

			const float deadZone = 0.0015f;
			const float range = 1.0f - deadZone;

			glm::vec3 camFront;
			camFront.x = -cos(glm::radians(rotation.x)) * sin(glm::radians(rotation.y));
			camFront.y = sin(glm::radians(rotation.x));
			camFront.z = cos(glm::radians(rotation.x)) * cos(glm::radians(rotation.y));
			camFront = glm::normalize(camFront);

			float moveSpeed = deltaTime * movementSpeed * 2.0f;
			float rotSpeed = deltaTime * rotationSpeed * 50.0f;

			// Move
			if (fabsf(axisLeft.y) > deadZone)
			{
				float pos = (fabsf(axisLeft.y) - deadZone) / range;
				position -= camFront * pos * ((axisLeft.y < 0.0f) ? -1.0f : 1.0f) * moveSpeed;
				retVal = true;
			}
			if (fabsf(axisLeft.x) > deadZone)
			{
				float pos = (fabsf(axisLeft.x) - deadZone) / range;
				position += glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f))) * pos * ((axisLeft.x < 0.0f) ? -1.0f : 1.0f) * moveSpeed;
				retVal = true;
			}

			// Rotate
			if (fabsf(axisRight.x) > deadZone)
			{
				float pos = (fabsf(axisRight.x) - deadZone) / range;
				rotation.y += pos * ((axisRight.x < 0.0f) ? -1.0f : 1.0f) * rotSpeed;
				retVal = true;
			}
			if (fabsf(axisRight.y) > deadZone)
			{
				float pos = (fabsf(axisRight.y) - deadZone) / range;
				rotation.x -= pos * ((axisRight.y < 0.0f) ? -1.0f : 1.0f) * rotSpeed;
				retVal = true;
			}
		}
		else
		{
			// todo: move code from example base class for look-at
		}

		if (retVal)
		{
			updateViewMatrix();
		}

		return retVal;
	}

};