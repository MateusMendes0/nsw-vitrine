#pragma once

namespace vitrine {

#ifndef VITRINE_VERSION
#define VITRINE_VERSION "1.0.0"
#endif

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

constexpr int kCoverColumns = 5;
constexpr int kCoverRows = 1;

constexpr int kClassicColumns = 4;
constexpr int kClassicRows = 2;

constexpr const char* kAppVersion = VITRINE_VERSION;
constexpr const char* kAppAuthor = "Mateus Mendes";

}  // namespace vitrine
