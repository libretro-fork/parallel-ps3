// RPCS3's game patches as core options, the way the cemu core lists a title's
// graphic packs: once the game is known, the patches for its serial become
// options of their own in a last category, Patch Manager, which is not there
// for a game without any (NNshi).
//
// - Every patch is an Enabled/Disabled switch with its own notes, off unless
//   turned on, as standalone lists them.
// - Patches that share a Group are switches too, of which one at most is on,
//   as RPCS3 applies one patch of a group at most: turning one on turns the
//   others off (NNshi: standalone does not merge them into one choice).
// - A patch's configurable values with a list of allowed values are options
//   too, shown while the patch is on. Ranges keep the patch's default.
// - "All titles" patches (serial "All", hashed SPU programs) are listed only
//   for the games they are known to affect (s_multigame_patches); listed for
//   every game they made the category appear everywhere. Turned on, they are
//   on for every game, as in standalone - RPCS3 applies them only where the
//   SPU program's hash matches anyway.
//
// The patches come from patch.yml (the one the core carries), from
// imported_patch.yml (what was imported in standalone RPCS3, copied over) and
// from the game's own <serial>_patch.yml, the three RPCS3 loads itself. Which
// are on is the .opt file's business: before each boot patch_config.yml is
// written from the options, and RPCS3 applies a patch only where the hash of
// the executable matches, so the version of the game needs no matching here.

#include "stdafx.h"
#include "libretro_patch_manager.h"

#include "Utilities/bin_patch.h"
#include "Utilities/File.h"

#include <deque>
#include <map>
#include <set>

LOG_CHANNEL(lr_patch_log, "libretro patches");

namespace
{
    struct config_value_option
    {
        std::string key;
        std::string name; // the configurable value's name in the patch
        std::vector<std::pair<std::string /*label*/, f64>> values;
    };

