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
};

class Actor {
private:
public:
	glm::vec3 position{};
	glm::vec3 rotation{};
	glm::vec3 scale{};
	vkglTF::Model* model{ nullptr };
	std::string tag{ "" };
	Actor(ActorCreateInfo createInfo) : position(createInfo.position), rotation(createInfo.rotation), scale(createInfo.scale), model(createInfo.model), tag(createInfo.tag) { };
	void rotate(const glm::vec3 delta);
	void move(const glm::vec3 dir, float deltaT);
	glm::mat4 getMatrix() const;
	float getRadius() const;
};

class ActorManager {
public:
	std::unordered_map<std::string, Actor*> actors;
	Actor* addActor(const std::string name, Actor* actor);
	~ActorManager();
};