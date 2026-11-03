// chess-coach-cli profile (profiles.hpp).
#include "profiles.hpp"

#include <chesscoach/tier.hpp>

#include "engine_common.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <utility>

namespace chesscoach::cli {

Result<OpenProfile> open_profile(const std::string& data_dir, const std::string& name,
                                 const Clock& clock) {
    auto store = open_profiles(data_dir, clock);
    if (!store) {
        return tl::unexpected(store.error());
    }
    const auto all = store->profiles();
    const auto found = std::ranges::find(all, name, &Profile::name);
    if (found == all.end()) {
        return fail(fmt::format("no profile named \"{}\" in {}", name, data_dir));
    }
    return OpenProfile{.store = std::move(*store), .profile = *found};
}

Result<Color> parse_colour(const std::string& text) {
    if (text == "white") {
        return Color::white;
    }
    if (text == "black") {
        return Color::black;
    }
    return fail(fmt::format("colour \"{}\" is neither white nor black", text));
}

Result<void> run_profile(const ProfileOptions& options, std::ostream& out, const Clock& clock) {
    if (options.action == "export") {
        const auto exported = ProfileStore::export_profiles(options.data_dir, options.path);
        if (!exported) {
            if (exported.error() == ProfileError::unwritable) {
                return fail(fmt::format("{} exists or cannot be written", options.path));
            }
            if (exported.error() == ProfileError::not_profiles) {
                return fail(fmt::format("{} has no profiles.db to export", options.data_dir));
            }
            return fail(
                fmt::format("profiles.db in {} {}", options.data_dir, describe(exported.error())));
        }
        out << "exported: " << options.path << '\n';
        return {};
    }
    if (options.action == "import") {
        const auto imported = ProfileStore::import_profiles(options.data_dir, options.path, clock);
        if (!imported) {
            const ProfileError reason = imported.error();
            if (reason == ProfileError::unwritable || reason == ProfileError::storage) {
                return fail(fmt::format("profiles.db in {} cannot be replaced; nothing was changed",
                                        options.data_dir));
            }
            if (reason == ProfileError::busy) {
                return fail(fmt::format("{} or profiles.db in {} {}", options.path,
                                        options.data_dir, describe(reason)));
            }
            return fail(fmt::format("{} {}", options.path, describe(reason)));
        }
        out << "imported: " << options.path << '\n';
        return {};
    }
    auto store = open_profiles(options.data_dir, clock);
    if (!store) {
        return tl::unexpected(store.error());
    }
    if (options.action == "create") {
        const auto tier = tier_from_index(options.tier);
        if (!tier) {
            return fail(fmt::format("tier {} is not one of 0 to 6", options.tier));
        }
        const auto created = store->create_profile(options.name, *tier);
        if (!created) {
            return fail(fmt::format("profile \"{}\": profiles.db {}", options.name,
                                    describe(created.error())));
        }
        out << fmt::format("created: {} {} tier {}\n", created->id, created->name,
                           tier_index(created->tier));
        return {};
    }
    if (options.action == "remove") {
        const auto all = store->profiles();
        const auto found = std::ranges::find(all, options.name, &Profile::name);
        if (found == all.end()) {
            return fail(fmt::format("no profile named \"{}\"", options.name));
        }
        if (const auto removed = store->remove_profile(found->id); !removed) {
            return fail(fmt::format("profile \"{}\": profiles.db {}", options.name,
                                    describe(removed.error())));
        }
        out << "removed: " << options.name << '\n';
        return {};
    }
    for (const auto& profile : store->profiles()) {
        out << fmt::format("{} {} tier {} ({})\n", profile.id, profile.name,
                           tier_index(profile.tier), tier_label(profile.tier));
    }
    return {};
}

} // namespace chesscoach::cli