    struct patch_option
    {
        std::string key;
        std::string group;                     // empty: none; else one of a group is on at most
        std::vector<std::string> descriptions; // the patch (one)
        bool all_titles = false;               // an "All titles" patch, configured for all
        std::map<std::string /*description*/, std::vector<config_value_option>> config_values;
    };

// The "All titles" patches of patch.yml (hashed SPU programs) that name the
// games they are known to affect only in their notes: those games' serials,
// so the patch is listed for them. Demos and betas are left out. Made from
// the notes with RPCS3's compatibility list (NNshi did the first).
static const std::map<std::string_view, std::vector<std::string_view>> s_multigame_patches = {
    {"Disable SPU MLAA - Prince of Persia series", {"NPEB00341", "NPEB00389", "NPEB00390", "NPUB30303", "NPUB30387", "NPUB30388"}},
    {"Disable SPU MLAA - Multiple titles (01)", {"BCAS20233", "BCAS20281", "BCES01503", "BCES01740", "BCUS98282", "BCUS98377", "BLES01211", "BLES01222", "BLES01250", "BLES01414", "BLJM60352", "BLJM60489", "BLJM61149", "BLUS30732", "BLUS30736", "BLUS30842", "NPEA00275", "NPEA00288", "NPEA00385", "NPEA00386", "NPEA00387", "NPEA00438", "NPEB00645", "NPEB00821", "NPEB00915", "NPEB01363", "NPEB02228", "NPHA80188", "NPJA00075", "NPJB00466", "NPJB00532", "NPUA70218", "NPUA80275", "NPUA80523", "NPUA80643", "NPUA80644", "NPUA80645", "NPUB30745", "NPUB31077", "NPUB31345", "NPUB31658"}},
    {"Disable SPU MLAA - Multiple titles (02)", {"BLES01066", "BLES01167", "BLES01169", "BLES01399", "BLJM60316", "BLUS30580", "BLUS30642", "BLUS30709", "BLUS30826", "NPEB00618", "NPEB02034", "NPUB30539", "NPUB31509"}},
    {"Disable SPU MLAA - Multiple titles (03)", {"BCAS20254", "BCES01097", "BCES01115", "BCES01279", "BCES01598", "BCUS98259", "BCUS98278", "BCUS98279", "NPEA00280", "NPUA80677"}},
    {"Disable SPU MLAA - Multiple titles (04)", {"BLAS50796", "BLES02178", "BLES02243", "BLJM61249", "BLJM61313", "BLUS31599", "NPEB02281", "NPUB31838", "NPUB31857"}},
    {"Disable SPU MLAA - WWE '13, WWE 2K14", {"BLES01699", "BLES01937", "BLUS31015", "BLUS31277", "NPEB01815"}},
    {"Disable SPU MLAA - ICO", {"BCES01097", "BCES01115", "BCUS98259", "NPEA00279", "NPUA80676"}},
    {"Disable SPU MLAA - Macross 30", {"BLJS10184"}},
    {"Disable SPU MLAA - Metro Last Light", {"BLES01867", "BLJS10218", "BLUS31184", "NPEB01369", "NPUB31219"}},
    {"Disable SPU MLAA - God of War: Ascension (01)", {"BCES01741", "BCES01742", "BCUS98232", "NPEA00445", "NPHA80258", "NPUA80918"}},
    {"Disable SPU MLAA - God of War: Ascension (02)", {"BCES01741", "BCES01742", "BCUS98232", "NPEA00445", "NPHA80258", "NPUA80918"}},
    {"Disable SPU MLAA - God of War: Ascension (03)", {"BCES01741", "BCES01742", "BCUS98232", "NPEA00445", "NPHA80258", "NPUA80918"}},
    {"Disable SPU MLAA - Twisted Metal, Resident Evil: Operation Raccoon City", {"BCES01010", "BCES01400", "BCUS98106", "BLES01288", "BLJM60342", "BLUS30750", "NPEA00362", "NPEB00985", "NPUA80079", "NPUB30767"}},
    {"Disable SPU MLAA - Multiple titles (05)", {"BCES00484", "BCES00956", "BCES01104", "BCES01284", "BCES01285", "BCES01369", "BCUS98242", "BCUS98247", "BCUS98248", "BCUS98271", "BLES00991", "BLES01392", "BLES01393", "BLES01698", "BLES01796", "BLES02026", "BLJM60502", "BLJS10127", "BLUS30585", "BLUS30613", "BLUS30723", "BLUS31433", "NPEA00303", "NPEA00304", "NPEA00315", "NPEA00333", "NPEA00429", "NPEA00505", "NPEB00435", "NPEB00476", "NPEB01020", "NPEB01332", "NPJA00077", "NPUA80619", "NPUA80661", "NPUA80678", "NPUA80696", "NPUA80875", "NPUA80930", "NPUB30383", "NPUB30830", "NPUB31072", "NPUB31496"}},
    {"Disable SPU MLAA - LittleBigPlanet 2 (01)", {"BCAS20113", "BCES00850", "BCES01086", "BCES01693", "BCES01694", "BCUS98245", "BCUS98372", "NPEA00324", "NPEA00437", "NPUA80662"}},
    {"Disable SPU MLAA - LittleBigPlanet 2 (02)", {"BCAS20113", "BCES00850", "BCES01086", "BCES01693", "BCES01694", "BCUS98245", "BCUS98372", "NPEA00324", "NPEA00437", "NPUA80662"}},
    {"Disable SPU MLAA - LittleBigPlanet 2, LittleBigPlanet 3, LittleBigPlanet Hub", {"BCAS20113", "BCES00850", "BCES01086", "BCES01663", "BCES01693", "BCES01694", "BCES02068", "BCUS98245", "BCUS98362", "BCUS98372", "NPEA00324", "NPEA00437", "NPEA00515", "NPUA80662", "NPUA81116"}},
};

    std::string s_serial;
    std::map<std::string, std::string> s_last_values; // what a group's switches were, to see which one was turned on
    patch_engine::patch_map s_map;
    std::vector<patch_option> s_options;
    std::vector<retro_core_option_v2_definition> s_definitions;
    std::deque<std::string> s_strings;

    const char* keep(std::string value)
    {
        s_strings.push_back(std::move(value));
        return s_strings.back().c_str();
    }

    // Short and stable: the same patch of the same game gets the same key in
    // every build, so a choice in the .opt file stays with it.
    std::string key_hash(std::string_view text)
    {
        u32 hash = 0x811c9dc5;
        for (char c : text)
        {
            hash ^= static_cast<u8>(c);
            hash *= 0x01000193;
        }
        return fmt::format("%08x", hash);
    }

    bool for_serial(const patch_engine::patch_info& info, const std::string& serial)
    {
        for (const auto& [title, serials] : info.titles)
        {
            if (serials.contains(serial))
                return true;
        }
        return false;
    }

