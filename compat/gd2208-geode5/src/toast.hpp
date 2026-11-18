#pragma once
// NexLevel notifications: small cards in the top-right corner with a violet outline and a soft
// sound. They stack (newest on top), stay a few seconds and survive scene changes.
#include <Geode/Geode.hpp>

#include <string>

namespace nexlevel::mod {

enum class ToastKind { Info, Success, Warning, Error, Chat };

// Safe from any thread and during scene changes (shown on the next frame).
void toast(const std::string& title, const std::string& text = "", ToastKind kind = ToastKind::Info);

} // namespace nexlevel::mod
