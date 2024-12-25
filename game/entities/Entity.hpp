/*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "glm/glm.hpp"

namespace Game {
	namespace Entities {
		class Entity {
		public:
			glm::vec2 position{};
			glm::vec2 direction{};
			float timer{ 0.0f };
			float health{ 100.0f };
			float speed{ 1.0f };
			float scale{ 1.0f };
			// @todo: archetype instead
			uint32_t imageIndex;
		};
	}
}