    // An "All titles" patch whose notes name this game
    bool all_titles_for_serial(const patch_engine::patch_info& info, const std::string& description, const std::string& serial)
    {
        const auto titles = info.titles.find(patch_key::all);
        if (titles == info.titles.end() || !titles->second.contains(patch_key::all))
            return false;
        const auto games = s_multigame_patches.find(description);
        return games != s_multigame_patches.end() &&
            std::find(games->second.begin(), games->second.end(), serial) != games->second.end();
    }

    // The notes' first paragraph, as the sublabel
    std::string first_paragraph(const std::string& notes)
    {
        std::string text = notes.substr(0, notes.find("\n\n"));
        for (char& c : text)
        {
            if (c == '\n' || c == '\r')
                c = ' ';
        }
        return text;
    }

    std::string value_text(f64 value)
    {
        return fmt::format("%g", value);
    }
}

namespace libretro_patches
{
    void collect(const std::string& serial)
    {
        s_serial = serial;
        s_map.clear();
        s_options.clear();
        s_definitions.clear();
        s_strings.clear();

        if (serial.empty())
            return;

        const std::string dir = patch_engine::get_patches_path();
        for (const std::string& path : {dir + "patch.yml", patch_engine::get_imported_patch_path(), dir + serial + "_patch.yml"})
        {
            if (fs::is_file(path))
                patch_engine::load(s_map, path);
        }

        // One option per description, whichever executables (hashes) it is
        // for, as standalone lists them
        std::map<std::string, patch_option> singles;
        std::map<std::string, const patch_engine::patch_info*> first_info;

        for (const auto& [hash, container] : s_map)
        {
            for (const auto& [description, info] : container.patch_info_map)
            {
                const bool all_titles = !for_serial(info, serial) && all_titles_for_serial(info, description, serial);
                if (!all_titles && !for_serial(info, serial))
                    continue;

                first_info.try_emplace(description, &info);

                patch_option& option = singles[description];
                option.group = info.patch_group;
                option.all_titles = option.all_titles || all_titles;
                if (option.descriptions.empty())
                    option.descriptions.push_back(description);
            }
        }

        for (auto& [name, option] : singles)
        {
            option.key = "rpcs3_zpatch_" + key_hash(serial + '\x1f' + "p" + name);

            for (const std::string& description : option.descriptions)
            {
                const patch_engine::patch_info& info = *first_info.at(description);
                for (const auto& [value_name, value] : info.default_config_values)
                {
                    if (value.allowed_values.empty())
                        continue; // a range: the patch's default

                    config_value_option config{};
                    config.key = option.key + "_" + key_hash(description + '\x1f' + value_name);
                    config.name = value_name;
                    for (const auto& allowed : value.allowed_values)
                        config.values.emplace_back(allowed.label.empty() ? value_text(allowed.value) : allowed.label, allowed.value);
                    option.config_values[description].push_back(std::move(config));
                }
            }
            s_options.push_back(std::move(option));
        }
        s_last_values.clear();

        // The definitions, in the order the options were made
        for (const patch_option& option : s_options)
        {
            // Made in place: a definition is 2 KiB with its 128 value slots,
            // and two of them on the stack put this past the core's 4 KiB
            // stack frame
            retro_core_option_v2_definition& def = s_definitions.emplace_back();
            def.key = keep(option.key);
            def.category_key = "patches";

            const patch_engine::patch_info& first = *first_info.at(option.descriptions.front());
            std::string info = first_paragraph(first.notes);
            if (!first.author.empty())
                info += (info.empty() ? "" : " ") + fmt::format("(%s)", first.author);
            if (!option.group.empty())
                info += (info.empty() ? "" : " ") + fmt::format("One patch of the group %s at most is on.", option.group);
            if (option.all_titles)
                info += (info.empty() ? "" : " ") + std::string("For several games: on, it is on for all of them.");
            def.desc = keep(option.descriptions.front());
            def.info = keep(info.empty() ? "Applies at the next start of the game." : info + " Applies at the next start of the game.");
            def.values[0] = {"disabled", nullptr};
            def.values[1] = {"enabled", nullptr};
            def.default_value = "disabled";

            for (const auto& [description, configs] : option.config_values)
            {
                const patch_engine::patch_info& info = *first_info.at(description);
                for (const config_value_option& config : configs)
                {
                    retro_core_option_v2_definition& value_def = s_definitions.emplace_back();
                    value_def.key = keep(config.key);
                    value_def.category_key = "patches";
                    value_def.desc = keep(fmt::format("%s: %s", description, config.name));
                    value_def.info = keep(fmt::format("A value of the patch \"%s\".", description));

                    const f64 default_value = ::at32(info.default_config_values, config.name).value;
                    usz index = 0;
                    for (const auto& [label, value] : config.values)
                    {
                        if (index + 1 >= RETRO_NUM_CORE_OPTION_VALUES_MAX)
                            break;
                        value_def.values[index].value = keep(label);
                        if (value == default_value || !value_def.default_value)
                            value_def.default_value = value_def.values[index].value;
                        index++;
                    }
                }
            }
        }

        lr_patch_log.notice("%u patch options for %s", s_options.size(), serial);
    }

