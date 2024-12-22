/*
* Copyright(C) 2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include "Monsters.hpp"
#include "json.hpp"

namespace ObjectTypes {

	void MonsterTypes::loadFromFile(const std::string jsonFileName)
	{
		std::ifstream stream(jsonFileName);
		assert(stream.good());
		nlohmann::json json = nlohmann::json::parse(stream);
		// @todo
		for (auto& set : json.items()) {
			MonsterTypeSet typeSet{};
			typeSet.name = set.key();

			for (auto& monster : set.value().items()) {
				nlohmann::json& jsonElement = monster.value();
				MonsterType type{};
				type.name = jsonElement["name"];
				type.image = jsonElement["image"];

				typeSet.types.push_back(type);

			}

			sets.push_back(typeSet);
		}
	}

}