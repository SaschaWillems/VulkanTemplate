/*
 * Copyright (C) 2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "ActorManager.h"

Actor* ActorManager::addActor(const std::string name, Actor* actor) {
	actors[name] = actor;
	return actor;
}

ActorManager::~ActorManager() {
	for (auto& it : actors) {
		delete it.second;
	}
}

void Actor::rotate(const glm::vec3 delta)
{
	this->rotation += delta;
}

void Actor::move(const glm::vec3 dir, float deltaT)
{
	glm::vec3 camFront;
	camFront.x = -cos(glm::radians(rotation.x)) * sin(glm::radians(rotation.y));
	camFront.y = sin(glm::radians(rotation.x));
	camFront.z = cos(glm::radians(rotation.x)) * cos(glm::radians(rotation.y));
	camFront = glm::normalize(camFront);

	const float moveSpeed = 0.005f;

	if (dir.z < 0.0f) {
		position += camFront * moveSpeed;
	}
	if (dir.z > 0.0f) {
		position -= camFront * moveSpeed;
	}
	//if (keys.left)
	//	position -= glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f))) * moveSpeed;
	//if (keys.right)
	//	position += glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f))) * moveSpeed;
}

glm::mat4 Actor::getMatrix() const
{
	const glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
	glm::mat4 r = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
	r = glm::rotate(r, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
	r = glm::rotate(r, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
	const glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
	return t * r * s;
}

float Actor::getRadius() const
{
	glm::vec3 size = (model->dimensions.max - model->dimensions.min) * scale * 1.1f;
	float maxsize = std::max(size.x, std::max(size.y, size.z));
	return maxsize / 2.0f;
}