    bool any()
    {
        return !s_options.empty();
    }

    const std::vector<retro_core_option_v2_definition>& definitions()
    {
        return s_definitions;
    }

    // The patch an option turns on: its description, or empty for none
    static std::string chosen(const patch_option& option, const option_getter& get)
    {
        return get(option.key.c_str()) == "enabled" ? option.descriptions.front() : std::string{};
    }

    void update_display(retro_environment_t cb, const option_getter& get)
    {
        if (!cb)
            return;

        // A group's switch turned on turns the group's others off
        for (const patch_option& option : s_options)
        {
            if (option.group.empty())
                continue;
            const std::string value = get(option.key.c_str());
            const bool turned_on = value == "enabled" && s_last_values[option.key] != "enabled";
            if (!turned_on)
                continue;
            for (const patch_option& other : s_options)
            {
                if (&other == &option || other.group != option.group || get(other.key.c_str()) != "enabled")
                    continue;
                retro_variable var{other.key.c_str(), "disabled"};
                cb(RETRO_ENVIRONMENT_SET_VARIABLE, &var);
                s_last_values[other.key] = "disabled";
            }
        }
        for (const patch_option& option : s_options)
            s_last_values[option.key] = get(option.key.c_str());

        for (const patch_option& option : s_options)
        {
            const std::string on = chosen(option, get);
            for (const auto& [description, configs] : option.config_values)
            {
                for (const config_value_option& config : configs)
                {
                    retro_core_option_display display{config.key.c_str(), description == on};
                    cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &display);
                }
            }
        }
    }

    void write_config(const option_getter& get)
    {
        // The whole file from the options: what standalone or an earlier game
        // left in it would otherwise stay on
        patch_engine::patch_map config;

        std::set<std::string> groups_on;
        for (const patch_option& option : s_options)
        {
            const std::string on = chosen(option, get);
            if (on.empty())
                continue;
            // One of a group at most, should the .opt file have more on
            if (!option.group.empty() && !groups_on.insert(option.group).second)
            {
                lr_patch_log.warning("%s: another patch of group %s is on already", on, option.group);
                continue;
            }

            // The values picked, by name
            std::map<std::string, f64> picked;
            if (const auto it = option.config_values.find(on); it != option.config_values.end())
            {
                for (const config_value_option& value : it->second)
                {
                    const std::string label = get(value.key.c_str());
                    for (const auto& [value_label, number] : value.values)
                    {
                        if (value_label == label)
                            picked[value.name] = number;
                    }
                }
            }

            for (const auto& [hash, container] : s_map)
            {
                const auto info = container.patch_info_map.find(on);
                if (info == container.patch_info_map.end())
                    continue;

                patch_engine::patch_info& out = config[hash].patch_info_map[on];
                out.default_config_values = info->second.default_config_values;
                // An "All titles" patch is configured for all, as standalone
                // does; RPCS3 matches its hash itself
                const std::string& serial_key = option.all_titles ? patch_key::all : s_serial;
                for (const auto& [title, serials] : info->second.titles)
                {
                    const auto serial = serials.find(serial_key);
                    if (serial == serials.end())
                        continue;
                    for (const auto& [app_version, values] : serial->second)
                    {
                        patch_engine::patch_config_values& out_values = out.titles[title][serial_key][app_version];
                        out_values.enabled = true;
                        out_values.config_values = info->second.default_config_values;
                        for (const auto& [name, number] : picked)
                            out_values.config_values[name].value = number;
                    }
                }
            }
        }

        patch_engine::save_config(config);
    }
}
