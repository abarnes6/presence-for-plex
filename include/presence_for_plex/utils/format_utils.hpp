#pragma once

#include "presence_for_plex/core/models.hpp"
#include <string>

namespace presence_for_plex::utils {

/**
 * @brief Replace placeholders in a format string with values from MediaInfo
 *
 * Supported placeholders:
 * - Basic: {title}, {original_title}, {year}, {studio}, {summary}
 * - TV: {show}, {season}, {episode}, {se}, {SxE}, {season_padded}, {episode_padded}
 * - Music: {artist}, {album}, {track}
 * - Other: {username}, {genre}, {genres}, {rating}
 * - Playback: {progress}, {duration}, {progress_percentage}, {remaining}, {state}, {type}
 *
 * @param format The format string containing placeholders
 * @param media The MediaInfo object containing the data
 * @return The formatted string with placeholders replaced
 */
std::string replace_placeholders(const std::string& format, const core::MediaInfo& media);

/**
 * @brief Format duration in seconds to a human-readable string (MM:SS or HH:MM:SS)
 *
 * @param seconds Duration in seconds
 * @return Formatted duration string
 */
std::string format_duration(double seconds);

/**
 * @brief Format progress as a percentage string
 *
 * @param progress Current progress in seconds
 * @param duration Total duration in seconds
 * @return Formatted percentage string (e.g., "42%")
 */
std::string format_progress_percentage(double progress, double duration);

} // namespace presence_for_plex::utils
