/*
 * Copyright (C) 2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include <string>
#include <iostream>
#include <unordered_map>
#include <SFML/Audio.hpp>

// @todo: std::(de)queue?

#undef PlaySoundA

class AudioManager {
private:
	sf::Sound sound;
public:
	std::unordered_map<std::string, sf::SoundBuffer*> soundBuffers;
	void AddSoundFile(const std::string name, const std::string filename);
	// Named like this to avoud a WinApi macro (PlaySoundA)
	void PlaySnd(const std::string name);
};