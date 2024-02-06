// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef IPTSD_COMMON_BUILDOPTS_HPP
#define IPTSD_COMMON_BUILDOPTS_HPP

#include <string_view>

namespace iptsd::common::buildopts {

/*
 * This header is automatically generated by meson.
 *
 * Because the include is quite "magic" - as in, the file doesn't normally exist - we
 * only allow including it here, and wrap it's contents in properly typed C++.
 */
#include <configure.h>

/*!
 * The main iptsd config file.
 */
constexpr std::string_view ConfigFile = IPTSD_CONFIG_FILE;

/*!
 * The directory from where iptsd loads drop-in config snippets.
 * These snippets get overlayed over @ref ConfigFile.
 */
constexpr std::string_view ConfigDir = IPTSD_CONFIG_DIR;

/*!
 * The directory from where device specific configs are loaded.
 * These configs can be overriden by @ref ConfigFile and @ref ConfigDir.
 */
constexpr std::string_view PresetDir = IPTSD_PRESET_DIR;

/*
 * Make sure that nothing uses the defines directly.
 */
#undef IPTSD_CONFIG_DIR
#undef IPTSD_CONFIG_FILE
#undef IPTSD_PRESET_DIR

} // namespace iptsd::common::buildopts

#endif // IPTSD_COMMON_BUILDOPTS_HPP
