/*
 * Copyright (C) 2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <string>
#include "glm/glm.hpp"
#include "glTF.h"

struct ActorCreateInfo {
	glm::vec3 position{};
	glm::vec3 rotation{};
	glm::vec3 scale{};
	vkglTF::Model* model{ nullptr };
	std::string tag{ "" };
	glm::vec3 constantVelocity;
};

class Actor {
private:
public:
	glm::vec3 position{};
	glm::vec3 rotation{};
	glm::vec3 scale{};
	vkglTF::Model* model{ nullptr };
	std::string tag{ "" };
	glm::vec3 constantVelocity{};
	Actor(ActorCreateInfo createInfo) : position(createInfo.position), rotation(createInfo.rotation), scale(createInfo.scale), model(createInfo.model), tag(createInfo.tag), constantVelocity(createInfo.constantVelocity) { };
	void rotate(const glm::vec3 delta);
	void move(const glm::vec3 dir, float deltaT);
	void update(float deltaTime);
	glm::mat4 getMatrix() const;
	float getRadius() const;
};

class ActorManager {
public:
	std::unordered_map<std::string, Actor*> actors;
	Actor* addActor(const std::string name, Actor* actor);
	~ActorManager();
};