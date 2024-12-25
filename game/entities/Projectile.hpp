/*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "Entity.hpp"

namespace Game {
	namespace Entities {
		class Projectile : public Entity {
		public:
			float damage;
			// @todo: better name
			float life;
		};
	}
}