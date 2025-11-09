#include "presence_for_plex/utils/format_utils.hpp"
#include <sstream>
#include <iomanip>
#include <numeric>

namespace presence_for_plex::utils {

std::string format_duration(double seconds) {
    int total_seconds = static_cast<int>(seconds);
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int secs = total_seconds % 60;

    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << ":" << std::setfill('0') << std::setw(2) << minutes
            << ":" << std::setw(2) << secs;
    } else {
        oss << minutes << ":" << std::setfill('0') << std::setw(2) << secs;
    }
    return oss.str();
}

std::string format_progress_percentage(double progress, double duration) {
    if (duration <= 0) return "0%";
    double percentage = (progress / duration) * 100.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << percentage << "%";
    return oss.str();
}

std::string replace_placeholders(const std::string& format, const core::MediaInfo& media) {
    std::string result = format;

    auto replace = [&result](const std::string& placeholder, const std::string& value) {
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    };

    // Basic media information
    replace("{title}", media.title);
    replace("{original_title}", media.original_title);
    replace("{year}", media.year > 0 ? std::to_string(media.year) : "");
    replace("{studio}", media.studio);
    replace("{summary}", media.summary);

    // TV show specific
    replace("{show}", media.grandparent_title);
    replace("{show_title}", media.grandparent_title);
    replace("{season}", media.season > 0 ? std::to_string(media.season) : "");
    replace("{episode}", media.episode > 0 ? std::to_string(media.episode) : "");
    replace("{season_padded}", media.season > 0 ? (media.season < 10 ? "0" + std::to_string(media.season) : std::to_string(media.season)) : "");
    replace("{episode_padded}", media.episode > 0 ? (media.episode < 10 ? "0" + std::to_string(media.episode) : std::to_string(media.episode)) : "");

    // Music specific
    replace("{artist}", media.artist);
    replace("{album}", media.album);
    replace("{track}", media.track > 0 ? std::to_string(media.track) : "");

    // Playback state
    std::string state_str;
    switch (media.state) {
        case core::PlaybackState::Playing: state_str = "Playing"; break;
        case core::PlaybackState::Paused: state_str = "Paused"; break;
        case core::PlaybackState::Buffering: state_str = "Buffering"; break;
        case core::PlaybackState::Stopped: state_str = "Stopped"; break;
        default: state_str = "Unknown"; break;
    }
    replace("{state}", state_str);

    // Media type
    std::string type_str;
    switch (media.type) {
        case core::MediaType::Movie: type_str = "Movie"; break;
        case core::MediaType::TVShow: type_str = "TV Show"; break;
        case core::MediaType::Music: type_str = "Music"; break;
        default: type_str = "Media"; break;
    }
    replace("{type}", type_str);

    // Playback progress
    replace("{progress}", format_duration(media.progress));
    replace("{duration}", format_duration(media.duration));
    replace("{progress_percentage}", format_progress_percentage(media.progress, media.duration));
    replace("{remaining}", format_duration(media.duration - media.progress));

    // User information
    replace("{username}", media.username);

    // Genres
    if (!media.genres.empty()) {
        std::string genres_str = std::accumulate(
            std::next(media.genres.begin()),
            media.genres.end(),
            media.genres[0],
            [](const std::string& a, const std::string& b) {
                return a + ", " + b;
            }
        );
        replace("{genres}", genres_str);
        replace("{genre}", media.genres[0]);
    } else {
        replace("{genres}", "");
        replace("{genre}", "");
    }

    // Rating
    if (media.rating > 0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << media.rating;
        replace("{rating}", oss.str());
    } else {
        replace("{rating}", "");
    }

    // Season/Episode formatting helpers
    if (media.season > 0 && media.episode > 0) {
        std::ostringstream se_format;
        se_format << "S" << media.season << " • E" << media.episode;
        replace("{se}", se_format.str());

        std::ostringstream sxe_format;
        sxe_format << "S" << std::setfill('0') << std::setw(2) << media.season
                   << "E" << std::setw(2) << media.episode;
        replace("{SxE}", sxe_format.str());
    } else {
        replace("{se}", "");
        replace("{SxE}", "");
    }

    return result;
}

} // namespace presence_for_plex::utils
